/*
 * Integration Test: Open-Meteo weather + Elpriset spot price
 *
 * Verifies:
 *  1. Open-Meteo URL building
 *  2. Open-Meteo fetch and JSON parsing → OpenMeteoResponse
 *  3. Elpriset URL building (today and tomorrow)
 *  4. Elpriset fetch and JSON parsing → ElprisetResponse
 *
 * Adding a new weather or spot price source? Follow the same pattern:
 *  - Build URL with Build<Source>ApiUrl()
 *  - Fetch with HTTPHTTPFetcher_Fetch()
 *  - Parse with APIParser_Parse<Source>()
 *  - Add a corresponding test block here
 */

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

#define RESET  "\033[0m"
#define RED    "\033[31m"
#define GREEN  "\033[32m"
#define CYAN   "\033[36m"

static void print_header(const char *title)
{
    printf("\n" CYAN "========================================\n");
    printf("%s\n", title);
    printf("========================================" RESET "\n\n");
}

static void print_result(const char *name, int passed)
{
    printf(passed ? GREEN "✓ PASS" RESET " - %s\n"
                  : RED   "✗ FAIL" RESET " - %s\n", name);
}

// ── TEST 1: Open-Meteo ────────────────────────────────────────────────────────

static int test_openmeteo(HTTPFetcher *fetcher, APIParser *parser)
{
    print_header("TEST 1: Open-Meteo fetch & parse");

    char url[512];
    if (BuildOpenMeteoApiUrl(url, sizeof(url), WEATHER_LAT, WEATHER_LON) != 0) {
        printf(RED "ERROR: BuildOpenMeteoApiUrl failed\n" RESET);
        return -1;
    }
    printf("URL: %s\n\n", url);

    HTTPFetchResponse resp = {0};
    if (HTTPFetcher_Fetch(fetcher, url, &resp) != 0) {
        printf(RED "ERROR: Fetch failed\n" RESET);
        return -1;
    }
    printf("Status: %d  Size: %zu bytes\n", resp.status, strlen(resp.data));
    print_result("HTTP 200", resp.status == 200);

    OpenMeteoResponse om = {0};
    int parseOk = (APIParser_ParseOpenMeteo(parser, resp.data, &om) == 0);
    HTTPFetcher_FreeResponse(&resp);
    print_result("Parse succeeded", parseOk);
    if (!parseOk) return -1;

    printf("Entries parsed: %d\n", om.count);
    print_result("At least 24 hourly entries", om.count >= 24);

    if (om.count > 0) {
        const OpenMeteoEntry *e = &om.entries[0];
        printf("\nFirst entry (%s):\n", e->time);
        printf("  Temperature:        %.1f °C\n",  e->temperature_2m);
        printf("  Humidity:           %.0f %%\n",   e->humidity_2m);
        printf("  Cloud cover:        %.0f %%\n",   e->cloud_cover);
        printf("  Wind speed:         %.1f m/s\n",  e->wind_speed_10m);
        printf("  Solar irradiance:   %.1f W/m²\n", e->shortwave_radiation);

        int hasAllFields = (e->time[0] != '\0');
        print_result("First entry has timestamp", hasAllFields);
    }

    return 0;
}

// ── TEST 2: Elpriset spot prices ──────────────────────────────────────────────

static int test_elpriset(HTTPFetcher *fetcher, APIParser *parser)
{
    print_header("TEST 2: Elpriset fetch & parse");

    // Today
    char urlToday[256];
    if (BuildSpotPriceApiUrl(urlToday, sizeof(urlToday), SPOTPRICE_REGION, NULL) != 0) {
        printf(RED "ERROR: BuildSpotPriceApiUrl (today) failed\n" RESET);
        return -1;
    }
    printf("Today URL:    %s\n", urlToday);

    // Tomorrow
    char urlTomorrow[256];
    BuildSpotPriceTomorrowUrl(urlTomorrow, sizeof(urlTomorrow), SPOTPRICE_REGION);
    printf("Tomorrow URL: %s\n\n", urlTomorrow);

    HTTPFetchResponse resp = {0};
    if (HTTPFetcher_Fetch(fetcher, urlToday, &resp) != 0) {
        printf(RED "ERROR: Fetch failed\n" RESET);
        return -1;
    }
    printf("Status: %d  Size: %zu bytes\n", resp.status, strlen(resp.data));
    print_result("HTTP 200", resp.status == 200);

    ElprisetResponse prices = {0};
    int parseOk = (APIParser_ParseElpriset(parser, resp.data, &prices) == 0);
    HTTPFetcher_FreeResponse(&resp);
    print_result("Parse succeeded", parseOk);
    if (!parseOk) return -1;

    printf("Prices parsed: %d\n", prices.count);
    print_result("At least 24 price entries", prices.count >= 24);

    if (prices.count > 0) {
        const ElprisetEntry *e = &prices.entries[0];
        printf("\nFirst entry:\n");
        printf("  From: %s\n",       e->time_start);
        printf("  To:   %s\n",       e->time_end);
        printf("  SEK:  %.4f kr/kWh\n", e->SEK_per_kWh);
        printf("  EUR:  %.4f €/kWh\n",  e->EUR_per_kWh);
        print_result("Price is non-negative", e->SEK_per_kWh >= 0.0);
    }

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────

int main(void)
{
    printf(CYAN "\nGridGuard — Weather & Spot Price Parser Test\n" RESET);
    printf("Region: %s  Location: %s, %s\n", SPOTPRICE_REGION, WEATHER_LAT, WEATHER_LON);

    HTTPFetcher fetcher;
    if (HTTPFetcher_Initiate(&fetcher) != 0) {
        fprintf(stderr, "ERROR: HTTPFetcher_Initiate failed\n");
        return 1;
    }

    APIParser parser;
    if (APIParser_Initiate(&parser) != 0) {
        fprintf(stderr, "ERROR: APIParser_Initiate failed\n");
        HTTPFetcher_Shutdown(&fetcher);
        return 1;
    }

    int failures = 0;
    if (test_openmeteo(&fetcher, &parser) != 0) failures++;
    if (test_elpriset(&fetcher, &parser)  != 0) failures++;

    APIParser_Shutdown(&parser);
    HTTPFetcher_Shutdown(&fetcher);

    printf("\n");
    if (failures == 0)
        printf(GREEN "All tests passed.\n" RESET);
    else
        printf(RED "%d test(s) failed.\n" RESET, failures);

    return failures > 0 ? 1 : 0;
}
