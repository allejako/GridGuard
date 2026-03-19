#define _POSIX_C_SOURCE 200809L

#include "fetcher/Fetcher.h"
#include "net/HTTPFetcher.h"
#include "cache/SharedCache.h"
#include "api/APIEndpoints.h"
#include "domain/Config.h"
#include "sys/Logger.h"
#include "sys/ProcessHeartbeat.h"
#include <sys/select.h>
#include "ipc/WorkRequest.h"
#include "ipc/FetchResult.h"
#include "libs/cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>

// Timestamp file path for cache invalidation signaling
#define DATA_UPDATE_TIMESTAMP_PATH "/tmp/gridguard_last_data_update"

// Maximum reasonable number of price entries (24h * 4 quarters + buffer = 100)
#define MAX_PRICE_ENTRIES 100

// Helper: Write timestamp file to signal that fresh data is available
// This allows the server to invalidate forecast caches automatically
static void signal_data_updated(void)
{
    FILE *f = fopen(DATA_UPDATE_TIMESTAMP_PATH, "w");
    if (f)
    {
        fprintf(f, "%ld", time(NULL));
        fclose(f);
        LOG_DEBUG("FetcherProcess: Data update timestamp written");
    }
    else
    {
        LOG_WARNING("FetcherProcess: Failed to write data update timestamp");
    }
}

// Helper: Validate Elpriset API response structure
// Returns true if array is valid (correct size and has required fields)
static bool validate_price_array(cJSON *array, const char *label)
{
    if (!array || !cJSON_IsArray(array))
    {
        LOG_ERROR("FetcherProcess: %s is not a valid JSON array", label);
        return false;
    }

    int count = cJSON_GetArraySize(array);
    if (count == 0)
    {
        LOG_WARNING("FetcherProcess: %s is empty", label);
        return false;
    }

    if (count > MAX_PRICE_ENTRIES)
    {
        LOG_ERROR("FetcherProcess: %s has too many entries (%d > %d)", label, count, MAX_PRICE_ENTRIES);
        return false;
    }

    // Validate first entry has required fields
    cJSON *first = cJSON_GetArrayItem(array, 0);
    if (!first || !cJSON_IsObject(first))
    {
        LOG_ERROR("FetcherProcess: %s first entry is not an object", label);
        return false;
    }

    if (!cJSON_HasObjectItem(first, "time_start") || !cJSON_HasObjectItem(first, "SEK_per_kWh"))
    {
        LOG_ERROR("FetcherProcess: %s missing required fields (time_start, SEK_per_kWh)", label);
        return false;
    }

    LOG_DEBUG("FetcherProcess: %s validated (%d entries)", label, count);
    return true;
}

