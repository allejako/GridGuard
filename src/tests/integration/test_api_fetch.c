#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "Fetcher.h"
#include "APIEndpoints.h"
#include "Parser.h"
#include "OpenMeteoData.h"
#include "ElprisetData.h"
#include "Config.h"

void print_separator(const char *title)
{
    printf("\n========== %s ==========\n", title);
}

int test_spot_prices(Fetcher *fetcher, Parser *parser)
{
    print_separator("SPOTPRISER (elpriset.se)");

    char url[256];
    if (BuildSpotPriceApiUrl(url, sizeof(url), SPOTPRICE_REGION, NULL) != 0)
    {
        printf("ERROR: Kunde inte bygga spotpris-URL\n");
        return -1;
    }

    printf("URL: %s\n", url);

    FetchResponse response = {0};
    if (Fetcher_Fetch(fetcher, url, &response) != 0)
    {
        printf("ERROR: Kunde inte hämta spotpriser\n");
        return -1;
    }

    printf("HTTP Status: %d\n", response.status);
    printf("Response storlek: %zu bytes\n", strlen(response.data));

    // Parsa JSON
    ElprisetResponse spotData = {0};
    if (Parser_ParseElpriset(parser, response.data, &spotData) != 0)
    {
        printf("ERROR: Kunde inte parsa spotpriser\n");
        Fetcher_FreeResponse(&response);
        return -1;
    }

    printf("Antal priser: %d\n\n", spotData.count);

    // Visa de första 5 priserna
    printf("%-20s %-12s\n", "Tid", "SEK/kWh");
    printf("%-20s %-12s\n", "---", "-------");

    int showCount = spotData.count < 5 ? spotData.count : 5;
    for (int i = 0; i < showCount; i++)
    {
        ElprisetEntry *p = &spotData.entries[i];
        // Visa bara första 16 tecken av tidsstämpeln (utan timezone)
        char timeStr[17];
        strncpy(timeStr, p->time_start, 16);
        timeStr[16] = '\0';
        printf("%-20s %-12.4f\n", timeStr, p->SEK_per_kWh);
    }

    Fetcher_FreeResponse(&response);
    printf("\nSpotpriser OK!\n");
    return 0;
}

int test_weather(Fetcher *fetcher, Parser *parser)
{
    print_separator("VÄDERDATA (Open-Meteo)");

    char url[512];
    if (BuildWeatherApiUrl(url, sizeof(url), WEATHER_LAT, WEATHER_LON) != 0)
    {
        printf("ERROR: Kunde inte bygga väder-URL\n");
        return -1;
    }

    printf("URL: %s\n", url);

    FetchResponse response = {0};
    if (Fetcher_Fetch(fetcher, url, &response) != 0)
    {
        printf("ERROR: Kunde inte hämta väderdata\n");
        return -1;
    }

    printf("HTTP Status: %d\n", response.status);
    printf("Response storlek: %zu bytes\n", strlen(response.data));

    // Parsa JSON
    OpenMeteoResponse forecast = {0};
    if (Parser_ParseOpenMeteo(parser, response.data, &forecast) != 0)
    {
        printf("ERROR: Kunde inte parsa väderdata\n");
        Fetcher_FreeResponse(&response);
        return -1;
    }

    printf("Antal prognoser: %d\n\n", forecast.count);

    // Visa de första 5 prognoserna
    printf("%-20s %-8s %-8s %-12s\n", "Tid", "Temp", "Moln%", "Sol W/m²");
    printf("%-20s %-8s %-8s %-12s\n", "---", "----", "-----", "--------");

    int showCount = forecast.count < 5 ? forecast.count : 5;
    for (int i = 0; i < showCount; i++)
    {
        OpenMeteoEntry *w = &forecast.entries[i];
        printf("%-20s %-8.1f %-8.0f %-12.0f\n",
               w->time, w->temperature_2m, w->cloud_cover, w->shortwave_radiation);
    }

    Fetcher_FreeResponse(&response);
    printf("\nVäderdata OK!\n");
    return 0;
}

int main(void)
{
    printf("GridGuard API Test\n");
    printf("==================\n");

    // Initiera komponenter
    Fetcher fetcher = {0};
    if (Fetcher_Initiate(&fetcher) != 0)
    {
        printf("ERROR: Kunde inte initiera Fetcher\n");
        return 1;
    }

    Parser parser = {0};
    if (Parser_Initiate(&parser) != 0)
    {
        printf("ERROR: Kunde inte initiera Parser\n");
        Fetcher_Shutdown(&fetcher);
        return 1;
    }

    // Kör tester
    int spotResult = test_spot_prices(&fetcher, &parser);
    int weatherResult = test_weather(&fetcher, &parser);

    // Cleanup
    Parser_Shutdown(&parser);
    Fetcher_Shutdown(&fetcher);

    // Sammanfattning
    print_separator("RESULTAT");
    printf("Spotpriser:  %s\n", spotResult == 0 ? "OK" : "FAILED");
    printf("Väderdata:   %s\n", weatherResult == 0 ? "OK" : "FAILED");

    printf("\nAll tests passed!\n");
    return (spotResult == 0 && weatherResult == 0) ? 0 : 1;
}
