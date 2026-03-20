#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "server/ClientHandler.h"
#include "server/GridGuard.h"
#include "cache/SharedCache.h"
#include "net/HTTPRequest.h"
#include "net/HTTPResponse.h"
#include "sys/WorkCompletion.h"
#include "auth/JWTValidator.h"
#include "db/UserConfigDB.h"
#include "db/ScheduleDB.h"
#include "compute/LoadScheduler.h"
#include "watchdog/Metrics.h"
#include "sys/Logger.h"
#include "libs/cJSON.h"

extern time_t  timegm(struct tm *tm);
extern char   *strptime(const char *s, const char *format, struct tm *tm);

// Timestamp file path for cache invalidation checking
#define DATA_UPDATE_TIMESTAMP_PATH "/tmp/gridguard_last_data_update"

// Helper: Read the last data update timestamp from Fetcher's signal file
// Returns 0 if file doesn't exist or can't be read (never invalidate in that case)
static time_t ReadLastDataUpdate(void)
{
    FILE *f = fopen(DATA_UPDATE_TIMESTAMP_PATH, "r");
    if (!f)
        return 0;

    time_t timestamp = 0;
    if (fscanf(f, "%ld", &timestamp) != 1)
        timestamp = 0;

    fclose(f);
    return timestamp;
}

// Parse ISO 8601 UTC timestamp ("2026-03-09T14:30:00Z") to time_t.
static time_t ParseISO8601(const char *s)
{
    if (!s) return 0;
    struct tm tm = {0};
    if (!strptime(s, "%Y-%m-%dT%H:%M:%SZ", &tm))
        return 0;
    tm.tm_isdst = 0;
    return timegm(&tm);
}

// GET / - GRIDGUARD ROOT // Simple welcome page with API documentation and quick start instructions.
static void HandleRoot(int fd)
{
    const char *welcome =
        "<!DOCTYPE html>\n"
        "<html><head><title>GridGuard</title>\n"
        "<meta charset='utf-8'>\n"
        "<style>\n"
        "body { background: #fff; color: #1a1a1a; margin: 0; padding: 60px 20px; font-family: 'SF Mono', 'Monaco', 'Courier New', monospace; }\n"
        "pre { font-size: 14px; line-height: 1.6; max-width: 800px; margin: 0 auto; white-space: pre; }\n"
        ".brand { font-size: 28px; font-weight: 900; color: #000; letter-spacing: 0.15em; }\n"
        ".separator { color: #000; margin: 6px 0 20px 0; }\n"
        ".tagline { color: #666; font-size: 13px; margin-bottom: 40px; }\n"
        ".section-title { color: #000; font-weight: bold; }\n"
        ".endpoint { color: #333; }\n"
        ".arrow { color: #999; }\n"
        ".comment { color: #999; }\n"
        ".divider { color: #ddd; }\n"
        "</style>\n"
        "</head><body>\n"
        "<pre>\n"
        "\n"
        "  <span class='brand'>GRIDGUARD</span>\n"
        "  <span class='separator'>━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━</span>\n"
        "  <span class='tagline'>Energy optimization for Swedish households</span>\n"
        "\n"
        "\n"
        "  <span class='section-title'>PUBLIC ENDPOINTS</span>\n"
        "\n"
        "    <span class='endpoint'>GET  /</span>              <span class='arrow'>→</span> This page\n"
        "    <span class='endpoint'>GET  /health</span>        <span class='arrow'>→</span> Health check\n"
        "    <span class='endpoint'>GET  /metrics</span>       <span class='arrow'>→</span> Process metrics\n"
        "\n"
        "\n"
        "  <span class='section-title'>AUTHENTICATED ENDPOINTS</span> <span class='comment'>(JWT required)</span>\n"
        "\n"
        "    <span class='endpoint'>GET     /forecast</span>      <span class='arrow'>→</span> 96h energy forecast\n"
        "    <span class='endpoint'>GET/PUT /user/config</span>   <span class='arrow'>→</span> User configuration\n"
        "    <span class='endpoint'>GET     /schedule</span>      <span class='arrow'>→</span> List scheduled loads\n"
        "    <span class='endpoint'>POST    /schedule</span>      <span class='arrow'>→</span> Schedule new load\n"
        "    <span class='endpoint'>DELETE  /schedule/:id</span> <span class='arrow'>→</span> Cancel schedule\n"
        "\n"
        "\n"
        "  <span class='section-title'>QUICK START</span>\n"
        "\n"
        "    <span class='comment'># Start server + live dashboard (recommended)</span>\n"
        "    make client\n"
        "\n"
        "    <span class='comment'># Dashboard refreshes every 60s automatically</span>\n"
        "    <span class='comment'># Custom interval: GridGuard-client forecast --watch --interval 30</span>\n"
        "\n"
        "\n"
        "  <span class='section-title'>DOCUMENTATION</span>\n"
        "\n"
        "    docs/API.md          <span class='comment'>Complete API reference</span>\n"
        "    docs/ARCHITECTURE.md <span class='comment'>System design deep-dive</span>\n"
        "    README.md            <span class='comment'>Getting started</span>\n"
        "\n"
        "\n"
        "  <span class='divider'>─────────────────────────────────────────────────────────────────────</span>\n"
        "\n"
        "  <span class='comment'>LINTECH 2026</span>\n"
        "\n"
        "</pre>\n"
        "</body></html>";

    char response[8192];
    snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\n" "Content-Type: text/html; charset=utf-8\r\n" "Content-Length: %zu\r\n" "Connection: close\r\n" "\r\n" "%s", strlen(welcome), welcome);
    write(fd, response, strlen(response));
}

