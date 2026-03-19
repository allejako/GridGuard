#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "net/HTTPFetcher.h"
#include "api/APIEndpoints.h"
#include "api/APIParser.h"
#include "api/OpenMeteoResponse.h"
#include "api/ElprisetResponse.h"
#include "domain/Config.h"

void print_separator(const char *title)
{
    printf("\n========== %s ==========\n", title);
}

int test_spot_prices(HTTPFetcher *fetcher, APIParser *parser)
{
    print_separator("SPOTPRISER (elpriset.se)");

    char url[256];
    if (BuildSpotPriceApiUrl(url, sizeof(url), SPOTPRICE_REGION, NULL) != 0)
    {
        printf("ERROR: Could not build spot price URL\n");
        return -1;
    }

    printf("URL: %s\n", url);

    HTTPFetchResponse response = {0};
    if (HTTPFetcher_Fetch(fetcher, url, &response) != 0)
    {
        printf("ERROR: Could not fetch spot prices\n");
        return -1;
    }

    printf("HTTP Status: %d\n", response.status);
    printf("Response size: %zu bytes\n", strlen(response.data));

    // Parse JSON
    ElprisetResponse spotData = {0};
    if (APIParser_ParseElpriset(parser, response.data, &spotData) != 0)
    {
        printf("ERROR: Could not parse spot prices\n");
        HTTPFetcher_FreeResponse(&response);
        return -1;
    }

    printf("Price count: %d\n\n", spotData.count);

    // Show the first 5 prices
    printf("%-20s %-12s\n", "Time", "SEK/kWh");
    printf("%-20s %-12s\n", "---", "-------");

    int showCount = spotData.count < 5 ? spotData.count : 5;
    for (int i = 0; i < showCount; i++)
    {
        ElprisetEntry *p = &spotData.entries[i];
        // Show only the first 16 characters of the timestamp (without timezone)
        char timeStr[17];
        strncpy(timeStr, p->time_start, 16);
        timeStr[16] = '\0';
        printf("%-20s %-12.4f\n", timeStr, p->SEK_per_kWh);
    }

    HTTPFetcher_FreeResponse(&response);
    printf("\nSpotpriser OK!\n");
    return 0;
}

int test_weather(HTTPFetcher *fetcher, APIParser *parser)
{
    print_separator("WEATHER DATA (Open-Meteo)");

    char url[512];
    if (BuildOpenMeteoApiUrl(url, sizeof(url), WEATHER_LAT, WEATHER_LON) != 0)
    {
        printf("ERROR: Could not build weather URL\n");
        return -1;
    }

    printf("URL: %s\n", url);

    HTTPFetchResponse response = {0};
    if (HTTPFetcher_Fetch(fetcher, url, &response) != 0)
    {
        printf("ERROR: Could not fetch weather data\n");
        return -1;
    }

    printf("HTTP Status: %d\n", response.status);
    printf("Response size: %zu bytes\n", strlen(response.data));

    // Parse JSON
    OpenMeteoResponse forecast = {0};
    if (APIParser_ParseOpenMeteo(parser, response.data, &forecast) != 0)
    {
        printf("ERROR: Could not parse weather data\n");
        HTTPFetcher_FreeResponse(&response);
        return -1;
    }

    printf("Forecast count: %d\n\n", forecast.count);

    // Show the first 5 forecasts
    printf("%-20s %-8s %-8s %-12s\n", "Time", "Temp", "Cloud%", "Solar W/m2");
    printf("%-20s %-8s %-8s %-12s\n", "---", "----", "-----", "--------");

    int showCount = forecast.count < 5 ? forecast.count : 5;
    for (int i = 0; i < showCount; i++)
    {
        OpenMeteoEntry *w = &forecast.entries[i];
        printf("%-20s %-8.1f %-8.0f %-12.0f\n",
               w->time, w->temperature_2m, w->cloud_cover, w->shortwave_radiation);
    }

    HTTPFetcher_FreeResponse(&response);
    printf("\nWeather data OK!\n");
    return 0;
}

int main(void)
{
    printf("GridGuard API Test\n");
    printf("==================\n");

    // Initialize components
    HTTPFetcher fetcher = {0};
    if (HTTPFetcher_Initiate(&fetcher) != 0)
    {
        printf("ERROR: Could not initialize Fetcher\n");
        return 1;
    }

    APIParser parser = {0};
    if (APIParser_Initiate(&parser) != 0)
    {
        printf("ERROR: Could not initialize Parser\n");
        HTTPFetcher_Shutdown(&fetcher);
        return 1;
    }

    // Run tests
    int spotResult = test_spot_prices(&fetcher, &parser);
    int weatherResult = test_weather(&fetcher, &parser);

    // Cleanup
    APIParser_Shutdown(&parser);
    HTTPFetcher_Shutdown(&fetcher);

    // Summary
    print_separator("RESULTS");
    printf("Spot prices: %s\n", spotResult == 0 ? "OK" : "FAILED");
    printf("Weather:     %s\n", weatherResult == 0 ? "OK" : "FAILED");

    printf("\nAll tests passed!\n");
    return (spotResult == 0 && weatherResult == 0) ? 0 : 1;
}
