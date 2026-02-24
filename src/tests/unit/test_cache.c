#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "JsonCache.h"
#include "Logger.h"

static int passed = 0;
static int failed = 0;

#define ASSERT(label, expr)                                    \
    do {                                                       \
        if (expr) {                                            \
            printf("  PASS: %s\n", label);                    \
            passed++;                                          \
        } else {                                               \
            printf("  FAIL: %s (line %d)\n", label, __LINE__);\
            failed++;                                          \
        }                                                      \
    } while (0)

static void print_separator(const char *title)
{
    printf("\n========== %s ==========\n", title);
}

// ----------------------------------------------------------------
// Test 1: Store och Lookup (HIT)
// ----------------------------------------------------------------
static void test_store_and_lookup(void)
{
    print_separator("TEST 1: Store och Lookup (HIT)");

    JsonCache cache;
    ASSERT("Initiate returnerar 0", JsonCache_Initiate(&cache, 60) == 0);
    ASSERT("isInitialized är true", cache.isInitialized == true);

    const char *key  = "test_key";
    const char *json = "{\"value\":42}";
    ASSERT("Store returnerar 0", JsonCache_Store(&cache, key, json) == 0);

    char out[256] = {0};
    ASSERT("Lookup returnerar 0 (HIT)", JsonCache_Lookup(&cache, key, out, sizeof(out)) == 0);
    ASSERT("Lookup ger rätt data", strcmp(out, json) == 0);

    JsonCache_Shutdown(&cache);
}

// ----------------------------------------------------------------
// Test 2: Lookup på nyckel som inte finns (MISS)
// ----------------------------------------------------------------
static void test_lookup_miss(void)
{
    print_separator("TEST 2: Lookup MISS");

    JsonCache cache;
    JsonCache_Initiate(&cache, 60);

    char out[256] = {0};
    ASSERT("Lookup på okänd nyckel returnerar -1", JsonCache_Lookup(&cache, "nonexistent", out, sizeof(out)) == -1);

    JsonCache_Shutdown(&cache);
}

// ----------------------------------------------------------------
// Test 3: TTL-expiry — entryn ska gå ut efter TTL
// ----------------------------------------------------------------
static void test_ttl_expiry(void)
{
    print_separator("TEST 3: TTL-expiry (TTL=1s, sleep 2s)");

    JsonCache cache;
    JsonCache_Initiate(&cache, 1); // 1 sekunds TTL

    ASSERT("Store returnerar 0", JsonCache_Store(&cache, "ttl_key", "{\"x\":1}") == 0);

    char out[256] = {0};
    ASSERT("Lookup direkt ger HIT", JsonCache_Lookup(&cache, "ttl_key", out, sizeof(out)) == 0);

    printf("  (sover 2 sekunder...)\n");
    sleep(2);

    ASSERT("Lookup efter expiry ger -1 (MISS)", JsonCache_Lookup(&cache, "ttl_key", out, sizeof(out)) == -1);

    JsonCache_Shutdown(&cache);
}

// ----------------------------------------------------------------
// Test 4: Update — lagra samma nyckel två gånger, data ska uppdateras
// ----------------------------------------------------------------
static void test_update_existing_key(void)
{
    print_separator("TEST 4: Update befintlig nyckel");

    JsonCache cache;
    JsonCache_Initiate(&cache, 60);

    JsonCache_Store(&cache, "update_key", "{\"v\":1}");
    JsonCache_Store(&cache, "update_key", "{\"v\":2}");

    char out[256] = {0};
    JsonCache_Lookup(&cache, "update_key", out, sizeof(out));
    ASSERT("Data uppdaterades till v=2", strcmp(out, "{\"v\":2}") == 0);

    // Kontrollera att det bara finns en entry (inte duplikat)
    int count = 0;
    for (int i = 0; i < JSON_CACHE_MAX_ENTRIES; i++)
    {
        if (cache.entries[i].occupied && strcmp(cache.entries[i].key, "update_key") == 0)
            count++;
    }
    ASSERT("Bara en entry för nyckeln (ingen duplikat)", count == 1);

    JsonCache_Shutdown(&cache);
}