// GET /health
static void HandleHealth(int fd)
{
    HTTPResponse_SendJson(fd, "{\"status\":\"ok\",\"service\":\"GridGuard\"}");
}

// GET /metrics — returns watchdog and process metrics for monitoring
static void HandleMetrics(int fd)
{
    WatchdogMetrics *metrics = Metrics_Open();
    if (!metrics)
    {
        HTTPResponse_SendJson(fd, "{\"error\":\"Watchdog metrics not available\"}");
        return;
    }

    time_t now = time(NULL);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "service", "GridGuard");
    cJSON_AddNumberToObject(root, "timestamp", (double)now);

    // Watchdog info
    cJSON *watchdog = cJSON_AddObjectToObject(root, "watchdog");
    cJSON_AddNumberToObject(watchdog, "uptime_seconds", (double)difftime(now, metrics->watchdogStartTime));
    cJSON_AddNumberToObject(watchdog, "restart_count", metrics->restartCount);
    cJSON_AddNumberToObject(watchdog, "max_restarts", metrics->maxRestarts);
    cJSON_AddNumberToObject(watchdog, "restart_window_seconds", metrics->restartWindowSec);

    if (metrics->lastRestartTime > 0)
    {
        cJSON_AddNumberToObject(watchdog, "last_restart_seconds_ago", (double)difftime(now, metrics->lastRestartTime));
    }

    // Fetcher process
    cJSON *fetcher = cJSON_AddObjectToObject(root, "fetcher");
    cJSON_AddNumberToObject(fetcher, "pid", (int)metrics->fetcherPid);
    cJSON_AddNumberToObject(fetcher, "uptime_seconds", (double)difftime(now, metrics->fetcherStartTime));
    cJSON_AddNumberToObject(fetcher, "last_heartbeat_seconds_ago", (double)difftime(now, metrics->fetcherLastHeartbeat));

    // Parser process
    cJSON *parser = cJSON_AddObjectToObject(root, "parser");
    cJSON_AddNumberToObject(parser, "pid", (int)metrics->parserPid);
    cJSON_AddNumberToObject(parser, "uptime_seconds", (double)difftime(now, metrics->parserStartTime));
    cJSON_AddNumberToObject(parser, "last_heartbeat_seconds_ago", (double)difftime(now, metrics->parserLastHeartbeat));

    // Server process
    cJSON *server = cJSON_AddObjectToObject(root, "server");
    cJSON_AddNumberToObject(server, "pid", (int)metrics->serverPid);
    cJSON_AddNumberToObject(server, "uptime_seconds", (double)difftime(now, metrics->serverStartTime));
    cJSON_AddNumberToObject(server, "last_heartbeat_seconds_ago", (double)difftime(now, metrics->serverLastHeartbeat));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    Metrics_Close(metrics);

    if (json)
    {
        HTTPResponse_SendJson(fd, json);
        free(json);
    }
    else
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Failed to generate metrics");
    }
}

