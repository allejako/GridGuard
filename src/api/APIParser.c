#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "api/APIParser.h"
#include "libs/cJSON.h"
#include "sys/Logger.h"

static double validate_temperature(double temp)
{
    if (!isfinite(temp)) {
        LOG_WARNING("Invalid temperature value (non-finite), defaulting to 0°C");
        return 0.0;
    }
    if (temp < -60.0 || temp > 60.0) {
        LOG_WARNING("Temperature out of bounds: %.1f°C, clamping to valid range", temp);
        if (temp < -60.0) return -60.0;
        if (temp > 60.0) return 60.0;
    }
    return temp;
}

static double validate_humidity(double humidity)
{
    if (!isfinite(humidity)) {
        LOG_WARNING("Invalid humidity value (non-finite), defaulting to 50%%");
        return 50.0;
    }
    if (humidity < 0.0 || humidity > 100.0) {
        LOG_WARNING("Humidity out of bounds: %.1f%%, clamping to 0-100%%", humidity);
        if (humidity < 0.0) return 0.0;
        if (humidity > 100.0) return 100.0;
    }
    return humidity;
}

static double validate_cloud_cover(double cloud)
{
    if (!isfinite(cloud)) {
        LOG_WARNING("Invalid cloud cover value (non-finite), defaulting to 50%%");
        return 50.0;
    }
    if (cloud < 0.0 || cloud > 100.0) {
        LOG_WARNING("Cloud cover out of bounds: %.1f%%, clamping to 0-100%%", cloud);
        if (cloud < 0.0) return 0.0;
        if (cloud > 100.0) return 100.0;
    }
    return cloud;
}

static double validate_wind_speed(double wind)
{
    if (!isfinite(wind)) {
        LOG_WARNING("Invalid wind speed value (non-finite), defaulting to 0 m/s");
        return 0.0;
    }
    if (wind < 0.0 || wind > 150.0) {
        LOG_WARNING("Wind speed out of bounds: %.1f m/s, clamping to 0-150 m/s", wind);
        if (wind < 0.0) return 0.0;
        if (wind > 150.0) return 150.0;
    }
    return wind;
}

static double validate_solar_radiation(double solar)
{
    if (!isfinite(solar)) {
        LOG_WARNING("Invalid solar radiation value (non-finite), defaulting to 0 W/m²");
        return 0.0;
    }
    if (solar < 0.0 || solar > 1500.0) {
        LOG_WARNING("Solar radiation out of bounds: %.1f W/m², clamping to 0-1500 W/m²", solar);
        if (solar < 0.0) return 0.0;
        if (solar > 1500.0) return 1500.0;
    }
    return solar;
}

static double validate_spot_price_sek(double price)
{
    if (!isfinite(price)) {
        LOG_WARNING("Invalid spot price SEK value (non-finite), defaulting to 1.0 SEK/kWh");
        return 1.0;
    }
    if (price < 0.0 || price > 20.0) {
        LOG_WARNING("Spot price SEK out of bounds: %.2f SEK/kWh, clamping to 0-20", price);
        if (price < 0.0) return 0.0;
        if (price > 20.0) return 20.0;
    }
    return price;
}

static double validate_spot_price_eur(double price)
{
    if (!isfinite(price)) {
        LOG_WARNING("Invalid spot price EUR value (non-finite), defaulting to 0.1 EUR/kWh");
        return 0.1;
    }
    if (price < 0.0 || price > 2.0) {
        LOG_WARNING("Spot price EUR out of bounds: %.2f EUR/kWh, clamping to 0-2", price);
        if (price < 0.0) return 0.0;
        if (price > 2.0) return 2.0;
    }
    return price;
}

static double validate_exchange_rate(double exr)
{
    if (!isfinite(exr)) {
        LOG_WARNING("Invalid exchange rate value (non-finite), defaulting to 11.0 SEK/EUR");
        return 11.0;
    }
    if (exr < 5.0 || exr > 20.0) {
        LOG_WARNING("Exchange rate out of bounds: %.2f SEK/EUR, clamping to 5-20", exr);
        if (exr < 5.0) return 5.0;
        if (exr > 20.0) return 20.0;
    }
    return exr;
}