int FetcherProcess_Initiate(FetcherProcess *proc, const char *requestFifoPath, const char *resultFifoPath)
{
    if (!proc || !requestFifoPath || !resultFifoPath) return -1;

    memset(proc, 0, sizeof(FetcherProcess));
    strncpy(proc->requestFifoPath, requestFifoPath, sizeof(proc->requestFifoPath) - 1);
    strncpy(proc->resultFifoPath, resultFifoPath, sizeof(proc->resultFifoPath) - 1);

    // Allokera HTTPFetcher service
    proc->fetcher = calloc(1, sizeof(HTTPFetcher));
    if (!proc->fetcher)
    {
        LOG_ERROR("FetcherProcess: Failed to allocate HTTPFetcher");
        return -1;
    }

    if (HTTPFetcher_Initiate((HTTPFetcher *)proc->fetcher) != 0)
    {
        LOG_ERROR("FetcherProcess: Failed to initiate HTTPFetcher");
        free(proc->fetcher);
        return -1;
    }

    // Open shared memory caches (created by main process)
    proc->weatherCache = calloc(1, sizeof(SharedCache));
    proc->priceCache = calloc(1, sizeof(SharedCache));

    if (!proc->weatherCache || !proc->priceCache)
    {
        LOG_ERROR("FetcherProcess: Failed to allocate cache structures");
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
        free(proc->weatherCache);
        free(proc->priceCache);
        return -1;
    }

    // Attach to existing shared memory segmentss
    if (SharedCache_Initiate((SharedCache *)proc->weatherCache, "/gridguard_weather", 900 /* 15 min */) != 0)
    {
        LOG_ERROR("FetcherProcess: Failed to attach to weather cache");
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
        free(proc->weatherCache);
        free(proc->priceCache);
        return -1;
    }

    if (SharedCache_Initiate((SharedCache *)proc->priceCache, "/gridguard_price", 43200 /* 12 h — prices published once per day */) != 0)
    {
        LOG_ERROR("FetcherProcess: Failed to attach to price cache");
        SharedCache_Shutdown((SharedCache *)proc->weatherCache);
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
        free(proc->weatherCache);
        free(proc->priceCache);
        return -1;
    }

    // Open request FIFO for reading (blocks until server opens write end)
    proc->requestFifoFd = open(requestFifoPath, O_RDONLY);
    if (proc->requestFifoFd < 0)
    {
        LOG_ERROR("FetcherProcess: Failed to open request FIFO %s for reading", requestFifoPath);
        SharedCache_Shutdown((SharedCache *)proc->priceCache);
        SharedCache_Shutdown((SharedCache *)proc->weatherCache);
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
        free(proc->weatherCache);
        free(proc->priceCache);
        return -1;
    }

    // Open result FIFO for writing (blocks until the parser process opens the read end)
    proc->resultFifoFd = open(resultFifoPath, O_WRONLY);
    if (proc->resultFifoFd < 0)
    {
        LOG_ERROR("FetcherProcess: Failed to open result FIFO %s for writing", resultFifoPath);
        close(proc->requestFifoFd);
        SharedCache_Shutdown((SharedCache *)proc->priceCache);
        SharedCache_Shutdown((SharedCache *)proc->weatherCache);
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
        free(proc->weatherCache);
        free(proc->priceCache);
        return -1;
    }

    // Initialize separate timers for weather (15 min) and price (daily at 13:00)
    proc->weatherIntervalSeconds = 900;  // 15 minutes
    proc->lastWeatherFetch = time(NULL);
    proc->lastPriceFetch = 0;       // Force immediate price fetch on startup
    proc->lastPriceFetchDate = 0;
    proc->tomorrowPricesFetched = false;

    proc->isRunning = true;

    LOG_INFO("FetcherProcess: Initialized (PID %d, Request FIFO %s, Result FIFO %s, Weather interval: %ds, Price fetch: daily at 13:00 CET)", getpid(), requestFifoPath, resultFifoPath, proc->weatherIntervalSeconds);
    return 0;
}

