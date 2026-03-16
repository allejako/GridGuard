#define _POSIX_C_SOURCE 200809L

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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <math.h>
#include <errno.h>

// Helper: parse ISO 8601 timestamp till time_t (hanterar tidszoner)
static time_t parse_iso8601(const char *timeStr)
{
    struct tm tm = {0};
    int tzHour = 0, tzMin = 0;
    char tzSign = '+';

    // Försök parsa med tidszon först (t.ex. "2026-03-16T00:00:00+01:00")
    int parsed = sscanf(timeStr, "%d-%d-%dT%d:%d:%d%c%d:%d",
                       &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                       &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
                       &tzSign, &tzHour, &tzMin);

    if (parsed < 6) {
        // Fallback: parsa utan tidszon (t.ex. "2026-03-16T13:20:00")
        sscanf(timeStr, "%d-%d-%dT%d:%d:%d",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    }

    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;

    // Använd mktime för lokal tid och justera för tidszon om angiven
    time_t result = mktime(&tm);

    // Justera för tidszon om den parsades
    if (parsed >= 7) {
        int tzOffset = (tzHour * 3600 + tzMin * 60);
        if (tzSign == '-') {
            tzOffset = -tzOffset;
        }
        // Subtrahera tidszonoffset för att få UTC, sedan mktime konverterar till lokal tid
        result -= tzOffset;
    }

    return result;
}

// Bygg forecast data genom att matcha OpenMeteoResponse med ElprisetResponse baserat på timestamp
static void build_forecast_data(const OpenMeteoResponse *om, const ElprisetResponse *elpriset, const char *region __attribute__((unused)), ForecastData *forecast)
{
    memset(forecast, 0, sizeof(ForecastData));
    forecast->lastUpdated = time(NULL);

    LOG_INFO("ParserProcess: build_forecast_data() called with om->count=%d, elpriset->count=%d", om->count, elpriset->count);

    int count = 0;
    for (int i = 0; i < om->count && count < 96; i++)
    {
        const OpenMeteoEntry *src = &om->entries[i];
        ForecastEntry *entry = &forecast->entries[count];

        entry->timestamp = parse_iso8601(src->time);
        entry->temperature = src->temperature_2m;
        entry->humidity = src->humidity_2m;
        entry->cloudCover = src->cloud_cover;
        entry->windSpeed = src->wind_speed_10m;
        entry->solarIrradiance = src->shortwave_radiation;

        // Matcha elpris baserat på tid-på-dagen (time-of-day matching)
        // Dagens priser är 00:00-23:45, vädret börjar från "nu"
        // Matchning: extrahera timme+minut från både väder och pris
        entry->spotPriceSek = 0.0;

        struct tm weatherTime;
        localtime_r(&entry->timestamp, &weatherTime);
        int weatherMinuteOfDay = weatherTime.tm_hour * 60 + weatherTime.tm_min;

        for (int j = 0; j < elpriset->count; j++)
        {
            const ElprisetEntry *price = &elpriset->entries[j];
            time_t priceTime = parse_iso8601(price->time_start);
            struct tm priceTimeStruct;
            localtime_r(&priceTime, &priceTimeStruct);
            int priceMinuteOfDay = priceTimeStruct.tm_hour * 60 + priceTimeStruct.tm_min;

            // Matcha om tid-på-dagen är inom 15 minuter
            if (abs(weatherMinuteOfDay - priceMinuteOfDay) < 15)
            {
                entry->spotPriceSek = price->SEK_per_kWh;
                break;
            }
        }

        if (entry->spotPriceSek == 0.0 && i < 3)
        {
            LOG_WARNING("ParserProcess: No price match for %02d:%02d (minute %d)",
                       weatherTime.tm_hour, weatherTime.tm_min, weatherMinuteOfDay);
        }

        entry->valid = true;
        count++;
    }

    forecast->count = count;
    LOG_INFO("ParserProcess: Built forecast with %d quarter-hour entries (%.1f hours)", count, count / 4.0);
}

int ParserProcess_Initiate(ParserProcess *proc, const char *fifoPath, const char *socketPath, const char *notifyPath)
{
    if (!proc || !fifoPath || !socketPath || !notifyPath)
        return -1;

    memset(proc, 0, sizeof(ParserProcess));
    strncpy(proc->fifoPath, fifoPath, sizeof(proc->fifoPath) - 1);
    strncpy(proc->socketPath, socketPath, sizeof(proc->socketPath) - 1);
    strncpy(proc->notifyPath, notifyPath, sizeof(proc->notifyPath) - 1);

    // Allokera APIParser service
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

    // Öppna FIFO för läsning, blockerar tills fetch-processen öppnar write end
    proc->fifoFd = open(fifoPath, O_RDONLY);
    if (proc->fifoFd < 0)
    {
        LOG_ERROR("ParserProcess: Failed to open FIFO %s for reading", fifoPath);
        APIParser_Shutdown((APIParser *)proc->parser);
        free(proc->parser);
        return -1;
    }

    // Skapa Unix domain socket server för att kommunicera med Compute-tråden
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

    unlink(socketPath); // Ta bort gammal socket om den finns

    if (bind(proc->serverSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
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

    // Skapa notify FIFO för att signalera Compute när data är redo
    unlink(notifyPath); // Ta bort gammal FIFO om den finns
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

    // Öppna notify FIFO för skrivning (will block until ComputeWorker opens read end)
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
            ssize_t bytesRead = read(proc->fifoFd, &fetchResult, sizeof(fetchResult));

            if (bytesRead == 0)
            {
                LOG_INFO("ParserProcess: FIFO closed, exiting");
                break;
            }

            if (bytesRead != sizeof(fetchResult))
            {
                LOG_ERROR("ParserProcess: Partial read from FIFO (%zd bytes)", bytesRead);
                continue;
            }

            LOG_INFO("ParserProcess: Processing FetchResult for %s/%s", fetchResult.userId, fetchResult.region);
            LOG_INFO("ParserProcess: priceJson length=%zu, preview: %.200s", strlen(fetchResult.priceJson), fetchResult.priceJson);

            // Parsa JSON data från FetchResult
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
            pendingResult->gridFee_low = fetchResult.gridFee_low;
            pendingResult->gridFee_normal = fetchResult.gridFee_normal;
            pendingResult->gridFee_high = fetchResult.gridFee_high;

            if (pricesParsed)
            {
                build_forecast_data(&omData, &elprisetData, fetchResult.region, &pendingResult->forecastData);
            }
            else
            {
                // Build forecast without prices
                build_forecast_data(&omData, &elprisetData, fetchResult.region, &pendingResult->forecastData);
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
            int socket_error = 0;
            socklen_t error_len = sizeof(socket_error);
            if (getsockopt(pendingClientSocket, SOL_SOCKET, SO_ERROR, &socket_error, &error_len) < 0 || socket_error != 0)
            {
                LOG_ERROR("ParserProcess: Socket is in error state: %s", socket_error ? strerror(socket_error) : "getsockopt failed");
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