// GET /forecast - returns 96h energy forecast with pricing and recommendations.
static void HandleForecast(int fd, struct GridGuard *app, const JWTClaims *claims)
{
    UserConfig cfg;
    int found = UserConfigDB_Get(&app->db, claims->subject, &cfg);
    if (found == -1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Database error");
        return;
    }
    if (found == 1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "User config not set. Use PUT /user/config first.");
        return;
    }

    // Check cache before running pipeline.
    // Include today's date in cache key to prevent serving yesterday's forecast
    time_t now = time(NULL);
    struct tm nowTm;
    gmtime_r(&now, &nowTm);

    char cacheKey[SHARED_CACHE_KEY_MAX];
    snprintf(cacheKey, sizeof(cacheKey), "%04d%02d%02d:%.2f,%.2f:%s:%.1f", nowTm.tm_year + 1900, nowTm.tm_mon + 1, nowTm.tm_mday, cfg.latitude, cfg.longitude, cfg.region, cfg.solarAreaM2);

    // Smart cache invalidation: Check if input data (weather/prices) has been updated.
    // If yes, invalidate ALL forecast caches (not just this user's) to prevent race conditions.
    // This is safe because we only invalidate when data actually changes (not on every request).
    // Uses process-shared lastDataUpdateCheck to ensure only one worker thread invalidates per update.
    pthread_mutex_lock(&app->updateCheckMutex);
    time_t lastDataUpdate = ReadLastDataUpdate();

    if (lastDataUpdate > 0 && lastDataUpdate > app->lastDataUpdateCheck)
    {
        LOG_INFO("ClientHandler: New data detected (timestamp %ld), invalidating ALL forecast caches", lastDataUpdate);
        SharedCache_InvalidateAll(&app->forecastCache);
        app->lastDataUpdateCheck = lastDataUpdate;
    }
    pthread_mutex_unlock(&app->updateCheckMutex);

    char cachedJson[SHARED_CACHE_DATA_MAX];
    if (SharedCache_Lookup(&app->forecastCache, cacheKey, cachedJson, sizeof(cachedJson)) == 0)
    {
        LOG_INFO("ClientHandler: Cache HIT for user=%s key=%s (skipping Fetch+Parse)", claims->subject, cacheKey);
        HTTPResponse_SendJson(fd, cachedJson);
        return;
    }

    LOG_INFO("ClientHandler: Cache MISS for user=%s key=%s (running full pipeline)", claims->subject, cacheKey);

    // Run full pipeline: Fetcher → Parser → Compute.
    WorkCompletion wc;
    if (WorkCompletion_Initiate(&wc) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Internal error");
        return;
    }

    WorkRequest req;
    snprintf(req.lat, sizeof(req.lat), "%.4f", cfg.latitude);
    snprintf(req.lon, sizeof(req.lon), "%.4f", cfg.longitude);
    strncpy(req.region, cfg.region,      sizeof(req.region) - 1);
    strncpy(req.userId, claims->subject, sizeof(req.userId) - 1);
    strncpy(req.location, cfg.location,  sizeof(req.location) - 1);
    req.solarAreaM2     = cfg.solarAreaM2;
    req.solarEfficiency = cfg.solarEfficiency;
    req.consumptionKwh  = cfg.consumptionKwh;
    req.gridFeeLow    = cfg.gridFeeLow;
    req.gridFeeNormal = cfg.gridFeeNormal;
    req.gridFeeHigh   = cfg.gridFeeHigh;

    LOG_INFO("ClientHandler: Forecast for user=%s lat=%s lon=%s region=%s solar=%.1fm²/%.0f%% load=%.2fkWh/h", claims->subject, req.lat, req.lon, req.region, req.solarAreaM2, req.solarEfficiency * 100.0, req.consumptionKwh);

    if (GridGuard_SubmitRequest(app, &req, &wc) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Queue full, try again later");
        WorkCompletion_Shutdown(&wc);
        return;
    }

    if (WorkCompletion_Wait(&wc) == 0)
    {
        SharedCache_Store(&app->forecastCache, cacheKey, wc.json);
        HTTPResponse_SendJson(fd, wc.json);
    }
    else
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Pipeline error or timeout");
    }

    WorkCompletion_Shutdown(&wc);
}

