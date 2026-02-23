#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "ClientHandler.h"
#include "GridGuard.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "WorkCompletion.h"
#include "JWTValidator.h"
#include "UserConfigDB.h"
#include "Logger.h"
#include "cJSON.h"

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
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Database error");
        return;
    }
    if (found == 1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,
                               "User config not set. Use PUT /user/config first.");
        return;
    }

    WorkCompletion wc;
    if (WorkCompletion_Initiate(&wc) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Internal error");
        return;
    }

    WorkRequest req = {
        .clientFd   = fd,
        .completion = &wc
    };
    snprintf(req.lat, sizeof(req.lat), "%.4f", cfg.latitude);
    snprintf(req.lon, sizeof(req.lon), "%.4f", cfg.longitude);
    strncpy(req.region,   cfg.region,      sizeof(req.region)   - 1);
    strncpy(req.location, claims->subject, sizeof(req.location) - 1);
    req.solarAreaM2     = cfg.solarAreaM2;
    req.solarEfficiency = cfg.solarEfficiency;
    req.consumptionKwh  = cfg.consumptionKwh;

    LOG_INFO("ClientHandler: Forecast for user=%s lat=%s lon=%s region=%s solar=%.1fm²/%.0f%% load=%.2fkWh/h",
             claims->subject, req.lat, req.lon, req.region,
             req.solarAreaM2, req.solarEfficiency * 100.0, req.consumptionKwh);

    if (GridGuard_SubmitRequest(app, &req) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Queue full, try again later");
        WorkCompletion_Destroy(&wc);
        return;
    }

    if (WorkCompletion_Wait(&wc) == 0)
        HTTPResponse_SendJson(fd, wc.json);
    else
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Pipeline error or timeout");

    WorkCompletion_Destroy(&wc);
}

// GET /user/config — returns the stored config for the authenticated user.
static void HandleGetUserConfig(int fd, struct GridGuard *app, const JWTClaims *claims)
{
    UserConfig cfg;
    int found = UserConfigDB_Get(&app->db, claims->subject, &cfg);
    if (found == -1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Database error");
        return;
    }
    if (found == 1)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "No config found");
        return;
    }

    char json[512];
    snprintf(json, sizeof(json),
             "{\"latitude\":%.4f,\"longitude\":%.4f,"
             "\"region\":\"%s\",\"solar_area_m2\":%.2f,"
             "\"solar_efficiency\":%.3f,\"consumption_kwh\":%.3f,"
             "\"updated_at\":%ld}",
             cfg.latitude, cfg.longitude, cfg.region,
             cfg.solarAreaM2, cfg.solarEfficiency,
             cfg.consumptionKwh, cfg.updatedAt);
    HTTPResponse_SendJson(fd, json);
}

// PUT /user/config — persist lat/lon/region/solar for the authenticated user.
static void HandlePutUserConfig(int fd, struct GridGuard *app, const JWTClaims *claims,
                                const HTTPRequest *request)
{
    cJSON *json = cJSON_Parse(request->body);
    if (!json)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST, "Invalid JSON");
        return;
    }

    cJSON *jLat         = cJSON_GetObjectItemCaseSensitive(json, "latitude");
    cJSON *jLon         = cJSON_GetObjectItemCaseSensitive(json, "longitude");
    cJSON *jRegion      = cJSON_GetObjectItemCaseSensitive(json, "region");
    cJSON *jArea        = cJSON_GetObjectItemCaseSensitive(json, "solar_area_m2");
    cJSON *jEff         = cJSON_GetObjectItemCaseSensitive(json, "solar_efficiency");
    cJSON *jConsumption = cJSON_GetObjectItemCaseSensitive(json, "consumption_kwh");

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
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,
                               "Invalid coordinates: latitude must be -90..90, longitude -180..180");
        cJSON_Delete(json);
        return;
    }

    double area = cJSON_IsNumber(jArea) ? jArea->valuedouble : 0.0;
    double eff  = cJSON_IsNumber(jEff)  ? jEff->valuedouble  : 0.0;
    double load = cJSON_IsNumber(jConsumption) ? jConsumption->valuedouble : 0.5;

    if (area < 0.0 || area > 10000.0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,
                               "Invalid solar_area_m2: must be 0..10000");
        cJSON_Delete(json);
        return;
    }
    if (eff < 0.0 || eff > 1.0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,
                               "Invalid solar_efficiency: must be 0.0..1.0");
        cJSON_Delete(json);
        return;
    }
    if (load < 0.0 || load > 1000.0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_400_BAD_REQUEST,
                               "Invalid consumption_kwh: must be 0..1000");
        cJSON_Delete(json);
        return;
    }

    UserConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.userId, claims->subject, sizeof(cfg.userId) - 1);
    cfg.latitude        = lat;
    cfg.longitude       = lon;
    strncpy(cfg.region, jRegion->valuestring, sizeof(cfg.region) - 1);
    cfg.solarAreaM2     = area;
    cfg.solarEfficiency = eff;
    cfg.consumptionKwh  = load;

    cJSON_Delete(json);

    if (UserConfigDB_Upsert(&app->db, &cfg) != 0)
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                               "Failed to save config");
        return;
    }

    LOG_INFO("ClientHandler: Saved config for user=%s lat=%.4f lon=%.4f region=%s",
             cfg.userId, cfg.latitude, cfg.longitude, cfg.region);
    HTTPResponse_SendJson(fd, "{\"status\":\"ok\"}");
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
        LOG_WARNING("ClientHandler: Unauthorized request to %s (fd=%d)",
                    request.path, fd);
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
    else
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "Not found");
    }

    close(fd);
}
