#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "server/ClientHandler.h"
#include "server/GridGuard.h"
#include "net/HTTPRequest.h"
#include "net/HTTPResponse.h"
#include "sys/WorkCompletion.h"
#include "auth/JWTValidator.h"
#include "db/UserConfigDB.h"
#include "db/ScheduleDB.h"
#include "domain/Scheduler.h"
#include "sys/Logger.h"
#include "libs/cJSON.h"

// These glibc extensions are not exposed by _POSIX_C_SOURCE — declare them directly.
extern time_t  timegm(struct tm *tm);
extern char   *strptime(const char *s, const char *format, struct tm *tm);

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

// GET /health — public, no auth required
static void HandleHealth(int fd)
{
    HTTPResponse_SendJson(fd, "{\"status\":\"ok\",\"service\":\"GridGuard\"}");
}

// GET /forecast — requires valid JWT + user config in DB.
// WorkCompletion lives on this thread's stack; safe because this thread
// blocks in WorkCompletion_Wait for the entire pipeline duration.
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

    WorkCompletion wc; // Stack-allocated WorkCompletion for this request's pipeline.
    if (WorkCompletion_Initiate(&wc) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Internal error");
        return;
    }

    WorkRequest req; // Build the WorkRequest from the user config and JWT claims.
    snprintf(req.lat, sizeof(req.lat), "%.4f", cfg.latitude);
    snprintf(req.lon, sizeof(req.lon), "%.4f", cfg.longitude);
    strncpy(req.region, cfg.region,      sizeof(req.region) - 1);
    strncpy(req.userId, claims->subject, sizeof(req.userId) - 1);
    strncpy(req.location, cfg.location,  sizeof(req.location) - 1);
    req.solarAreaM2     = cfg.solarAreaM2;
    req.solarEfficiency = cfg.solarEfficiency;
    req.consumptionKwh  = cfg.consumptionKwh;
    req.gridFee_low     = cfg.gridFee_low;
    req.gridFee_normal  = cfg.gridFee_normal;
    req.gridFee_high    = cfg.gridFee_high;

    LOG_INFO("ClientHandler: Forecast for user=%s lat=%s lon=%s region=%s solar=%.1fm²/%.0f%% load=%.2fkWh/h", claims->subject, req.lat, req.lon, req.region, req.solarAreaM2, req.solarEfficiency * 100.0, req.consumptionKwh);

    if (GridGuard_SubmitRequest(app, &req, &wc) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Queue full, try again later");
        WorkCompletion_Destroy(&wc);
        return;
    }

    if (WorkCompletion_Wait(&wc) == 0)
        HTTPResponse_SendJson(fd, wc.json);
    else
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Pipeline error or timeout");

    WorkCompletion_Destroy(&wc);
}

// GET /user/config — returns the stored config for the authenticated user.
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

    char json[512];
    snprintf(json, sizeof(json),
             "{\"location\":\"%s\",\"latitude\":%.4f,\"longitude\":%.4f,"
             "\"region\":\"%s\",\"solar_area_m2\":%.2f,"
             "\"solar_efficiency\":%.3f,\"consumption_kwh\":%.3f,"
             "\"updated_at\":%ld}",
             cfg.location, cfg.latitude, cfg.longitude, cfg.region,
             cfg.solarAreaM2, cfg.solarEfficiency,
             cfg.consumptionKwh, cfg.updatedAt);
    HTTPResponse_SendJson(fd, json);
}

// PUT /user/config — persist lat/lon/region/solar for the authenticated user.
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
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,
                               "Missing required fields: latitude, longitude, region");
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

    // Grid fees with defaults (typical Swedish Ellevio-like tariffs)
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
    cfg.gridFee_low     = gridFeeLow;
    cfg.gridFee_normal  = gridFeeNormal;
    cfg.gridFee_high    = gridFeeHigh;

    cJSON_Delete(json);

    if (UserConfigDB_Upsert(&app->db, &cfg) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return;
    }

    LOG_INFO("ClientHandler: Saved config for user=%s lat=%.4f lon=%.4f region=%s",
             cfg.userId, cfg.latitude, cfg.longitude, cfg.region);
    HTTPResponse_SendJson(fd, "{\"status\":\"ok\"}");
}

// ---------------------------------------------------------------------------
// Parse an ISO 8601 UTC time string ("2026-03-02T00:00:00Z") to time_t.
// Returns 0 on parse failure.
// ---------------------------------------------------------------------------
static time_t ParseISO8601UTC(const char *s)
{
    if (!s) return 0;
    struct tm tm = {0};
    if (!strptime(s, "%Y-%m-%dT%H:%M:%SZ", &tm))
        return 0;
    tm.tm_isdst = 0;
    return timegm(&tm);
}