// GET /user/config
static void HandleGetUserConfig(int fd, struct GridGuard *app, const JWTClaims *claims)
{
    UserConfig cfg;
    int found = UserConfigDB_Get(&app->db, claims->subject, &cfg);
    if (found == -1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Database error");
        return;
    }
    if (found == 1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "No config found");
        return;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "location", cfg.location);
    cJSON_AddNumberToObject(json, "latitude", cfg.latitude);
    cJSON_AddNumberToObject(json, "longitude", cfg.longitude);
    cJSON_AddStringToObject(json, "region", cfg.region);
    cJSON_AddNumberToObject(json, "solar_area_m2", cfg.solarAreaM2);
    cJSON_AddNumberToObject(json, "solar_efficiency", cfg.solarEfficiency);
    cJSON_AddNumberToObject(json, "consumption_kwh", cfg.consumptionKwh);
    cJSON_AddNumberToObject(json, "updated_at", cfg.updatedAt);

    char *jsonStr = cJSON_PrintUnformatted(json);
    HTTPResponse_SendJson(fd, jsonStr);
    free(jsonStr);
    cJSON_Delete(json);
}

// PUT /user/config
static void HandlePutUserConfig(int fd, struct GridGuard *app, const JWTClaims *claims, const HTTPRequest *request)
{
    cJSON *json = cJSON_Parse(request->body);
    if (!json)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "Invalid JSON");
        return;
    }

    cJSON *jLocation    = cJSON_GetObjectItemCaseSensitive(json, "location");
    cJSON *jLat         = cJSON_GetObjectItemCaseSensitive(json, "latitude");
    cJSON *jLon         = cJSON_GetObjectItemCaseSensitive(json, "longitude");
    cJSON *jRegion      = cJSON_GetObjectItemCaseSensitive(json, "region");
    cJSON *jArea        = cJSON_GetObjectItemCaseSensitive(json, "solar_area_m2");
    cJSON *jEff         = cJSON_GetObjectItemCaseSensitive(json, "solar_efficiency");
    cJSON *jConsumption = cJSON_GetObjectItemCaseSensitive(json, "consumption_kwh");
    cJSON *jGridFeeLow  = cJSON_GetObjectItemCaseSensitive(json, "grid_fee_low");
    cJSON *jGridFeeNormal = cJSON_GetObjectItemCaseSensitive(json, "grid_fee_normal");
    cJSON *jGridFeeHigh = cJSON_GetObjectItemCaseSensitive(json, "grid_fee_high");

    if (!cJSON_IsNumber(jLat) || !cJSON_IsNumber(jLon) || !cJSON_IsString(jRegion))
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "Missing required fields: latitude, longitude, region");
        cJSON_Delete(json);
        return;
    }

    double lat = jLat->valuedouble;
    double lon = jLon->valuedouble;
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "Invalid coordinates: latitude must be -90..90, longitude -180..180");
        cJSON_Delete(json);
        return;
    }

    double area = cJSON_IsNumber(jArea) ? jArea->valuedouble : 0.0;
    double eff  = cJSON_IsNumber(jEff)  ? jEff->valuedouble  : 0.0;
    double load = cJSON_IsNumber(jConsumption) ? jConsumption->valuedouble : 0.5;
    double gridFeeLow = cJSON_IsNumber(jGridFeeLow) ? jGridFeeLow->valuedouble : 0.25;
    double gridFeeNormal = cJSON_IsNumber(jGridFeeNormal) ? jGridFeeNormal->valuedouble : 0.35;
    double gridFeeHigh = cJSON_IsNumber(jGridFeeHigh) ? jGridFeeHigh->valuedouble : 0.45;

    if (area < 0.0 || area > 10000.0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,"Invalid solar_area_m2: must be 0..10000");
        cJSON_Delete(json);
        return;
    }
    if (eff < 0.0 || eff > 1.0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "Invalid solar_efficiency: must be 0.0..1.0");
        cJSON_Delete(json);
        return;
    }
    if (load < 0.0 || load > 1000.0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "Invalid consumption_kwh: must be 0..1000");
        cJSON_Delete(json);
        return;
    }
    if (gridFeeLow < 0.0 || gridFeeLow > 10.0 || gridFeeNormal < 0.0 || gridFeeNormal > 10.0 || gridFeeHigh < 0.0 || gridFeeHigh > 10.0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "Invalid grid fees: must be 0..10 kr/kWh");
        cJSON_Delete(json);
        return;
    }

    UserConfig cfg;  
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.userId, claims->subject, sizeof(cfg.userId) - 1);
    if (cJSON_IsString(jLocation) && jLocation->valuestring)
        strncpy(cfg.location, jLocation->valuestring, sizeof(cfg.location) - 1);
    cfg.latitude        = lat;
    cfg.longitude       = lon;
    strncpy(cfg.region, jRegion->valuestring, sizeof(cfg.region) - 1);
    cfg.solarAreaM2     = area;
    cfg.solarEfficiency = eff;
    cfg.consumptionKwh  = load;
    cfg.gridFeeLow    = gridFeeLow;
    cfg.gridFeeNormal = gridFeeNormal;
    cfg.gridFeeHigh   = gridFeeHigh;

    cJSON_Delete(json);

    if (UserConfigDB_Upsert(&app->db, &cfg) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return;
    }

    LOG_INFO("ClientHandler: Saved config for user=%s lat=%.4f lon=%.4f region=%s", cfg.userId, cfg.latitude, cfg.longitude, cfg.region);
    HTTPResponse_SendJson(fd, "{\"status\":\"ok\"}");
}

