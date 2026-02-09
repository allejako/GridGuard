#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "APIFetcher.h"
#include "APIEndpoints.h"
#include "Parser.h"
#include "DataStructures.h"
#include "Config.h"

void print_separator(const char *title)
{
    printf("\n========== %s ==========\n", title);
}

int test_spot_prices(APIFetcher *fetcher, Parser *parser)
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

    // Parsa JSON
    SpotPriceData spotData = {0};
    if (Parser_ParseSpotPrices(parser, response.data, &spotData) != 0)
    {
        printf("ERROR: Kunde inte parsa spotpriser\n");
        APIFetcher_FreeResponse(&response);
        return -1;
    }

    printf("Antal priser: %d\n\n", spotData.count);

    // Visa de första 5 priserna
    printf("%-20s %-12s %-10s\n", "Tid", "SEK/kWh", "Region");
    printf("%-20s %-12s %-10s\n", "---", "-------", "------");

    int showCount = spotData.count < 5 ? spotData.count : 5;
    for (int i = 0; i < showCount; i++)
    {
        SpotPrice *p = &spotData.prices[i];
        char timeStr[32];
        struct tm *tm = localtime(&p->timestamp);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", tm);
        printf("%-20s %-12.4f %-10s\n", timeStr, p->priceSekPerKwh, p->region);
    }

    APIFetcher_FreeResponse(&response);
    printf("\nSpotpriser OK!\n");
    return 0;
}

int test_weather(APIFetcher *fetcher, Parser *parser)
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

    // Parsa JSON
    WeatherForecast forecast = {0};
    if (Parser_ParseWeatherData(parser, response.data, &forecast) != 0)
    {
        printf("ERROR: Kunde inte parsa väderdata\n");
        APIFetcher_FreeResponse(&response);
        return -1;
    }

    printf("Antal prognoser: %d\n\n", forecast.count);

    // Visa de första 5 prognoserna
    printf("%-20s %-8s %-8s %-12s\n", "Tid", "Temp", "Moln%", "Sol W/m²");
    printf("%-20s %-8s %-8s %-12s\n", "---", "----", "-----", "--------");

    int showCount = forecast.count < 5 ? forecast.count : 5;
    for (int i = 0; i < showCount; i++)
    {
        WeatherData *w = &forecast.forecasts[i];
        char timeStr[32];
        struct tm *tm = localtime(&w->timestamp);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", tm);
        printf("%-20s %-8.1f %-8.0f %-12.0f\n",
               timeStr, w->temperature, w->cloudCover, w->solarIrradiance);
    }

    APIFetcher_FreeResponse(&response);
    printf("\nVäderdata OK!\n");
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

    // Kör tester
    int spotResult = test_spot_prices(&fetcher, &parser);
    int weatherResult = test_weather(&fetcher, &parser);

    // Cleanup
    Parser_Shutdown(&parser);
    APIFetcher_Shutdown(&fetcher);

    // Sammanfattning
    print_separator("RESULTAT");
    printf("Spotpriser:  %s\n", spotResult == 0 ? "OK" : "FAILED");
    printf("Väderdata:   %s\n", weatherResult == 0 ? "OK" : "FAILED");

    return (spotResult == 0 && weatherResult == 0) ? 0 : 1;
}