// ---------------------------------------------------------------------------
// POST /schedule — find the cheapest time window for a shiftable load and
// save it to the database.
// ---------------------------------------------------------------------------
static void HandlePostSchedule(int fd, struct GridGuard *app, const JWTClaims *claims,
                                const HTTPRequest *request)
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
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,
                               "Invalid duration_minutes: must be 1..1440");
        cJSON_Delete(body);
        return;
    }
    if (powerKw <= 0.0 || powerKw > 1000.0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,
                               "Invalid power_kw: must be > 0 and <= 1000");
        cJSON_Delete(body);
        return;
    }

    char loadId[64] = {0};
    strncpy(loadId, jLoadId->valuestring, sizeof(loadId) - 1);
    cJSON_Delete(body);

    // Load user config to build the WorkRequest.
    UserConfig cfg;
    int found = UserConfigDB_Get(&app->db, claims->subject, &cfg);
    if (found != 0)
    {
        HTTPResponse_SendError(fd,
            found == 1 ? HTTP_STATUS_400_BAD_REQUEST : HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
            found == 1 ? "User config not set. Use PUT /user/config first." : "Database error");
        return;
    }

    // Run the forecast pipeline to get hourly cost data.
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
    req.gridFee_low     = cfg.gridFee_low;
    req.gridFee_normal  = cfg.gridFee_normal;
    req.gridFee_high    = cfg.gridFee_high;

    if (GridGuard_SubmitRequest(app, &req, &wc) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Queue full, try again later");
        WorkCompletion_Destroy(&wc);
        return;
    }

    if (WorkCompletion_Wait(&wc) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Pipeline error or timeout");
        WorkCompletion_Destroy(&wc);
        return;
    }

    // Parse the forecast JSON to build SchedulerEntry array.
    cJSON *forecast = cJSON_Parse(wc.json);
    WorkCompletion_Destroy(&wc);

    if (!forecast)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Failed to parse forecast data");
        return;
    }

    cJSON *fcArray = cJSON_GetObjectItemCaseSensitive(forecast, "forecast");
    if (!cJSON_IsArray(fcArray))
    {
        cJSON_Delete(forecast);
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Invalid forecast data");
        return;
    }

    int fcCount = cJSON_GetArraySize(fcArray);
    if (fcCount <= 0)
    {
        cJSON_Delete(forecast);
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "No forecast entries");
        return;
    }

    SchedulerEntry *entries = (SchedulerEntry *)calloc(fcCount, sizeof(SchedulerEntry));
    if (!entries)
    {
        cJSON_Delete(forecast);
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Out of memory");
        return;
    }

    int validCount = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, fcArray)
    {
        cJSON *jTime = cJSON_GetObjectItemCaseSensitive(item, "time");
        cJSON *jCost = cJSON_GetObjectItemCaseSensitive(item, "total_cost_sek_kwh");
        if (!cJSON_IsString(jTime) || !cJSON_IsNumber(jCost))
            continue;
        time_t ts = ParseISO8601UTC(jTime->valuestring);
        if (ts == 0)
            continue;
        entries[validCount].timestamp       = ts;
        entries[validCount].totalCostPerKwh = jCost->valuedouble;
        validCount++;
    }
    cJSON_Delete(forecast);

    if (validCount == 0)
    {
        free(entries);
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "No valid forecast entries for scheduling");
        return;
    }

    ScheduleWindow window;
    time_t nowTime = time(NULL);
    if (LoadScheduler_FindWindow(entries, validCount, durationMinutes, powerKw,
                                 deadline, nowTime, &window) != 0)
    {
        free(entries);
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,
                               "No valid window found within forecast and deadline");
        return;
    }
    free(entries);

    // Build and persist the schedule entry.
    ScheduleEntry sched;
    memset(&sched, 0, sizeof(sched));
    snprintf(sched.scheduleId, sizeof(sched.scheduleId), "%.127s_%ld",
             claims->subject, (long)nowTime);
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
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Failed to save schedule");
        return;
    }

    // Format scheduled_start as ISO 8601 for the response.
    char startStr[32] = {0};
    struct tm startTm;
    gmtime_r(&window.scheduledStart, &startTm);
    strftime(startStr, sizeof(startStr), "%Y-%m-%dT%H:%M:%SZ", &startTm);

    char json[512];
    snprintf(json, sizeof(json),
             "{\"schedule_id\":\"%s\",\"load_id\":\"%s\","
             "\"scheduled_start\":\"%s\","
             "\"duration_minutes\":%d,\"power_kw\":%.2f,"
             "\"estimated_cost_sek\":%.2f,\"savings_sek\":%.2f,"
             "\"status\":\"pending\"}",
             sched.scheduleId, sched.loadId, startStr,
             sched.durationMinutes, sched.powerKw,
             sched.estimatedCostSek, sched.savingsSek);

    LOG_INFO("ClientHandler: Schedule created %s for user=%s load=%s start=%s savings=%.2f SEK",
             sched.scheduleId, claims->subject, loadId, startStr, window.savingsSek);

    HTTPResponse_SendJson(fd, json);
}

