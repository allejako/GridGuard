#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <unistd.h>

#include "ClientHandler.h"
#include "GridGuard.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "WorkCompletion.h"
#include "JWTValidator.h"
#include "Logger.h"

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

// GET /health — public, no auth required
static void HandleHealth(int fd)
{
    HTTPResponse_SendJson(fd, "{\"status\":\"ok\",\"service\":\"GridGuard\"}");
}

// GET /forecast — requires valid JWT.
// WorkCompletion lives on this thread's stack; safe because this thread
// blocks in WorkCompletion_Wait for the entire pipeline duration.
static void HandleForecast(int fd, struct GridGuard *app, const JWTClaims *claims)
{
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
    // TODO Fas 3: replace hardcoded location/region with user config from SQLite
    //             looked up by claims->subject (user_id).
    strncpy(req.location, "stockholm", sizeof(req.location) - 1);
    strncpy(req.region,   "SE3",       sizeof(req.region) - 1);

    LOG_INFO("ClientHandler: Forecast for user=%s location=%s region=%s",
             claims->subject, req.location, req.region);

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
    else if (strcmp(request.path, "/weather") == 0)
    {
        // TODO Fas 3: fetch real weather data for claims->subject's location
        HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "Not implemented yet");
    }
    else if (strcmp(request.path, "/spotprice") == 0)
    {
        // TODO Fas 3: fetch real spot prices for claims->subject's region
        HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "Not implemented yet");
    }
    else
    {
        HTTPResponse_SendError(fd, HTTP_STATUS_404_NOT_FOUND, "Not found");
    }

    close(fd);
}