static void validate_time_string(char *time, size_t maxLen)
{
    if (!time || maxLen == 0) return;

    time[maxLen - 1] = '\0';

    for (size_t i = 0; i < maxLen && time[i] != '\0'; i++)
    {
        char c = time[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '-' || c == ':' || c == 'T' || c == '+'))
        {
            LOG_WARNING("Invalid character in time string at position %zu, replacing with '_'", i);
            time[i] = '_';
        }
    }
}

int APIParser_Initiate(APIParser *parser)
{
    if (!parser)
        return -1;

    parser->isInitialized = true;
    pthread_mutex_init(&parser->mutex, NULL);
    LOG_INFO("APIParser initialized");
    return 0;
}

int APIParser_ParseOpenMeteo(APIParser *parser, const char *jsonData, OpenMeteoResponse *forecast)
{
    if (!parser || !parser->isInitialized || !jsonData || !forecast)
    {
        LOG_ERROR("Invalid parameters for APIParser_ParseOpenMeteo");
        return -1;
    }

    pthread_mutex_lock(&parser->mutex);

    cJSON *root = cJSON_Parse(jsonData);
    if (!root)
    {
        LOG_ERROR("Failed to parse weather JSON: %s", cJSON_GetErrorPtr());
        pthread_mutex_unlock(&parser->mutex);
        return -1;
    }

    // Open-Meteo format: minutely_15.time[], minutely_15.temperature_2m[], etc.
    cJSON *minutely = cJSON_GetObjectItem(root, "minutely_15");
    if (!cJSON_IsObject(minutely))
    {
        LOG_ERROR("Weather JSON missing 'minutely_15' object");
        cJSON_Delete(root);
        pthread_mutex_unlock(&parser->mutex);
        return -1;
    }

    cJSON *times = cJSON_GetObjectItem(minutely, "time");
    cJSON *temps = cJSON_GetObjectItem(minutely, "temperature_2m");
    cJSON *humidity = cJSON_GetObjectItem(minutely, "relative_humidity_2m");
    cJSON *clouds = cJSON_GetObjectItem(minutely, "cloud_cover");
    cJSON *winds = cJSON_GetObjectItem(minutely, "wind_speed_10m");
    cJSON *solar = cJSON_GetObjectItem(minutely, "shortwave_radiation");

    if (!cJSON_IsArray(times))
    {
        LOG_ERROR("Weather JSON missing 'minutely_15.time' array");
        cJSON_Delete(root);
        pthread_mutex_unlock(&parser->mutex);
        return -1;
    }

    int arraySize = cJSON_GetArraySize(times);
    int parsedCount = 0;

    for (int i = 0; i < arraySize && parsedCount < 192; i++)  // Weather limited to 192 (48h forecast)
    {
        OpenMeteoEntry *data = &forecast->entries[parsedCount];

        cJSON *timeItem = cJSON_GetArrayItem(times, i);
        if (cJSON_IsString(timeItem))
        {
            strncpy(data->time, timeItem->valuestring, sizeof(data->time) - 1);
            data->time[sizeof(data->time) - 1] = '\0';
            validate_time_string(data->time, sizeof(data->time));
        }
        else
        {
            data->time[0] = '\0';
        }

        cJSON *tempVal = cJSON_GetArrayItem(temps, i);
        double temp = cJSON_IsNumber(tempVal) ? tempVal->valuedouble : 0.0;
        data->temperature_2m = validate_temperature(temp);

        cJSON *humVal = cJSON_GetArrayItem(humidity, i);
        double hum = cJSON_IsNumber(humVal) ? humVal->valuedouble : 0.0;
        data->humidity_2m = validate_humidity(hum);

        cJSON *cloudVal = cJSON_GetArrayItem(clouds, i);
        double cld = cJSON_IsNumber(cloudVal) ? cloudVal->valuedouble : 0.0;
        data->cloud_cover = validate_cloud_cover(cld);

        cJSON *windVal = cJSON_GetArrayItem(winds, i);
        double wnd = cJSON_IsNumber(windVal) ? windVal->valuedouble : 0.0;
        data->wind_speed_10m = validate_wind_speed(wnd);

        cJSON *solarVal = cJSON_GetArrayItem(solar, i);
        double sol = cJSON_IsNumber(solarVal) ? solarVal->valuedouble : 0.0;
        data->shortwave_radiation = validate_solar_radiation(sol);

        parsedCount++;
    }

    forecast->count = parsedCount;

    cJSON_Delete(root);
    LOG_INFO("Parsed %d weather forecasts", parsedCount);
    pthread_mutex_unlock(&parser->mutex);
    return 0;
}