// ---------------------------------------------------------------------------
// GET /schedule — list all pending/active schedules for the authenticated user.
// ---------------------------------------------------------------------------
static void HandleGetSchedules(int fd, struct GridGuard *app, const JWTClaims *claims)
{
    ScheduleEntry entries[SCHEDULE_MAX_PER_USER];
    int count = 0;

    if (ScheduleDB_GetByUser(&app->db, claims->subject,
                             entries, SCHEDULE_MAX_PER_USER, &count) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Database error");
        return;
    }

    // Build JSON array (fixed buffer — each entry is ~250 chars max).
    char buf[SCHEDULE_MAX_PER_USER * 300 + 32];
    int pos = snprintf(buf, sizeof(buf), "{\"schedules\":[");

    for (int i = 0; i < count; i++)
    {
        const ScheduleEntry *e = &entries[i];

        char startStr[32] = {0};
        struct tm tm_s;
        gmtime_r(&e->scheduledStart, &tm_s);
        strftime(startStr, sizeof(startStr), "%Y-%m-%dT%H:%M:%SZ", &tm_s);

        int n = snprintf(buf + pos, sizeof(buf) - pos,
                         "%s{\"schedule_id\":\"%s\",\"load_id\":\"%s\","
                         "\"scheduled_start\":\"%s\","
                         "\"duration_minutes\":%d,\"power_kw\":%.2f,"
                         "\"estimated_cost_sek\":%.2f,\"savings_sek\":%.2f,"
                         "\"status\":\"%s\"}",
                         i == 0 ? "" : ",",
                         e->scheduleId, e->loadId, startStr,
                         e->durationMinutes, e->powerKw,
                         e->estimatedCostSek, e->savingsSek,
                         e->status);
        if (n < 0 || (size_t)(pos + n) >= sizeof(buf) - 2)
            break;
        pos += n;
    }

    snprintf(buf + pos, sizeof(buf) - pos, "]}");
    HTTPResponse_SendJson(fd, buf);
}

// ---------------------------------------------------------------------------
// DELETE /schedule/:id — cancel a schedule.
// ---------------------------------------------------------------------------
static void HandleDeleteSchedule(int fd, struct GridGuard *app, const JWTClaims *claims,
                                 const char *scheduleId)
{
    int rc = ScheduleDB_Delete(&app->db, scheduleId, claims->subject);
    if (rc == -1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Database error");
        return;
    }
    if (rc == 1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND,
                               "Schedule not found or not owned by user");
        return;
    }

    LOG_INFO("ClientHandler: Schedule %s cancelled by user=%s", scheduleId, claims->subject);
    HTTPResponse_SendJson(fd, "{\"status\":\"cancelled\"}");
}

// ---------------------------------------------------------------------------
// Main entry point — called once per HTTP connection by a ThreadPool worker.
// ---------------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // Public endpoints — no auth required.
    // -----------------------------------------------------------------------
    if (strcmp(request.path, "/health") == 0)
    {
        HandleHealth(fd);
        close(fd);
        return;
    }

    // -----------------------------------------------------------------------
    // Auth gate — all other endpoints require a valid JWT.
    // -----------------------------------------------------------------------
    const char *token = HTTPRequest_GetBearerToken(&request);
    JWTClaims claims;

    if (!token || JWT_Validate(token, &claims) != 0)
    {
        LOG_WARNING("ClientHandler: Unauthorized request to %s (fd=%d)", request.path, fd);
        HTTPResponse_SendError(fd, HTTP_STATUS_401_UNAUTHORIZED, "Unauthorized");
        close(fd);
        return;
    }

    // -----------------------------------------------------------------------
    // Protected endpoints.
    // -----------------------------------------------------------------------
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
