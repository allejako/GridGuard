/*
 * Integration Test: Multi-Source Weather System
 *
 * Tests the complete flow of fetching weather data from both SMHI and Open-Meteo,
 * parsing the responses, and combining them into WeatherData.
 *
 * This test verifies:
 * 1. SMHI API fetch and parsing
 * 2. Open-Meteo API fetch and parsing
 * 3. Timestamp matching between sources
 * 4. WeatherData combination logic
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "Fetcher.h"
#include "APIEndpoints.h"
#include "Parser.h"
#include "SMHIResponse.h"
#include "OpenMeteoResponse.h"
#include "Weather.h"
#include "Config.h"

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define CYAN    "\033[36m"

void print_header(const char *title)
{
    printf("\n" CYAN "========================================\n");
    printf("%s\n", title);
    printf("========================================" RESET "\n\n");
}

void print_test_result(const char *test_name, int passed)
{
    if (passed) {
        printf(GREEN "✓ PASS" RESET " - %s\n", test_name);
    } else {
        printf(RED "✗ FAIL" RESET " - %s\n", test_name);
    }
}

// Helper function to parse ISO 8601 timestamp
static time_t ParseISO8601(const char *timeStr)
{
    struct tm tm = {0};
    sscanf(timeStr, "%d-%d-%dT%d:%d:%d",
           &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
           &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return mktime(&tm);
}

// Helper to find SMHI parameter by name
static const SMHIParameter* FindSMHIParameter(const SMHITimeEntry *entry, const char *name)
{
    for (int i = 0; i < entry->paramCount; i++) {
        if (strcmp(entry->parameters[i].name, name) == 0) {
            return &entry->parameters[i];
        }
    }
    return NULL;
}

int test_smhi_fetch_and_parse(Fetcher *fetcher, Parser *parser, SMHIResponse *smhiData)
{
    print_header("TEST 1: SMHI Fetch & Parse");

    char url[512];
    if (BuildSMHIApiUrl(url, sizeof(url), WEATHER_LAT, WEATHER_LON) != 0) {
        printf(RED "ERROR: Failed to build SMHI URL\n" RESET);
        return -1;
    }

    printf("URL: %s\n", url);

    FetchResponse response = {0};
    if (Fetcher_Fetch(fetcher, url, &response) != 0) {
        printf(RED "ERROR: Failed to fetch from SMHI\n" RESET);
        return -1;
    }

    printf("HTTP Status: %d\n", response.status);
    printf("Response size: %zu bytes\n", strlen(response.data));

    if (Parser_ParseSMHI(parser, response.data, smhiData) != 0) {
        printf(RED "ERROR: Failed to parse SMHI JSON\n" RESET);
        Fetcher_FreeResponse(&response);
        return -1;
    }

    Fetcher_FreeResponse(&response);

    printf("\nSMHI Data parsed:\n");
    printf("  Approved time: %s\n", smhiData->approvedTime);
    printf("  Reference time: %s\n", smhiData->referenceTime);
    printf("  Location: %.2f, %.2f\n", smhiData->latitude, smhiData->longitude);
    printf("  Time entries: %d\n", smhiData->count);

    // Verify first entry has required parameters
    if (smhiData->count > 0) {
        const SMHITimeEntry *first = &smhiData->timeSeries[0];
        printf("\nFirst entry (%s):\n", first->validTime);
        printf("  Parameters: %d\n", first->paramCount);

        const SMHIParameter *temp = FindSMHIParameter(first, "t");
        const SMHIParameter *humidity = FindSMHIParameter(first, "r");
        const SMHIParameter *wind = FindSMHIParameter(first, "ws");
        const SMHIParameter *cloud = FindSMHIParameter(first, "tcc_mean");

        if (temp) printf("  Temperature: %.1f °C\n", temp->value);
        if (humidity) printf("  Humidity: %.0f %%\n", humidity->value);
        if (wind) printf("  Wind speed: %.1f m/s\n", wind->value);
        if (cloud) printf("  Cloud cover: %.0f octas\n", cloud->value);

        int has_required = temp && humidity && wind && cloud;
        print_test_result("SMHI has required parameters", has_required);

        if (!has_required) return -1;
    }

    print_test_result("SMHI fetch and parse", 1);
    return 0;
}

int test_openmeteo_fetch_and_parse(Fetcher *fetcher, Parser *parser, OpenMeteoResponse *omData)
{
    print_header("TEST 2: Open-Meteo Fetch & Parse");

    char url[512];
    if (BuildOpenMeteoApiUrl(url, sizeof(url), WEATHER_LAT, WEATHER_LON) != 0) {
        printf(RED "ERROR: Failed to build Open-Meteo URL\n" RESET);
        return -1;
    }

    printf("URL: %s\n", url);

    FetchResponse response = {0};
    if (Fetcher_Fetch(fetcher, url, &response) != 0) {
        printf(RED "ERROR: Failed to fetch from Open-Meteo\n" RESET);
        return -1;
    }

    printf("HTTP Status: %d\n", response.status);
    printf("Response size: %zu bytes\n", strlen(response.data));

    if (Parser_ParseOpenMeteo(parser, response.data, omData) != 0) {
        printf(RED "ERROR: Failed to parse Open-Meteo JSON\n" RESET);
        Fetcher_FreeResponse(&response);
        return -1;
    }

    Fetcher_FreeResponse(&response);

    printf("\nOpen-Meteo Data parsed:\n");
    printf("  Time entries: %d\n", omData->count);

    if (omData->count > 0) {
        const OpenMeteoEntry *first = &omData->entries[0];
        printf("\nFirst entry (%s):\n", first->time);
        printf("  Temperature: %.1f °C\n", first->temperature_2m);
        printf("  Humidity: %.0f %%\n", first->humidity_2m);
        printf("  Cloud cover: %.0f %%\n", first->cloud_cover);
        printf("  Wind speed: %.1f km/h\n", first->wind_speed_10m);
        printf("  Solar irradiance: %.1f W/m²\n", first->shortwave_radiation);

        int has_solar = first->shortwave_radiation >= 0;
        print_test_result("Open-Meteo has solar irradiance", has_solar);

        if (!has_solar) return -1;
    }

    print_test_result("Open-Meteo fetch and parse", 1);
    return 0;
}

int test_timestamp_matching(const SMHIResponse *smhi, const OpenMeteoResponse *openmeteo)
{
    print_header("TEST 3: Timestamp Matching");

    if (smhi->count == 0 || openmeteo->count == 0) {
        printf(RED "ERROR: No data to match\n" RESET);
        return -1;
    }

    int matches = 0;
    int checked = 0;

    // Check first 10 entries
    int limit = (smhi->count < 10) ? smhi->count : 10;

    for (int i = 0; i < limit; i++) {
        const SMHITimeEntry *smhiEntry = &smhi->timeSeries[i];
        time_t smhiTime = ParseISO8601(smhiEntry->validTime);

        for (int j = 0; j < openmeteo->count; j++) {
            const OpenMeteoEntry *omEntry = &openmeteo->entries[j];
            time_t omTime = ParseISO8601(omEntry->time);

            int diff = abs((int)difftime(smhiTime, omTime));

            if (diff < 60) {  // 60 second tolerance
                matches++;

                if (matches <= 3) {  // Print first 3 matches
                    printf("Match %d:\n", matches);
                    printf("  SMHI: %s\n", smhiEntry->validTime);
                    printf("  Open-Meteo: %s\n", omEntry->time);
                    printf("  Time diff: %d seconds\n", diff);
                }
                break;
            }
        }
        checked++;
    }

    printf("\nMatching results:\n");
    printf("  Checked: %d entries\n", checked);
    printf("  Matched: %d entries\n", matches);
    printf("  Match rate: %.1f%%\n", (matches * 100.0) / checked);

    int success = (matches >= checked * 0.8);  // At least 80% should match
    print_test_result("Timestamp matching (≥80%)", success);

    return success ? 0 : -1;
}

int test_weather_data_combination(const SMHIResponse *smhi, const OpenMeteoResponse *openmeteo)
{
    print_header("TEST 4: WeatherData Combination");

    WeatherData weather = {0};
    weather.lastUpdated = time(NULL);

    int count = 0;
    int with_solar = 0;

    // Combine first 10 entries
    int limit = (smhi->count < 10) ? smhi->count : 10;

    for (int i = 0; i < limit && count < 96; i++) {
        const SMHITimeEntry *smhiEntry = &smhi->timeSeries[i];
        WeatherEntry *entry = &weather.entries[count];

        entry->timestamp = ParseISO8601(smhiEntry->validTime);

        // Extract SMHI parameters
        const SMHIParameter *temp = FindSMHIParameter(smhiEntry, "t");
        const SMHIParameter *humidity = FindSMHIParameter(smhiEntry, "r");
        const SMHIParameter *windSpeed = FindSMHIParameter(smhiEntry, "ws");
        const SMHIParameter *cloudCover = FindSMHIParameter(smhiEntry, "tcc_mean");

        if (temp) entry->temperature = temp->value;
        if (humidity) entry->humidity = humidity->value;
        if (windSpeed) entry->windSpeed = windSpeed->value;
        if (cloudCover) entry->cloudCover = (cloudCover->value / 8.0) * 100.0;

        // Find matching Open-Meteo entry for solar
        entry->solarIrradiance = 0.0;
        for (int j = 0; j < openmeteo->count; j++) {
            const OpenMeteoEntry *omEntry = &openmeteo->entries[j];
            time_t omTime = ParseISO8601(omEntry->time);

            if (abs((int)difftime(entry->timestamp, omTime)) < 60) {
                entry->solarIrradiance = omEntry->shortwave_radiation;
                if (entry->solarIrradiance > 0) with_solar++;
                break;
            }
        }

        entry->valid = true;
        count++;
    }

    weather.count = count;

    printf("Combined WeatherData:\n");
    printf("  Total entries: %d\n", count);
    printf("  Entries with solar data (>0 W/m²): %d\n", with_solar);

    // Print first 3 combined entries
    printf("\nFirst 3 combined entries:\n");
    for (int i = 0; i < 3 && i < count; i++) {
        WeatherEntry *e = &weather.entries[i];
        char timestr[64];
        strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M", localtime(&e->timestamp));

        printf("\nEntry %d (%s):\n", i+1, timestr);
        printf("  Temperature: %.1f °C (SMHI)\n", e->temperature);
        printf("  Humidity: %.0f %% (SMHI)\n", e->humidity);
        printf("  Wind: %.1f m/s (SMHI)\n", e->windSpeed);
        printf("  Clouds: %.0f %% (SMHI)\n", e->cloudCover);
        printf("  Solar: %.1f W/m² (OpenMeteo)\n", e->solarIrradiance);
    }

    // Success if we got entries and all have valid data structure
    // Solar can be 0 at night, which is correct
    int success = (count > 0);

    if (with_solar == 0) {
        printf(YELLOW "\nNote: No solar radiation >0 (testing during night/dark hours)\n" RESET);
    }

    print_test_result("WeatherData combination", success);

    return success ? 0 : -1;
}

int main(void)
{
    printf("\n" CYAN "╔════════════════════════════════════════╗\n");
    printf("║  Multi-Source Weather Integration Test ║\n");
    printf("╚════════════════════════════════════════╝" RESET "\n");

    Fetcher fetcher = {0};
    if (Fetcher_Initiate(&fetcher) != 0) {
        printf(RED "ERROR: Failed to initialize Fetcher\n" RESET);
        return 1;
    }

    Parser parser = {0};
    if (Parser_Initiate(&parser) != 0) {
        printf(RED "ERROR: Failed to initialize Parser\n" RESET);
        Fetcher_Shutdown(&fetcher);
        return 1;
    }

    int result = 0;
    SMHIResponse smhiData = {0};
    OpenMeteoResponse omData = {0};

    // Run tests
    if (test_smhi_fetch_and_parse(&fetcher, &parser, &smhiData) != 0) {
        result = 1;
        goto cleanup;
    }

    if (test_openmeteo_fetch_and_parse(&fetcher, &parser, &omData) != 0) {
        result = 1;
        goto cleanup;
    }

    if (test_timestamp_matching(&smhiData, &omData) != 0) {
        result = 1;
        goto cleanup;
    }

    if (test_weather_data_combination(&smhiData, &omData) != 0) {
        result = 1;
        goto cleanup;
    }

cleanup:
    Parser_Shutdown(&parser);
    Fetcher_Shutdown(&fetcher);

    printf("\n" CYAN "========================================" RESET "\n");
    if (result == 0) {
        printf(GREEN "ALL TESTS PASSED ✓\n" RESET);
    } else {
        printf(RED "SOME TESTS FAILED ✗\n" RESET);
    }
    printf(CYAN "========================================" RESET "\n\n");

    return result;
}
