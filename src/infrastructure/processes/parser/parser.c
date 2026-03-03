#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "APIParser.h"
#include "Forecast.h"
#include "OpenMeteoResponse.h"
#include "ElprisetResponse.h"
#include "Logger.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <math.h>

typedef struct
{
    char userId[64];
    char location[64];
    char region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    double gridFee_low;
    double gridFee_normal;
    double gridFee_high;
    char openMeteoJson[32768];
    char priceJson[16384];
} FetchResult;

typedef struct
{
    char userId[64];
    char location[64];
    char region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    double gridFee_low;
    double gridFee_normal;
    double gridFee_high;
    ForecastData forecastData;
} ParseResult;

// Helper: parse ISO 8601 timestamp till time_t
static time_t parse_iso8601(const char *timeStr)
{
    struct tm tm = {0};
    sscanf(timeStr, "%d-%d-%dT%d:%d:%d",
           &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
           &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return mktime(&tm);
}

// Bygg forecast data genom att matcha OpenMeteoResponse med ElprisetResponse baserat på timestamp
static void build_forecast_data(const OpenMeteoResponse *om, const ElprisetResponse *elpriset, const char *region __attribute__((unused)), ForecastData *forecast)
{
    memset(forecast, 0, sizeof(ForecastData));
    forecast->lastUpdated = time(NULL);

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

        // Matcha spot price baserat på timestamp
        entry->spotPriceSek = 0.0;
        for (int j = 0; j < elpriset->count; j++)
        {
            const ElprisetEntry *price = &elpriset->entries[j];
            time_t priceTime = parse_iso8601(price->time_start);

            if (abs((int)difftime(entry->timestamp, priceTime)) < 60)
            {
                entry->spotPriceSek = price->SEK_per_kWh;
                break;
            }
        }

        entry->valid = true;
        count++;
    }

    forecast->count = count;
    LOG_INFO("ParserProcess: Built forecast with %d entries", count);
}

int ParserProcess_Initiate(ParserProcess *proc, const char *fifoPath, const char *socketPath)
{
    if (!proc || !fifoPath || !socketPath)
        return -1;

    memset(proc, 0, sizeof(ParserProcess));
    strncpy(proc->fifoPath, fifoPath, sizeof(proc->fifoPath) - 1);
    strncpy(proc->socketPath, socketPath, sizeof(proc->socketPath) - 1);

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

    proc->isRunning = true;
    LOG_INFO("ParserProcess: Initialized (PID %d, FIFO %s, Socket %s)", getpid(), fifoPath, socketPath);
    return 0;
}

int ParserProcess_Run(ParserProcess *proc)
{
    if (!proc || !proc->isRunning)
        return -1;

    APIParser *parser = (APIParser *)proc->parser;
    LOG_INFO("ParserProcess: Starting main loop");

    while (proc->isRunning)
    {
        FetchResult fetchResult;

        // Läs FetchResult från FIFO 
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

        // Bygg ParseResult med data från FetchResult + parsade API-responser
        ParseResult parseResult = {0};
        strncpy(parseResult.userId, fetchResult.userId, sizeof(parseResult.userId) - 1);
        strncpy(parseResult.location, fetchResult.location, sizeof(parseResult.location) - 1);
        strncpy(parseResult.region, fetchResult.region, sizeof(parseResult.region) - 1);
        parseResult.solarAreaM2 = fetchResult.solarAreaM2;
        parseResult.solarEfficiency = fetchResult.solarEfficiency;
        parseResult.consumptionKwh = fetchResult.consumptionKwh;
        parseResult.gridFee_low = fetchResult.gridFee_low;
        parseResult.gridFee_normal = fetchResult.gridFee_normal;
        parseResult.gridFee_high = fetchResult.gridFee_high;

        if (pricesParsed)
        {
            build_forecast_data(&omData, &elprisetData, fetchResult.region, &parseResult.forecastData);
        }
        else
        {
            // Bygg forecast utan prices
            build_forecast_data(&omData, &elprisetData, fetchResult.region, &parseResult.forecastData);
        }

        // Vänta på connection från Compute-tråd
        int clientSocket = accept(proc->serverSocket, NULL, NULL);
        if (clientSocket < 0)
        {
            LOG_ERROR("ParserProcess: Failed to accept connection");
            continue;
        }

        LOG_INFO("ParserProcess: Accepted connection from Compute thread");

        // Skicka ParseResult till Compute via socket 
        ssize_t written = write(clientSocket, &parseResult, sizeof(parseResult));
        if (written != sizeof(parseResult))
        {
            LOG_ERROR("ParserProcess: Failed to write ParseResult to socket (%zd bytes)", written);
        }
        else
        {
            LOG_INFO("ParserProcess: Sent ParseResult to Compute (%zd bytes)", written);
        }

        close(clientSocket);
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

    unlink(proc->socketPath);

    if (proc->parser)
    {
        APIParser_Shutdown((APIParser *)proc->parser);
        free(proc->parser);
    }

    LOG_INFO("ParserProcess: Shutdown complete");
}
