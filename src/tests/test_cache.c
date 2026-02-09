#include "cache.h"
#include <stdio.h>
#include <unistd.h>

int main() {
    printf("Testing cache...\n\n");
    
    cacheInit();
    
    // basic test
    printf("Test 1: Basic set/get\n");
    cacheSet("test_key", "some_data", 900);
    const char *result = cacheGet("test_key");
    if (result) {
        printf("Got data: %s\n", result);
    }
    
    // test miss
    printf("\nTest 2: Cache miss\n");
    result = cacheGet("nonexistent");
    if (!result) {
        printf("Correctly returned NULL\n");
    }
    
    // test TTL
    printf("\nTest 3: TTL expiration\n");
    cacheSet("short_ttl", "expires_soon", 2);
    result = cacheGet("short_ttl");
    printf("Before expiry: %s\n", result ? "OK" : "NULL");
    
    printf("Waiting 3 seconds...\n");
    sleep(3);
    
    result = cacheGet("short_ttl");
    printf("After expiry: %s\n", result ? "OK" : "NULL");
    
    cachePrintStats();
    cacheDestroy();
    
    return 0;
}