// POST /schedule - find cheapest time window for a shiftable load.
static void HandlePostSchedule(int fd, struct GridGuard *app, const JWTClaims *claims, const HTTPRequest *request)
{
    cJSON *body = cJSON_Parse(request->body);
    if (!body)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "Invalid JSON");
        return;
    }

    cJSON *jLoadId   = cJSON_GetObjectItemCaseSensitive(body, "load_id");
    cJSON *jDuration = cJSON_GetObjectItemCaseSensitive(body, "duration_minutes");
    cJSON *jPower    = cJSON_GetObjectItemCaseSensitive(body, "power_kw");
    cJSON *jDeadline = cJSON_GetObjectItemCaseSensitive(body, "deadline");

    if (!cJSON_IsString(jLoadId) || !cJSON_IsNumber(jDuration) || !cJSON_IsNumber(jPower))
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,
                               "Missing required fields: load_id, duration_minutes, power_kw");
        cJSON_Delete(body);
        return;
    }

    int    durationMinutes = (int)jDuration->valuedouble;
    double powerKw         = jPower->valuedouble;
    time_t deadline        = cJSON_IsNumber(jDeadline) ? (time_t)jDeadline->valuedouble : 0;

    if (durationMinutes <= 0 || durationMinutes > 1440)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "Invalid duration_minutes: must be 1..1440");
        cJSON_Delete(body);
        return;
    }
    if (powerKw <= 0.0 || powerKw > 1000.0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "Invalid power_kw: must be > 0 and <= 1000");
        cJSON_Delete(body);
        return;
    }

    char loadId[64] = {0};
    strncpy(loadId, jLoadId->valuestring, sizeof(loadId) - 1);
    cJSON_Delete(body);

    UserConfig cfg;
    int found = UserConfigDB_Get(&app->db, claims->subject, &cfg);
    if (found != 0)
    {
        HTTPResponse_SendError(fd,
            found == 1 ? HTTP_STATUS_400_BAD_REQUEST : HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
            found == 1 ? "User config not set. Use PUT /user/config first." : "Database error");
        return;
    }

    WorkCompletion wc;
    if (WorkCompletion_Initiate(&wc) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Internal error");
        return;
    }

    WorkRequest req;
    memset(&req, 0, sizeof(req));
    snprintf(req.lat, sizeof(req.lat), "%.4f", cfg.latitude);
    snprintf(req.lon, sizeof(req.lon), "%.4f", cfg.longitude);
    strncpy(req.region,   cfg.region,       sizeof(req.region)   - 1);
    strncpy(req.userId,   claims->subject,  sizeof(req.userId)   - 1);
    strncpy(req.location, cfg.location,     sizeof(req.location) - 1);
    req.solarAreaM2     = cfg.solarAreaM2;
    req.solarEfficiency = cfg.solarEfficiency;
    req.consumptionKwh  = cfg.consumptionKwh;
    req.gridFeeLow    = cfg.gridFeeLow;
    req.gridFeeNormal = cfg.gridFeeNormal;
    req.gridFeeHigh   = cfg.gridFeeHigh;

    if (GridGuard_SubmitRequest(app, &req, &wc) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Queue full, try again later");
        WorkCompletion_Shutdown(&wc);
        return;
    }

    if (WorkCompletion_Wait(&wc) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Pipeline error or timeout");
        WorkCompletion_Shutdown(&wc);
        return;
    }

    // Parse forecast to extract 15-minute quarter prices for scheduling.
    cJSON *forecast = cJSON_Parse(wc.json);
    WorkCompletion_Shutdown(&wc);

    if (!forecast)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Failed to parse forecast data");
        return;
    }

    cJSON *fcArray = cJSON_GetObjectItemCaseSensitive(forecast, "quarters");
    if (!cJSON_IsArray(fcArray))
    {
        cJSON_Delete(forecast);
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Invalid forecast data (missing 'quarters' array)");
        return;
    }

    int fcCount = cJSON_GetArraySize(fcArray);
    if (fcCount <= 0)
    {
        cJSON_Delete(forecast);
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "No forecast entries");
        return;
    }

    SchedulerEntry *entries = (SchedulerEntry *)calloc(fcCount, sizeof(SchedulerEntry));
    if (!entries)
    {
        cJSON_Delete(forecast);
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return;
    }

    int validCount = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, fcArray)
    {
        cJSON *jTime = cJSON_GetObjectItemCaseSensitive(item, "time");
        cJSON *jCost = cJSON_GetObjectItemCaseSensitive(item, "total_cost_sek_kwh");
        cJSON *jProd = cJSON_GetObjectItemCaseSensitive(item, "production_kwh");  // Solar production

        if (!cJSON_IsString(jTime) || !cJSON_IsNumber(jCost))
            continue;
        time_t ts = ParseISO8601(jTime->valuestring);
        if (ts == 0)
            continue;

        entries[validCount].timestamp       = ts;
        entries[validCount].totalCostPerKwh = jCost->valuedouble;
        entries[validCount].productionKwh   = cJSON_IsNumber(jProd) ? jProd->valuedouble : 0.0;  // 0.0 if no solar
        validCount++;
    }
    cJSON_Delete(forecast);

    if (validCount == 0)
    {
        free(entries);
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "No valid forecast entries for scheduling");
        return;
    }

    ScheduleWindow window;
    time_t nowTime = time(NULL);
    if (LoadScheduler_FindWindow(entries, validCount, durationMinutes, powerKw, deadline, nowTime, &window) != 0)
    {
        free(entries);
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "No valid window found within forecast and deadline");
        return;
    }
    free(entries);

    // Save schedule to database.
    ScheduleEntry sched; 
    memset(&sched, 0, sizeof(sched));
    snprintf(sched.scheduleId, sizeof(sched.scheduleId), "%.127s_%ld", claims->subject, (long)nowTime);
    strncpy(sched.userId,  claims->subject, sizeof(sched.userId)  - 1);
    strncpy(sched.loadId,  loadId,          sizeof(sched.loadId)  - 1);
    sched.scheduledStart   = window.scheduledStart;
    sched.durationMinutes  = window.durationMinutes;
    sched.powerKw          = window.powerKw;
    sched.estimatedCostSek = window.estimatedCostSek;
    sched.savingsSek       = window.savingsSek;
    strncpy(sched.status, "pending", sizeof(sched.status) - 1);
    sched.createdAt = nowTime;

    if (ScheduleDB_Insert(&app->db, &sched) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Failed to save schedule");
        return;
    }

    char startStr[32];
    struct tm startTm;
    gmtime_r(&window.scheduledStart, &startTm);
    strftime(startStr, sizeof(startStr), "%Y-%m-%dT%H:%M:%SZ", &startTm);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "schedule_id", sched.scheduleId);
    cJSON_AddStringToObject(json, "load_id", sched.loadId);
    cJSON_AddStringToObject(json, "scheduled_start", startStr);
    cJSON_AddNumberToObject(json, "duration_minutes", sched.durationMinutes);
    cJSON_AddNumberToObject(json, "power_kw", sched.powerKw);
    cJSON_AddNumberToObject(json, "estimated_cost_sek", sched.estimatedCostSek);
    cJSON_AddNumberToObject(json, "savings_sek", sched.savingsSek);
    cJSON_AddStringToObject(json, "status", "pending");

    char *jsonStr = cJSON_PrintUnformatted(json);
    HTTPResponse_SendJson(fd, jsonStr);
    free(jsonStr);
    cJSON_Delete(json);

    LOG_INFO("ClientHandler: Schedule created %s for user=%s load=%s start=%s savings=%.2f SEK", sched.scheduleId, claims->subject, loadId, startStr, window.savingsSek);
}

