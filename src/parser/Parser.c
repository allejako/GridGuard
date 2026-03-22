#define _GNU_SOURCE

#include "parser/Parser.h"
#include "api/APIParser.h"
#include "domain/Forecast.h"
#include "api/OpenMeteoResponse.h"
#include "api/ElprisetResponse.h"
#include "sys/Logger.h"
#include "sys/ProcessHeartbeat.h"
#include "ipc/FetchResult.h"
#include "ipc/ParseResult.h"

#include <stdlib.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <math.h>

// Parse ISO 8601 timestamp till time_t (UTC).
// Hanterar tre format:
//   "2026-03-16T13:20:00"        (naiv UTC, t.ex. Open-Meteo med timezone=UTC)
//   "2026-03-16T13:20:00Z"       (explicit UTC)
//   "2026-03-16T00:00:00+01:00"  (med tidszonoffset, t.ex. Elpriset)
// Parse ISO8601 timestamp to UTC Unix timestamp
// Handles explicit timezone offsets (e.g., "+01:00") or applies context offset
// timeStr: ISO8601 string (e.g., "2026-03-17T00:00:00+01:00" or "2026-03-17T00:00")
// utc_offset_seconds: Timezone offset from API metadata (e.g., 3600 for CET)
//                     Use 0 if timestamp has explicit timezone or is already UTC
time_t ParseISO8601(const char *timeStr, int utcOffsetSeconds)
{
    struct tm tm = {0};
    int tzHour = 0, tzMin = 0;
    char tzSign = 'Z';

    int parsed = sscanf(timeStr, "%d-%d-%dT%d:%d:%d%c%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &tzSign, &tzHour, &tzMin);

    if (parsed < 6)
        sscanf(timeStr, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec);

    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = 0;

    // timegm() treats struct tm as UTC
    time_t result = timegm(&tm);

    // Explicit timezone in string? (e.g., "+01:00")
    if (parsed >= 9 && (tzSign == '+' || tzSign == '-'))
    {
        int tzOffset = tzHour * 3600 + tzMin * 60;
        result = (tzSign == '+') ? result - tzOffset : result + tzOffset;
    }
    // No explicit timezone? Apply API-provided offset
    else if (utcOffsetSeconds != 0)
    {
        result -= utcOffsetSeconds;
    }

    return result;
}

// Normalize timestamp to 15-minute boundary in UTC
// This ensures both Open-Meteo (UTC) and Elpriset (CET/CEST) timestamps
// can be matched reliably regardless of timezone differences
static time_t NormalizeTo15MinUtc(time_t timestamp)
{
    // Truncate to nearest 15-minute boundary (900 seconds)
    // Example: 10:07:32 -> 10:00:00, 10:23:45 -> 10:15:00
    return (timestamp / 900) * 900;
}

// Build forecast data by matching OpenMeteoResponse with ElprisetResponse based on timestamp
static void BuildForecastData(const OpenMeteoResponse *om, const ElprisetResponse *elpriset, const char *region __attribute__((unused)), ForecastData *forecast)
{
    memset(forecast, 0, sizeof(ForecastData));
    forecast->lastUpdated = time(NULL);

    LOG_INFO("ParserProcess: BuildForecastData() called with om->count=%d, elpriset->count=%d", om->count, elpriset->count);

    int count = 0;
    for (int i = 0; i < om->count && count < 192; i++) // 48h = 192 quarters
    {
        const OpenMeteoEntry *src = &om->entries[i];
        ForecastEntry *entry = &forecast->entries[count];

        // Parse Open-Meteo timestamp with timezone context from API metadata
        entry->timestamp = ParseISO8601(src->time, om->utcOffsetSeconds);
        entry->temperature = src->temperature2m;
        entry->humidity = src->humidity2m;
        entry->cloudCover = src->cloudCover;
        entry->windSpeed = src->windSpeed10m;
        entry->solarIrradiance = src->shortwaveRadiation;

        // Match electricity price to weather quarter
        // Both APIs provide 15-minute data. After parsing with timezone context,
        // both timestamps are in UTC and can be matched directly.
        entry->spotPriceSek = 0.0;
        entry->hasPriceData = false;

        // Normalize weather timestamp to 15-min boundary
        time_t weatherQuarter = NormalizeTo15MinUtc(entry->timestamp);

        for (int j = 0; j < elpriset->count; j++)
        {
            const ElprisetEntry *price = &elpriset->entries[j];
            // Elpriset has explicit timezone (+01:00), pass 0 for utc_offset
            time_t priceTime = ParseISO8601(price->timeStart, 0);
            time_t priceQuarter = NormalizeTo15MinUtc(priceTime);

            if (weatherQuarter == priceQuarter)
            {
                entry->spotPriceSek = price->sekPerKwh;
                entry->hasPriceData = true;
                break;
            }
        }

        // Log first 3 failed matches for debugging
        if (!entry->hasPriceData && i < 3)
        {
            struct tm weatherTime;
            gmtime_r(&entry->timestamp, &weatherTime);
            LOG_WARNING("ParserProcess: No price match for %04d-%02d-%02d %02d:%02d UTC (ts=%ld, quarter=%ld)", weatherTime.tm_year + 1900, weatherTime.tm_mon + 1, weatherTime.tm_mday, weatherTime.tm_hour, weatherTime.tm_min, (long)entry->timestamp, (long)weatherQuarter);
        }

        entry->valid = true;
        count++;
    }

    forecast->count = count;

    // Log matching statistics for production monitoring
    int matched = 0, unmatched = 0;
    for (int i = 0; i < count; i++)
    {
        if (forecast->entries[i].hasPriceData)
            matched++;
        else
            unmatched++;
    }

    LOG_INFO("ParserProcess: Built forecast with %d quarters (%.1f hours)", count, count / 4.0);
    LOG_INFO("ParserProcess: Price matching → %d matched, %d unmatched (%.1f%% coverage)", matched, unmatched, matched * 100.0 / count);

    if (unmatched > count / 2)
    {
        LOG_WARNING("ParserProcess: >50%% prices missing! Check timezone: OpenMeteo=%s (UTC%+d)", om->timezone, om->utcOffsetSeconds / 3600);
    }
}

int ParserProcess_Initiate(ParserProcess *proc, const char *fifoPath, const char *socketPath, const char *notifyPath)
{
    if (!proc || !fifoPath || !socketPath || !notifyPath)
        return -1;

    memset(proc, 0, sizeof(ParserProcess));
    strncpy(proc->fifoPath, fifoPath, sizeof(proc->fifoPath) - 1);
    strncpy(proc->socketPath, socketPath, sizeof(proc->socketPath) - 1);
    strncpy(proc->notifyPath, notifyPath, sizeof(proc->notifyPath) - 1);

    // Allocate APIParser service
    proc->parser = calloc(1, sizeof(APIParser));
    if (!proc->parser)
    {
        LOG_ERROR("ParserProcess: Failed to allocate APIParser");
        return -1;
    }

    if (APIParser_Initiate((APIParser *)proc->parser) != 0)
    {
        LOG_ERROR("ParserProcess: Failed to initiate APIParser");
        free(proc->parser);
        return -1;
    }

    // Open FIFO for reading, blocks until the fetch process opens the write end
    proc->fifoFd = open(fifoPath, O_RDONLY);
    if (proc->fifoFd < 0)
    {
        LOG_ERROR("ParserProcess: Failed to open FIFO %s for reading", fifoPath);
        APIParser_Shutdown((APIParser *)proc->parser);
        free(proc->parser);
        return -1;
    }

    // Create Unix domain socket server to communicate with the Compute thread
    proc->serverSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (proc->serverSocket < 0)
    {
        LOG_ERROR("ParserProcess: Failed to create Unix socket");
        close(proc->fifoFd);
        APIParser_Shutdown((APIParser *)proc->parser);
        free(proc->parser);
        return -1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);

    unlink(socketPath); // Remove old socket if it exists

    if (bind(proc->serverSocket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        LOG_ERROR("ParserProcess: Failed to bind Unix socket to %s", socketPath);
        close(proc->fifoFd);
        close(proc->serverSocket);
        APIParser_Shutdown((APIParser *)proc->parser);
        free(proc->parser);
        return -1;
    }

    if (listen(proc->serverSocket, 5) < 0)
    {
        LOG_ERROR("ParserProcess: Failed to listen on Unix socket");
        close(proc->fifoFd);
        close(proc->serverSocket);
        unlink(socketPath);
        APIParser_Shutdown((APIParser *)proc->parser);
        free(proc->parser);
        return -1;
    }

    // Create notify FIFO to signal Compute when data is ready
    unlink(notifyPath); // Remove old FIFO if it exists
    if (mkfifo(notifyPath, 0600) < 0 && errno != EEXIST)
    {
        LOG_ERROR("ParserProcess: Failed to create notify FIFO %s: %s", notifyPath, strerror(errno));
        close(proc->fifoFd);
        close(proc->serverSocket);
        unlink(socketPath);
        APIParser_Shutdown((APIParser *)proc->parser);
        free(proc->parser);
        return -1;
    }

    // Open notify FIFO for writing (will block until ComputeWorker opens read end)
    // This synchronizes Parser and ComputeWorker startup
    LOG_INFO("ParserProcess: Waiting for ComputeWorker to open notify FIFO %s", notifyPath);
    proc->notifyFd = open(notifyPath, O_WRONLY);
    if (proc->notifyFd < 0)
    {
        LOG_ERROR("ParserProcess: Failed to open notify FIFO %s: %s", notifyPath, strerror(errno));
        close(proc->fifoFd);
        close(proc->serverSocket);
        unlink(socketPath);
        unlink(notifyPath);
        APIParser_Shutdown((APIParser *)proc->parser);
        free(proc->parser);
        return -1;
    }

    LOG_INFO("ParserProcess: ComputeWorker connected via notify FIFO");

    proc->isRunning = true;
    LOG_INFO("ParserProcess: Initialized (PID %d, FIFO %s, Socket %s, Notify %s)", getpid(), fifoPath, socketPath, notifyPath);
    return 0;
}

int ParserProcess_Run(ParserProcess *proc)
{
    if (!proc || !proc->isRunning)
        return -1;

    APIParser *parser = (APIParser *)proc->parser;

    ProcessHeartbeat heartbeat;
    ProcessHeartbeat_Initiate(&heartbeat, 5);

    LOG_INFO("ParserProcess: Starting main loop");

    // State: we might have parsed data waiting to be sent, and/or a pending client connection
    ParseResult *pendingResult = NULL;
    int pendingClientSocket = -1;

    while (proc->isRunning)
    {
        ProcessHeartbeat_Send(&heartbeat);

        // Monitor BOTH FIFO and server socket simultaneously
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(proc->fifoFd, &readfds);
        FD_SET(proc->serverSocket, &readfds);

        int maxFd = (proc->fifoFd > proc->serverSocket) ? proc->fifoFd : proc->serverSocket;

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(maxFd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0)
        {
            LOG_ERROR("ParserProcess: select() failed: %s", strerror(errno));
            break;
        }

        if (ready == 0)
            continue; // Timeout - loop again to send heartbeat

        // Check if we have a new client connection
        if (FD_ISSET(proc->serverSocket, &readfds) && pendingClientSocket < 0)
        {
            pendingClientSocket = accept(proc->serverSocket, NULL, NULL);
            if (pendingClientSocket < 0)
            {
                LOG_ERROR("ParserProcess: Failed to accept connection: %s", strerror(errno));
            }
            else
            {
                LOG_INFO("ParserProcess: Accepted connection from Compute thread");
            }
        }

        // Check if we have new FIFO data to process
        if (FD_ISSET(proc->fifoFd, &readfds) && !pendingResult)
        {
            FetchResult fetchResult;
            size_t totalRead = 0;
            size_t expectedSize = sizeof(fetchResult);

            // Read entire struct in chunks (handles large structs > PIPE_BUF)
            while (totalRead < expectedSize)
            {
                ssize_t bytesRead = read(proc->fifoFd, ((char *)&fetchResult) + totalRead, expectedSize - totalRead);

                if (bytesRead == 0)
                {
                    LOG_INFO("ParserProcess: FIFO closed, exiting");
                    proc->isRunning = false;
                    break;
                }

                if (bytesRead < 0)
                {
                    LOG_ERROR("ParserProcess: Read error from FIFO: %s", strerror(errno));
                    break;
                }

                totalRead += bytesRead;
            }

            if (!proc->isRunning || totalRead != expectedSize)
            {
                if (totalRead != expectedSize)
                    LOG_ERROR("ParserProcess: Incomplete read from FIFO (%zu/%zu bytes)", totalRead, expectedSize);
                continue;
            }

            LOG_INFO("ParserProcess: Processing FetchResult for %s/%s", fetchResult.userId, fetchResult.region);

            // Parse JSON data from FetchResult
            OpenMeteoResponse omData = {0};
            ElprisetResponse elprisetData = {0};
            bool omParsed = false;
            bool pricesParsed = false;

            if (strlen(fetchResult.openMeteoJson) > 0)
            {
                if (APIParser_ParseOpenMeteo(parser, fetchResult.openMeteoJson, &omData) == 0)
                {
                    LOG_INFO("ParserProcess: Parsed %d Open-Meteo entries", omData.count);
                    omParsed = true;
                }
                else
                {
                    LOG_ERROR("ParserProcess: Open-Meteo parse failed");
                }
            }

            if (strlen(fetchResult.priceJson) > 0)
            {
                if (APIParser_ParseElpriset(parser, fetchResult.priceJson, &elprisetData) == 0)
                {
                    LOG_INFO("ParserProcess: Parsed %d price entries", elprisetData.count);
                    pricesParsed = true;
                }
                else
                {
                    LOG_ERROR("ParserProcess: Elpriset parse failed");
                }
            }

            if (!omParsed)
            {
                LOG_ERROR("ParserProcess: Cannot create forecast without Open-Meteo data");
                continue;
            }

            // Allocate and build ParseResult with data from FetchResult + parsed API responses
            pendingResult = calloc(1, sizeof(ParseResult));
            if (!pendingResult)
            {
                LOG_ERROR("ParserProcess: Failed to allocate ParseResult");
                continue;
            }

            strncpy(pendingResult->userId, fetchResult.userId, sizeof(pendingResult->userId) - 1);
            strncpy(pendingResult->location, fetchResult.location, sizeof(pendingResult->location) - 1);
            strncpy(pendingResult->region, fetchResult.region, sizeof(pendingResult->region) - 1);
            pendingResult->solarAreaM2 = fetchResult.solarAreaM2;
            pendingResult->solarEfficiency = fetchResult.solarEfficiency;
            pendingResult->consumptionKwh = fetchResult.consumptionKwh;
            pendingResult->gridFeeLow = fetchResult.gridFeeLow;
            pendingResult->gridFeeNormal = fetchResult.gridFeeNormal;
            pendingResult->gridFeeHigh = fetchResult.gridFeeHigh;

            if (pricesParsed)
            {
                BuildForecastData(&omData, &elprisetData, fetchResult.region, &pendingResult->forecastData);
            }
            else
            {
                // Build forecast without prices
                BuildForecastData(&omData, &elprisetData, fetchResult.region, &pendingResult->forecastData);
            }

            LOG_INFO("ParserProcess: Data ready for %s, notifying Compute", pendingResult->userId);

            // Send notification to Compute via notify FIFO
            char notifySignal = 1; // Single byte to signal data ready
            ssize_t written = write(proc->notifyFd, &notifySignal, 1);
            if (written < 0)
            {
                LOG_ERROR("ParserProcess: Failed to write notify signal: %s", strerror(errno));
            }
        }

        // If we have BOTH parsed data AND a client connection, send the data
        if (pendingResult && pendingClientSocket >= 0)
        {
            // Verify socket is still valid before writing
            int socketError = 0;
            socklen_t errorLen = sizeof(socketError);
            if (getsockopt(pendingClientSocket, SOL_SOCKET, SO_ERROR, &socketError, &errorLen) < 0 || socketError != 0)
            {
                LOG_ERROR("ParserProcess: Socket is in error state: %s", socketError ? strerror(socketError) : "getsockopt failed");
                close(pendingClientSocket);
                pendingClientSocket = -1;
                free(pendingResult);
                pendingResult = NULL;
                continue;
            }

            // Send ParseResult to Compute via socket (handle partial writes)
            size_t totalSize = sizeof(ParseResult);
            size_t totalWritten = 0;
            const char *buf = (const char *)pendingResult;

            while (totalWritten < totalSize)
            {
                ssize_t written = write(pendingClientSocket, buf + totalWritten, totalSize - totalWritten);
                if (written < 0)
                {
                    LOG_ERROR("ParserProcess: Failed to write ParseResult to socket: %s (written %zu/%zu bytes)", strerror(errno), totalWritten, totalSize);
                    break;
                }
                if (written == 0)
                {
                    LOG_WARNING("ParserProcess: Socket closed by peer after %zu/%zu bytes", totalWritten, totalSize);
                    break;
                }
                totalWritten += written;
            }

            if (totalWritten == totalSize)
            {
                LOG_INFO("ParserProcess: Sent ParseResult to Compute (%zu bytes)", totalWritten);
            }

            // Clean up - transaction complete
            close(pendingClientSocket);
            pendingClientSocket = -1;
            free(pendingResult);
            pendingResult = NULL;
        }
    }

    // Cleanup any pending state before exit
    if (pendingResult)
    {
        free(pendingResult);
    }
    if (pendingClientSocket >= 0)
    {
        close(pendingClientSocket);
    }

    LOG_INFO("ParserProcess: Main loop exited");
    return 0;
}

void ParserProcess_Shutdown(ParserProcess *proc)
{
    if (!proc)
        return;

    LOG_INFO("ParserProcess: Shutting down");

    proc->isRunning = false;

    if (proc->fifoFd >= 0)
        close(proc->fifoFd);

    if (proc->serverSocket >= 0)
        close(proc->serverSocket);

    if (proc->notifyFd >= 0)
        close(proc->notifyFd);

    unlink(proc->socketPath);
    unlink(proc->notifyPath);

    if (proc->parser)
    {
        APIParser_Shutdown((APIParser *)proc->parser);
        free(proc->parser);
    }

    LOG_INFO("ParserProcess: Shutdown complete");
}
