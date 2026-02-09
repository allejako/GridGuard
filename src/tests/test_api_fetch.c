#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "APIFetcher.h"
#include "APIEndpoints.h"
#include "Parser.h"
#include "DataStructures.h"
#include "OpenMeteoData.h"
#include "ElprisetData.h"
#include "PipelineData.h"
#include "Config.h"

void print_separator(const char *title)
{
    printf("\n========== %s ==========\n", title);
}

int test_spot_prices(APIFetcher *fetcher, Parser *parser, ElprisetResponse *elpriset)
{
    print_separator("SPOTPRISER (elprisetjustnu.se)");

    char url[256];
    if (BuildSpotPriceApiUrl(url, sizeof(url), SPOTPRICE_REGION, NULL) != 0)
    {
        printf("ERROR: Kunde inte bygga spotpris-URL\n");
        return -1;
    }

    printf("URL: %s\n", url);

    APIResponse response = {0};
    if (APIFetcher_Fetch(fetcher, url, &response) != 0)
    {
        printf("ERROR: Kunde inte hämta spotpriser\n");
        return -1;
    }

    printf("HTTP Status: %d\n", response.status);
    printf("Response storlek: %zu bytes\n", response.size);

    // Parse to raw API struct
    if (Parser_ParseElpriset(parser, response.data, elpriset) != 0)
    {
        printf("ERROR: Kunde inte parsa spotpriser (ElprisetResponse)\n");
        APIFetcher_FreeResponse(&response);
        return -1;
    }

    printf("Antal priser (raw): %d\n\n", elpriset->count);

    printf("%-26s %-12s %-12s\n", "time_start", "SEK/kWh", "EUR/kWh");
    printf("%-26s %-12s %-12s\n", "----------", "-------", "-------");

    int showCount = elpriset->count < 5 ? elpriset->count : 5;
    for (int i = 0; i < showCount; i++)
    {
        ElprisetEntry *e = &elpriset->entries[i];
        printf("%-26s %-12.4f %-12.4f\n", e->time_start, e->SEK_per_kWh, e->EUR_per_kWh);
    }

    APIFetcher_FreeResponse(&response);
    printf("\nSpotpriser OK!\n");
    return 0;
}

int test_weather(APIFetcher *fetcher, Parser *parser, OpenMeteoResponse *meteo)
{
    print_separator("VÄDERDATA (Open-Meteo)");

    char url[512];
    if (BuildWeatherApiUrl(url, sizeof(url), WEATHER_LAT, WEATHER_LON) != 0)
    {
        printf("ERROR: Kunde inte bygga väder-URL\n");
        return -1;
    }

    printf("URL: %s\n", url);

    APIResponse response = {0};
    if (APIFetcher_Fetch(fetcher, url, &response) != 0)
    {
        printf("ERROR: Kunde inte hämta väderdata\n");
        return -1;
    }

    printf("HTTP Status: %d\n", response.status);
    printf("Response storlek: %zu bytes\n", response.size);

    // Parse to raw API struct
    if (Parser_ParseOpenMeteo(parser, response.data, meteo) != 0)
    {
        printf("ERROR: Kunde inte parsa väderdata (OpenMeteoResponse)\n");
        APIFetcher_FreeResponse(&response);
        return -1;
    }

    printf("Antal prognoser (raw): %d\n\n", meteo->count);

    printf("%-20s %-8s %-8s %-12s\n", "Tid", "Temp", "Moln%", "Sol W/m²");
    printf("%-20s %-8s %-8s %-12s\n", "---", "----", "-----", "--------");

    int showCount = meteo->count < 5 ? meteo->count : 5;
    for (int i = 0; i < showCount; i++)
    {
        OpenMeteoEntry *e = &meteo->entries[i];
        printf("%-20s %-8.1f %-8.0f %-12.0f\n",
               e->time, e->temperature_2m, e->cloud_cover, e->shortwave_radiation);
    }

    APIFetcher_FreeResponse(&response);
    printf("\nVäderdata OK!\n");
    return 0;
}

int test_pipeline(const OpenMeteoResponse *meteo, const ElprisetResponse *elpriset)
{
    print_separator("PIPELINE DATA");

    PipelineData pipeline = {0};
    if (Parser_BuildPipelineData(meteo, elpriset, &pipeline) != 0)
    {
        printf("ERROR: Kunde inte bygga PipelineData\n");
        return -1;
    }

    printf("Antal pipeline-entries: %d\n\n", pipeline.count);

    printf("%-20s %-8s %-8s %-10s %-10s\n", "Tid", "Temp", "Moln%", "Sol W/m²", "SEK/kWh");
    printf("%-20s %-8s %-8s %-10s %-10s\n", "---", "----", "-----", "--------", "-------");

    int showCount = pipeline.count < 5 ? pipeline.count : 5;
    for (int i = 0; i < showCount; i++)
    {
        PipelineEntry *e = &pipeline.entries[i];
        char timeStr[32];
        struct tm *tm = localtime(&e->timestamp);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", tm);
        printf("%-20s %-8.1f %-8.0f %-10.0f %-10.4f\n",
               timeStr, e->temperature, e->cloudCover, e->solarIrradiance, e->spotPriceSek);
    }

    printf("\nPipeline OK!\n");
    return 0;
}

int main(void)
{
    printf("GridGuard API Test\n");
    printf("==================\n");

    // Initiera komponenter
    APIFetcher fetcher = {0};
    if (APIFetcher_Initiate(&fetcher) != 0)
    {
        printf("ERROR: Kunde inte initiera APIFetcher\n");
        return 1;
    }

    Parser parser = {0};
    if (Parser_Initiate(&parser) != 0)
    {
        printf("ERROR: Kunde inte initiera Parser\n");
        APIFetcher_Shutdown(&fetcher);
        return 1;
    }

    // Raw API structs
    OpenMeteoResponse meteo = {0};
    ElprisetResponse elpriset = {0};

    // Kör tester
    int spotResult = test_spot_prices(&fetcher, &parser, &elpriset);
    int weatherResult = test_weather(&fetcher, &parser, &meteo);

    // Test pipeline combination
    int pipelineResult = -1;
    if (spotResult == 0 && weatherResult == 0)
        pipelineResult = test_pipeline(&meteo, &elpriset);

    // Cleanup
    Parser_Shutdown(&parser);
    APIFetcher_Shutdown(&fetcher);

    // Sammanfattning
    print_separator("RESULTAT");
    printf("Spotpriser:  %s\n", spotResult == 0 ? "OK" : "FAILED");
    printf("Väderdata:   %s\n", weatherResult == 0 ? "OK" : "FAILED");
    printf("Pipeline:    %s\n", pipelineResult == 0 ? "OK" : "FAILED");

    return (spotResult == 0 && weatherResult == 0 && pipelineResult == 0) ? 0 : 1;
}