int APIParser_ParseElpriset(APIParser *parser, const char *jsonData, ElprisetResponse *spotData)
{
    if (!parser || !parser->isInitialized || !jsonData || !spotData)
    {
        LOG_ERROR("Invalid parameters for APIParser_ParseElpriset");
        return -1;
    }

    pthread_mutex_lock(&parser->mutex);

    cJSON *root = cJSON_Parse(jsonData);
    if (!root)
    {
        LOG_ERROR("Failed to parse spot price JSON: %s", cJSON_GetErrorPtr());
        pthread_mutex_unlock(&parser->mutex);
        return -1;
    }

    // Handle array directly (elprisetjustnu.se returns array)
    if (!cJSON_IsArray(root))
    {
        LOG_ERROR("Spot price JSON is not an array");
        cJSON_Delete(root);
        pthread_mutex_unlock(&parser->mutex);
        return -1;
    }

    int arraySize = cJSON_GetArraySize(root);
    int parsedCount = 0;

    for (int i = 0; i < arraySize && parsedCount < 192; i++)  // Prices can hold 2 days (today + tomorrow)
    {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!item)
            continue;

        ElprisetEntry *price = &spotData->entries[parsedCount];

        cJSON *timeStart = cJSON_GetObjectItem(item, "time_start");
        if (cJSON_IsString(timeStart))
        {
            strncpy(price->time_start, timeStart->valuestring, sizeof(price->time_start) - 1);
            price->time_start[sizeof(price->time_start) - 1] = '\0';
            validate_time_string(price->time_start, sizeof(price->time_start));
        }
        else
        {
            price->time_start[0] = '\0';
        }

        cJSON *timeEnd = cJSON_GetObjectItem(item, "time_end");
        if (cJSON_IsString(timeEnd))
        {
            strncpy(price->time_end, timeEnd->valuestring, sizeof(price->time_end) - 1);
            price->time_end[sizeof(price->time_end) - 1] = '\0';
            validate_time_string(price->time_end, sizeof(price->time_end));
        }
        else
        {
            price->time_end[0] = '\0';
        }

        cJSON *sekPerKwh = cJSON_GetObjectItem(item, "SEK_per_kWh");
        double sek = cJSON_IsNumber(sekPerKwh) ? sekPerKwh->valuedouble : 0.0;
        price->SEK_per_kWh = validate_spot_price_sek(sek);

        cJSON *eurPerKwh = cJSON_GetObjectItem(item, "EUR_per_kWh");
        double eur = cJSON_IsNumber(eurPerKwh) ? eurPerKwh->valuedouble : 0.0;
        price->EUR_per_kWh = validate_spot_price_eur(eur);

        cJSON *exr = cJSON_GetObjectItem(item, "EXR");
        double exrVal = cJSON_IsNumber(exr) ? exr->valuedouble : 0.0;
        price->EXR = validate_exchange_rate(exrVal);

        parsedCount++;
    }

    spotData->count = parsedCount;

    cJSON_Delete(root);
    LOG_INFO("Parsed %d spot prices", parsedCount);
    pthread_mutex_unlock(&parser->mutex);
    return 0;
}

void APIParser_Shutdown(APIParser *parser)
{
    if (!parser || !parser->isInitialized)
        return;

    pthread_mutex_destroy(&parser->mutex);
    parser->isInitialized = false;
    LOG_INFO("APIParser shutdown");
}