// GET /schedule
static void HandleGetSchedules(int fd, struct GridGuard *app, const JWTClaims *claims)
{
    ScheduleEntry entries[SCHEDULE_MAX_PER_USER];
    int count = 0;

    if (ScheduleDB_GetByUser(&app->db, claims->subject, entries, SCHEDULE_MAX_PER_USER, &count) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Database error");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *schedules = cJSON_CreateArray();

    for (int i = 0; i < count; i++)
    {
        const ScheduleEntry *e = &entries[i];

        char startStr[32];
        struct tm tmS;
        gmtime_r(&e->scheduledStart, &tmS);
        strftime(startStr, sizeof(startStr), "%Y-%m-%dT%H:%M:%SZ", &tmS);

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "schedule_id", e->scheduleId);
        cJSON_AddStringToObject(item, "load_id", e->loadId);
        cJSON_AddStringToObject(item, "scheduled_start", startStr);
        cJSON_AddNumberToObject(item, "duration_minutes", e->durationMinutes);
        cJSON_AddNumberToObject(item, "power_kw", e->powerKw);
        cJSON_AddNumberToObject(item, "estimated_cost_sek", e->estimatedCostSek);
        cJSON_AddNumberToObject(item, "savings_sek", e->savingsSek);
        cJSON_AddStringToObject(item, "status", e->status);
        cJSON_AddItemToArray(schedules, item);
    }

    cJSON_AddItemToObject(root, "schedules", schedules);
    char *jsonStr = cJSON_PrintUnformatted(root);
    HTTPResponse_SendJson(fd, jsonStr);
    free(jsonStr);
    cJSON_Delete(root);
}