// ----------------------------------------------------------------
// Test 5: Full cache — eviction av äldsta entry
// ----------------------------------------------------------------
static void test_eviction_when_full(void)
{
    print_separator("TEST 5: Eviction när cachen är full");

    JsonCache cache;
    JsonCache_Initiate(&cache, 60);

    // Fyll alla 64 platser
    char key[JSON_CACHE_KEY_MAX];
    char json[64];
    for (int i = 0; i < JSON_CACHE_MAX_ENTRIES; i++)
    {
        snprintf(key,  sizeof(key),  "key_%d", i);
        snprintf(json, sizeof(json), "{\"i\":%d}", i);
        JsonCache_Store(&cache, key, json);
    }

    // Lägg till en till — ska evicta äldsta (key_0)
    ASSERT("Store på full cache returnerar 0", JsonCache_Store(&cache, "overflow_key", "{\"overflow\":true}") == 0);

    char out[256] = {0};
    ASSERT("Ny nyckel finns i cachen", JsonCache_Lookup(&cache, "overflow_key", out, sizeof(out)) == 0);

    JsonCache_Shutdown(&cache);
}

// ----------------------------------------------------------------
// Test 6: NULL-hantering — alla funktioner ska klara NULL-input
// ----------------------------------------------------------------
static void test_null_handling(void)
{
    print_separator("TEST 6: NULL-hantering");

    char out[256] = {0};
    JsonCache cache;
    JsonCache_Initiate(&cache, 60);

    ASSERT("Initiate med NULL returnerar -1",    JsonCache_Initiate(NULL, 60) == -1);
    ASSERT("Store med NULL cache returnerar -1", JsonCache_Store(NULL, "k", "{}") == -1);
    ASSERT("Store med NULL nyckel returnerar -1",JsonCache_Store(&cache, NULL, "{}") == -1);
    ASSERT("Store med NULL json returnerar -1",  JsonCache_Store(&cache, "k", NULL) == -1);
    ASSERT("Lookup med NULL cache returnerar -1",JsonCache_Lookup(NULL, "k", out, sizeof(out)) == -1);
    ASSERT("Lookup med NULL nyckel returnerar -1",JsonCache_Lookup(&cache, NULL, out, sizeof(out)) == -1);
    ASSERT("Lookup med NULL buf returnerar -1",  JsonCache_Lookup(&cache, "k", NULL, sizeof(out)) == -1);

    JsonCache_Shutdown(&cache);
    JsonCache_Shutdown(NULL); // Ska inte krascha
    printf("  PASS: Shutdown med NULL kraschar inte\n");
    passed++;
}

// ----------------------------------------------------------------
// Test 7: Multipla nycklar — isolerade från varandra
// ----------------------------------------------------------------
static void test_multiple_keys(void)
{
    print_separator("TEST 7: Multipla nycklar isolerade");

    JsonCache cache;
    JsonCache_Initiate(&cache, 60);

    JsonCache_Store(&cache, "smhi_59.33_18.07",     "{\"smhi\":true}");
    JsonCache_Store(&cache, "openmeteo_59.33_18.07", "{\"openmeteo\":true}");
    JsonCache_Store(&cache, "SE3_2026-02-23",        "{\"price\":0.89}");

    char out[256];

    memset(out, 0, sizeof(out));
    JsonCache_Lookup(&cache, "smhi_59.33_18.07", out, sizeof(out));
    ASSERT("SMHI-nyckel ger rätt data", strcmp(out, "{\"smhi\":true}") == 0);

    memset(out, 0, sizeof(out));
    JsonCache_Lookup(&cache, "openmeteo_59.33_18.07", out, sizeof(out));
    ASSERT("Open-Meteo-nyckel ger rätt data", strcmp(out, "{\"openmeteo\":true}") == 0);

    memset(out, 0, sizeof(out));
    JsonCache_Lookup(&cache, "SE3_2026-02-23", out, sizeof(out));
    ASSERT("Prisnyckel ger rätt data", strcmp(out, "{\"price\":0.89}") == 0);

    JsonCache_Shutdown(&cache);
}

// ----------------------------------------------------------------
// main
// ----------------------------------------------------------------
int main(void)
{
    printf("JsonCache Unit Tests\n");
    printf("====================\n");

    if (Logger_Initiate("logs/test_cache.log", LOG_LEVEL_DEBUG) != 0)
        fprintf(stderr, "Warning: Logger init failed\n");

    test_store_and_lookup();
    test_lookup_miss();
    test_ttl_expiry();
    test_update_existing_key();
    test_eviction_when_full();
    test_null_handling();
    test_multiple_keys();

    print_separator("RESULTAT");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    if (failed == 0)
        printf("\nAlla tester GODKÄNDA!\n");
    else
        printf("\n%d test MISSLYCKADES.\n", failed);

    Logger_Shutdown();
    return failed == 0 ? 0 : 1;
}