int FetcherProcess_Run(FetcherProcess *proc)
{
    if (!proc || !proc->isRunning)
        return -1;

    HTTPFetcher *fetcher = (HTTPFetcher *)proc->fetcher;
    SharedCache *weatherCache = (SharedCache *)proc->weatherCache;
    SharedCache *priceCache = (SharedCache *)proc->priceCache;

    ProcessHeartbeat heartbeat;
    ProcessHeartbeat_Initiate(&heartbeat, 5);

    LOG_INFO("FetcherProcess: Starting main loop");

    while (proc->isRunning)
    {
        ProcessHeartbeat_Send(&heartbeat);

        // Use select() with timeout to check if request FIFO has data, allowing periodic heartbeats.
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(proc->requestFifoFd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(proc->requestFifoFd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0)
        {
            LOG_ERROR("FetcherProcess: select() failed");
            break;
        }

        if (ready == 0)
        {
            // Timeout, no user request - check if it's time for periodic background fetch
            time_t now = time(NULL);

            // CRITICAL: Must use Europe/Stockholm timezone for price fetch timing
            // Elprisetjustnu publishes at 13:00 CET, regardless of server timezone
            struct tm now_tm_stockholm;
            {
                // Force Europe/Stockholm timezone for this conversion
                const char *_tmp_tz = getenv("TZ");
                char *old_tz = _tmp_tz ? strdup(_tmp_tz) : NULL;
                setenv("TZ", "Europe/Stockholm", 1);
                tzset();
                localtime_r(&now, &now_tm_stockholm);
                if (old_tz) { setenv("TZ", old_tz, 1); free(old_tz); }
                else          unsetenv("TZ");
                tzset();
            }

            // === PRICE FETCH: Daily at 13:00 CET (when tomorrow's prices are published) ===
            // Two cases:
            //   (A) First fetch of the day  — triggers once after 13:00, no throttle
            //   (B) Tomorrow retry          — elprisetjustnu may publish up to 14:00;
            //       keep retrying every 5 minutes until tomorrow's prices arrive.
            //       No hard stop: we keep trying until we succeed.
            int today_date = (now_tm_stockholm.tm_year + 1900) * 10000 +
                             (now_tm_stockholm.tm_mon + 1) * 100 +
                             now_tm_stockholm.tm_mday;

            bool first_fetch_today = (proc->lastPriceFetchDate != today_date &&
                                      now_tm_stockholm.tm_hour >= 13);

            bool retry_for_tomorrow = (proc->lastPriceFetchDate == today_date &&
                                       !proc->tomorrowPricesFetched &&
                                       now_tm_stockholm.tm_hour >= 13 &&
                                       (now - proc->lastPriceFetch) >= 300);  // 5-min throttle

            bool should_fetch_prices = first_fetch_today || retry_for_tomorrow;

            if (first_fetch_today)
                LOG_INFO("FetcherProcess: Daily price fetch triggered (Stockholm %02d:00)", now_tm_stockholm.tm_hour);
            else if (retry_for_tomorrow)
                LOG_INFO("FetcherProcess: Retrying tomorrow's prices (not yet published at 13:00)");

            if (should_fetch_prices && now >= proc->priceBackoffUntil)
            {
                // Fetch prices for all Swedish regions (SE1-SE4)
                const char *regions[] = {"SE1", "SE2", "SE3", "SE4"};
                int regions_count = 4;
                int fetched = 0, skipped = 0, tomorrowFetched = 0;

                // Use Stockholm timezone for today/tomorrow date calculation
                struct tm today, tomorrow;
                today = now_tm_stockholm;  // Already in Stockholm time
                // Use mktime to advance by one calendar day in Stockholm TZ.
                // Adding 86400 seconds fails on DST spring-forward nights (23 h day).
                tomorrow = today;
                tomorrow.tm_mday += 1;
                tomorrow.tm_isdst = -1;
                {
                    const char *_tmp_tz = getenv("TZ");
                    char *old_tz = _tmp_tz ? strdup(_tmp_tz) : NULL;
                    setenv("TZ", "Europe/Stockholm", 1);
                    tzset();
                    mktime(&tomorrow);  // normalise overflow (e.g. mday=32 → next month)
                    if (old_tz) { setenv("TZ", old_tz, 1); free(old_tz); }
                    else          unsetenv("TZ");
                    tzset();
                }

                for (int r = 0; r < regions_count; r++)
                {
                    const char *region = regions[r];
                    char priceKey[256];
                    snprintf(priceKey, sizeof(priceKey), "%s_%04d-%02d-%02d+%04d-%02d-%02d", region, today.tm_year + 1900, today.tm_mon + 1, today.tm_mday, tomorrow.tm_year + 1900, tomorrow.tm_mon + 1, tomorrow.tm_mday);

                    // Invalidate old cache to force fresh fetch
                    SharedCache_Invalidate(priceCache, priceKey);

                    char todayUrl[256], tomorrowUrl[256];
                    HTTPFetchResponse todayResp, tomorrowResp;
                    bool todaySuccess = false, tomorrowSuccess = false;

                    if (BuildSpotPriceApiUrl(todayUrl, sizeof(todayUrl), region, NULL) == 0)
                    {
                        if (HTTPFetcher_Fetch(fetcher, todayUrl, &todayResp) == 0)
                            todaySuccess = true;
                    }

                    if (BuildSpotPriceTomorrowUrl(tomorrowUrl, sizeof(tomorrowUrl), region) == 0)
                    {
                        if (HTTPFetcher_Fetch(fetcher, tomorrowUrl, &tomorrowResp) == 0)
                            tomorrowSuccess = true;
                    }

                    // Merge today + tomorrow and cache
                    if (todaySuccess && tomorrowSuccess)
                    {
                        cJSON *todayArray = cJSON_Parse(todayResp.data);
                        cJSON *tomorrowArray = cJSON_Parse(tomorrowResp.data);

                        // Validate both arrays before merging
                        bool todayValid = validate_price_array(todayArray, "Today prices");
                        bool tomorrowValid = validate_price_array(tomorrowArray, "Tomorrow prices");

                        if (todayValid && tomorrowValid)
                        {
                            cJSON *item = NULL;
                            cJSON_ArrayForEach(item, tomorrowArray)
                            {
                                cJSON_AddItemToArray(todayArray, cJSON_Duplicate(item, 1));
                            }

                            char *combinedJson = cJSON_PrintUnformatted(todayArray);
                            if (combinedJson)
                            {
                                SharedCache_Store(priceCache, priceKey, combinedJson);
                                cJSON_free(combinedJson);
                                fetched++;
                                tomorrowFetched++;
                                LOG_INFO("FetcherProcess: Cached %s prices (today+tomorrow)", region);
                            }
                        }
                        else
                        {
                            LOG_ERROR("FetcherProcess: %s price data validation failed", region);
                        }

                        if (todayArray) cJSON_Delete(todayArray);
                        if (tomorrowArray) cJSON_Delete(tomorrowArray);

                        proc->priceFailures = 0;
                        proc->priceBackoffUntil = 0;
                    }
                    else if (todaySuccess)  // At least got today's prices
                    {
                        cJSON *todayArray = cJSON_Parse(todayResp.data);
                        if (validate_price_array(todayArray, "Today prices"))
                        {
                            char *combinedJson = cJSON_PrintUnformatted(todayArray);
                            if (combinedJson)
                            {
                                SharedCache_Store(priceCache, priceKey, combinedJson);
                                cJSON_free(combinedJson);
                                fetched++;
                                LOG_WARNING("FetcherProcess: Only today's prices available for %s (tomorrow not published yet)", region);
                            }
                        }
                        else
                        {
                            LOG_ERROR("FetcherProcess: %s today price data validation failed", region);
                        }

                        if (todayArray) cJSON_Delete(todayArray);
                        proc->priceFailures = 0;
                    }
                    else
                    {
                        proc->priceFailures++;
                        if (proc->priceFailures >= 5)
                            proc->priceBackoffUntil = time(NULL) + 300;
                        skipped++;
                    }

                    if (todaySuccess) HTTPFetcher_FreeResponse(&todayResp);
                    if (tomorrowSuccess) HTTPFetcher_FreeResponse(&tomorrowResp);
                }

                LOG_INFO("FetcherProcess: Price fetch complete — %d regions with tomorrow, %d today-only, %d failed", tomorrowFetched, fetched - tomorrowFetched, skipped);

                // Signal that fresh price data is available (for cache invalidation)
                if (fetched > 0)
                {
                    signal_data_updated();
                }

                proc->lastPriceFetch = now;
                proc->lastPriceFetchDate = today_date;  // Always mark so retry throttle works
                // tomorrowPricesFetched = true only when ALL regions got tomorrow's data.
                // If any region is still missing tomorrow, keep retrying every 5 minutes.
                proc->tomorrowPricesFetched = (tomorrowFetched == regions_count && skipped == 0);
                if (!proc->tomorrowPricesFetched && fetched > 0)
                    LOG_INFO("FetcherProcess: Tomorrow's prices not yet published — will retry in 5 min");
            }

            // === WEATHER CACHE INVALIDATION: Every 15 minutes ===
            // Proactively invalidate weather cache to force fresh fetch on next user request.
            // This ensures weather data is never more than 15 minutes stale.
            time_t weather_elapsed = now - proc->lastWeatherFetch;
            if (weather_elapsed >= proc->weatherIntervalSeconds)
            {
                LOG_INFO("FetcherProcess: Invalidating ALL weather cache entries after %ld seconds", weather_elapsed);

                // Invalidate ALL weather cache entries to force fresh fetch
                // This prevents the TTL timing issue where:
                // - Weather fetched at 13:00:00, cached with TTL=900s (expires 13:15:00)
                // - Fetcher checks at 13:15:00: elapsed=900s >= 900s ✓
                // - But cache lookup at 13:15:00: age=900s > 900s? NO (not strictly greater)
                // - Result: Cache HIT when we wanted cache MISS!
                SharedCache_InvalidateAll((SharedCache *)proc->weatherCache);

                proc->lastWeatherFetch = now;
                LOG_INFO("FetcherProcess: Weather cache invalidated, next request will fetch fresh data");
            }

            continue; // No FIFO data - loop again to send heartbeat
        }

        WorkRequest request;
        ssize_t bytesRead = read(proc->requestFifoFd, &request, sizeof(request));

        if (bytesRead == 0)
        {
            LOG_INFO("FetcherProcess: request FIFO closed, exiting");
            break;
        }

        if (bytesRead != sizeof(request))
        {
            LOG_ERROR("FetcherProcess: Partial read from request FIFO (%zd bytes)", bytesRead);
            continue;
        }

        LOG_INFO("FetcherProcess: Processing request for %s/%s", request.userId, request.region);

        // Prepare result
        FetchResult result = {0};
        strncpy(result.userId, request.userId, sizeof(result.userId) - 1);
        strncpy(result.location, request.location, sizeof(result.location) - 1);
        strncpy(result.region, request.region, sizeof(result.region) - 1);
        result.solarAreaM2 = request.solarAreaM2;
        result.solarEfficiency = request.solarEfficiency;
        result.consumptionKwh = request.consumptionKwh;
        result.gridFee_low = request.gridFee_low;
        result.gridFee_normal = request.gridFee_normal;
        result.gridFee_high = request.gridFee_high;

        time_t now; // For circuit breaker timing

        // Fetch weather data with caching
        char weatherKey[256];
        snprintf(weatherKey, sizeof(weatherKey), "openmeteo_%s_%s", request.lat, request.lon);

        if (SharedCache_Lookup(weatherCache, weatherKey, result.openMeteoJson, sizeof(result.openMeteoJson)) == 0)
        {
            LOG_INFO("FetcherProcess: Weather cache HIT (%s)", weatherKey);
        }
        else
        {
            // Check if we should skip due to previous failures
            now = time(NULL);
            if (now < proc->weatherBackoffUntil)
            {
                time_t remaining = proc->weatherBackoffUntil - now;
                LOG_WARNING("FetcherProcess: Weather API circuit open (%d failures, retry in %ld seconds)", proc->weatherFailures, remaining);
            }
            else
            {
                char openMeteoUrl[512];
                if (BuildOpenMeteoApiUrl(openMeteoUrl, sizeof(openMeteoUrl), request.lat, request.lon) == 0)
                {
                    HTTPFetchResponse omResp;
                    if (HTTPFetcher_Fetch(fetcher, openMeteoUrl, &omResp) == 0)
                    {
                        // Success - reset circuit breaker
                        proc->weatherFailures = 0;
                        proc->weatherBackoffUntil = 0;

                        strncpy(result.openMeteoJson, omResp.data, sizeof(result.openMeteoJson) - 1);
                        SharedCache_Store(weatherCache, weatherKey, omResp.data);
                        HTTPFetcher_FreeResponse(&omResp);
                        LOG_INFO("FetcherProcess: Fetched Open-Meteo data (%zu bytes)", strlen(result.openMeteoJson));

                        // Signal that fresh weather data is available (for cache invalidation)
                        signal_data_updated();
                    }
                    else
                    {
                        // Failure - increment counter and potentially open circuit
                        proc->weatherFailures++;
                        if (proc->weatherFailures >= 5)
                        {
                            proc->weatherBackoffUntil = time(NULL) + 300; // 5 minutes backoff
                            LOG_ERROR("FetcherProcess: Weather API circuit opened after %d failures. Backing off for 5 minutes.", proc->weatherFailures);
                        }
                        else
                        {
                            LOG_WARNING("FetcherProcess: Open-Meteo fetch failed (failure %d/5)", proc->weatherFailures);
                        }
                    }
                }
            }
        }

        // Fetch price data (today's + tomorrow's prices for 48h forecast)
        // Cache key format: "SE3_2026-03-17+2026-03-18" for combined 48h data
        char priceKey[256];
        now = time(NULL);
        struct tm today, tomorrow;
        // Use Stockholm TZ for both cache key and API URLs so they always match
        // the background-fetch keys, even when the server runs in UTC or another TZ.
        {
            const char *_tmp_tz = getenv("TZ");
            char *old_tz = _tmp_tz ? strdup(_tmp_tz) : NULL;
            setenv("TZ", "Europe/Stockholm", 1);
            tzset();
            localtime_r(&now, &today);
            tomorrow = today;
            tomorrow.tm_mday += 1;
            tomorrow.tm_isdst = -1;
            mktime(&tomorrow);  // normalise + handle DST spring-forward
            if (old_tz) { setenv("TZ", old_tz, 1); free(old_tz); } else unsetenv("TZ");
            tzset();
        }

        snprintf(priceKey, sizeof(priceKey), "%s_%04d-%02d-%02d+%04d-%02d-%02d", request.region, today.tm_year + 1900, today.tm_mon + 1, today.tm_mday, tomorrow.tm_year + 1900, tomorrow.tm_mon + 1, tomorrow.tm_mday);

        if (SharedCache_Lookup(priceCache, priceKey, result.priceJson, sizeof(result.priceJson)) == 0)
        {
            LOG_INFO("FetcherProcess: Price cache HIT (48h) (%s)", priceKey);
        }
        else
        {
            // Circuit breaker: check if we should skip due to previous failures
            if (now < proc->priceBackoffUntil)
            {
                time_t remaining = proc->priceBackoffUntil - now;
                LOG_WARNING("FetcherProcess: Price API circuit open (%d failures, retry in %ld seconds)", proc->priceFailures, remaining);
            }
            else
            {
                // Fetch today's prices
                char todayUrl[256], tomorrowUrl[256];
                HTTPFetchResponse todayResp, tomorrowResp;
                bool todaySuccess = false, tomorrowSuccess = false;

                // Pass pre-computed Stockholm dates so URL and cache key always agree.
                if (BuildSpotPriceApiUrl(todayUrl, sizeof(todayUrl), request.region, &today) == 0)
                {
                    if (HTTPFetcher_Fetch(fetcher, todayUrl, &todayResp) == 0)
                    {
                        todaySuccess = true;
                        LOG_INFO("FetcherProcess: Fetched today's prices (%zu bytes)", strlen(todayResp.data));
                    }
                }

                // Fetch tomorrow's prices
                if (BuildSpotPriceApiUrl(tomorrowUrl, sizeof(tomorrowUrl), request.region, &tomorrow) == 0)
                {
                    if (HTTPFetcher_Fetch(fetcher, tomorrowUrl, &tomorrowResp) == 0)
                    {
                        tomorrowSuccess = true;
                        LOG_INFO("FetcherProcess: Fetched tomorrow's prices (%zu bytes)", strlen(tomorrowResp.data));
                    }
                }

                // Merge today + tomorrow into one JSON array
                if (todaySuccess && tomorrowSuccess)
                {
                    // Parse and merge JSON arrays using cJSON
                    cJSON *todayArray = cJSON_Parse(todayResp.data);
                    cJSON *tomorrowArray = cJSON_Parse(tomorrowResp.data);

                    if (todayArray && tomorrowArray && cJSON_IsArray(todayArray) && cJSON_IsArray(tomorrowArray))
                    {
                        // Append tomorrow's entries to today's array
                        cJSON *item = NULL;
                        cJSON_ArrayForEach(item, tomorrowArray)
                        {
                            cJSON_AddItemToArray(todayArray, cJSON_Duplicate(item, 1));
                        }

                        // Serialize combined array
                        char *combinedJson = cJSON_PrintUnformatted(todayArray);
                        if (combinedJson)
                        {
                            strncpy(result.priceJson, combinedJson, sizeof(result.priceJson) - 1);
                            result.priceJson[sizeof(result.priceJson) - 1] = '\0';
                            SharedCache_Store(priceCache, priceKey, combinedJson);
                            cJSON_free(combinedJson);

                            proc->priceFailures = 0;
                            proc->priceBackoffUntil = 0;

                            LOG_INFO("FetcherProcess: Merged 48h price data (%zu bytes)", strlen(result.priceJson));
                        }
                    }

                    if (todayArray) cJSON_Delete(todayArray);
                    if (tomorrowArray) cJSON_Delete(tomorrowArray);
                }
                else if (todaySuccess)
                {
                    // Fallback: use only today's data if tomorrow not available
                    strncpy(result.priceJson, todayResp.data, sizeof(result.priceJson) - 1);
                    result.priceJson[sizeof(result.priceJson) - 1] = '\0';
                    LOG_WARNING("FetcherProcess: Using only today's prices (tomorrow not available yet)");
                }
                else
                {
                    // Both failed
                    proc->priceFailures++;
                    if (proc->priceFailures >= 5)
                    {
                        proc->priceBackoffUntil = time(NULL) + 300;
                        LOG_ERROR("FetcherProcess: Price API circuit opened after %d failures", proc->priceFailures);
                    }
                    else
                    {
                        LOG_WARNING("FetcherProcess: Price fetch failed (failure %d/5)", proc->priceFailures);
                    }
                }

                if (todaySuccess) HTTPFetcher_FreeResponse(&todayResp);
                if (tomorrowSuccess) HTTPFetcher_FreeResponse(&tomorrowResp);
            }
        }

        // Skriv FetchResult till result FIFO (Fetch -> Parse)
        ssize_t written = write(proc->resultFifoFd, &result, sizeof(result));
        if (written != sizeof(result))
        {
            LOG_ERROR("FetcherProcess: Failed to write to result FIFO (%zd bytes)", written);
            break;
        }

        LOG_INFO("FetcherProcess: Wrote FetchResult to result FIFO (%zd bytes)", written);
    }

    LOG_INFO("FetcherProcess: Main loop exited");
    return 0;
}

void FetcherProcess_Shutdown(FetcherProcess *proc)
{
    if (!proc)
        return;

    LOG_INFO("FetcherProcess: Shutting down");

    proc->isRunning = false;

    if (proc->requestFifoFd >= 0)
        close(proc->requestFifoFd);

    if (proc->resultFifoFd >= 0)
        close(proc->resultFifoFd);

    if (proc->priceCache)
    {
        SharedCache_Shutdown((SharedCache *)proc->priceCache);
        free(proc->priceCache);
    }

    if (proc->weatherCache)
    {
        SharedCache_Shutdown((SharedCache *)proc->weatherCache);
        free(proc->weatherCache);
    }

    if (proc->fetcher)
    {
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
    }

    LOG_INFO("FetcherProcess: Shutdown complete");
}