// DELETE /schedule/:id
static void HandleDeleteSchedule(int fd, struct GridGuard *app, const JWTClaims *claims, const char *scheduleId)
{
    int rc = ScheduleDB_Delete(&app->db, scheduleId, claims->subject);
    if (rc == -1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Database error");
        return;
    }
    if (rc == 1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "Schedule not found or not owned by user");
        return;
    }

    LOG_INFO("ClientHandler: Schedule %s cancelled by user=%s", scheduleId, claims->subject);
    HTTPResponse_SendJson(fd, "{\"status\":\"cancelled\"}");
}

// Parse HTTP request, route to handler, and send response.
void Client_HandleRequest(int fd, struct GridGuard *app)
{
    HTTPRequest request;

    if (HTTPRequest_Parse(fd, &request) != 0)
    {
        LOG_WARNING("ClientHandler: Failed to parse HTTP request on fd %d", fd);
        close(fd);
        return;
    }

    LOG_INFO("ClientHandler: %s %s (fd=%d)", request.method, request.path, fd);

    // Public endpoints.
    if (strcmp(request.path, "/") == 0)
    {
        HandleRoot(fd);
        close(fd);
        return;
    }

    if (strcmp(request.path, "/health") == 0)
    {
        HandleHealth(fd);
        close(fd);
        return;
    }

    if (strcmp(request.path, "/metrics") == 0)
    {
        HandleMetrics(fd);
        close(fd);
        return;
    }

    // Validate JWT for protected endpoints.
    const char *token = HTTPRequest_GetBearerToken(&request);
    JWTClaims claims;

    if (!token || JWTValidator_Validate(token, &claims) != 0)
    {
        LOG_WARNING("ClientHandler: Unauthorized request to %s (fd=%d)", request.path, fd);
        HTTPResponse_SendError(fd, HTTP_STATUS_401_UNAUTHORIZED, "Unauthorized");
        close(fd);
        return;
    }

    if (strcmp(request.path, "/forecast") == 0)
    {
        HandleForecast(fd, app, &claims);
    }
    else if (strcmp(request.path, "/user/config") == 0)
    {
        if (strcmp(request.method, "GET") == 0)
            HandleGetUserConfig(fd, app, &claims);
        else if (strcmp(request.method, "PUT") == 0)
            HandlePutUserConfig(fd, app, &claims, &request);
        else
            HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "Method not allowed");
    }
    else if (strcmp(request.path, "/schedule") == 0)
    {
        if (strcmp(request.method, "GET") == 0)
            HandleGetSchedules(fd, app, &claims);
        else if (strcmp(request.method, "POST") == 0)
            HandlePostSchedule(fd, app, &claims, &request);
        else
            HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "Method not allowed");
    }
    else if (strncmp(request.path, "/schedule/", 10) == 0 && strlen(request.path) > 10)
    {
        const char *scheduleId = request.path + 10;
        if (strcmp(request.method, "DELETE") == 0)
            HandleDeleteSchedule(fd, app, &claims, scheduleId);
        else
            HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "Method not allowed");
    }
    else
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "Not found");
    }

    close(fd);
}
