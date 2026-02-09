#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cache.h"

static int passed = 0;
static int failed = 0;

#define TEST(name, expr) do { \
    if (expr) { printf("  PASS: %s\n", name); passed++; } \
    else { printf("  FAIL: %s\n", name); failed++; } \
} while(0)

void test_init(void)
{
    printf("\n--- cacheInit ---\n");
    TEST("init returns 0", cacheInit() == 0);
}

void test_set_and_get(void)
{
    printf("\n--- cacheSet / cacheGet ---\n");
    TEST("set returns 0", cacheSet("spot_SE3", "{\"price\":1.05}", 60) == 0);
    TEST("set second key", cacheSet("weather", "{\"temp\":-8.1}", 60) == 0);

    const char *val = cacheGet("spot_SE3");
    TEST("get returns data", val != NULL);
    TEST("get correct data", val && strcmp(val, "{\"price\":1.05}") == 0);

    TEST("get unknown key is NULL", cacheGet("nonexistent") == NULL);
}

void test_overwrite(void)
{
    printf("\n--- overwrite ---\n");
    cacheSet("spot_SE3", "{\"price\":2.00}", 60);
    const char *val = cacheGet("spot_SE3");
    TEST("overwrite updates data", val && strcmp(val, "{\"price\":2.00}") == 0);
}

void test_is_valid(void)
{
    printf("\n--- cacheIsValid ---\n");
    TEST("existing key is valid", cacheIsValid("spot_SE3") == true);
    TEST("unknown key is invalid", cacheIsValid("nope") == false);
}

void test_invalidate(void)
{
    printf("\n--- cacheInvalidate ---\n");
    TEST("invalidate returns 0", cacheInvalidate("weather") == 0);
    TEST("invalidated key returns NULL", cacheGet("weather") == NULL);
    TEST("invalidate unknown returns -1", cacheInvalidate("nope") == -1);
}

void test_expiry(void)
{
    printf("\n--- TTL expiry ---\n");
    cacheSet("short_lived", "data", 1);
    TEST("before expiry: valid", cacheGet("short_lived") != NULL);

    printf("  (waiting 2s for expiry...)\n");
    sleep(2);

    TEST("after expiry: NULL", cacheGet("short_lived") == NULL);
}

void test_null_input(void)
{
    printf("\n--- null input ---\n");
    TEST("set null key returns -1", cacheSet(NULL, "data", 60) == -1);
    TEST("set null data returns -1", cacheSet("key", NULL, 60) == -1);
    TEST("get null returns NULL", cacheGet(NULL) == NULL);
}

void test_clear(void)
{
    printf("\n--- cacheClear ---\n");
    cacheSet("a", "1", 60);
    cacheSet("b", "2", 60);
    cacheClear();
    TEST("after clear: a is NULL", cacheGet("a") == NULL);
    TEST("after clear: b is NULL", cacheGet("b") == NULL);
}

void test_stats(void)
{
    printf("\n--- cachePrintStats ---\n");
    cachePrintStats();
    TEST("stats printed OK", 1);
}

int main(void)
{
    printf("GridGuard Cache Test\n");
    printf("====================\n");

    test_init();
    test_set_and_get();
    test_overwrite();
    test_is_valid();
    test_invalidate();
    test_expiry();
    test_null_input();
    test_clear();
    test_stats();

    cacheDestroy();

    printf("\n====================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);

    return failed > 0 ? 1 : 0;
}
