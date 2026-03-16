/*
 * bench_cache.c — Benchmark for SharedCache (store + lookup latency)
 *
 * Measures:
 *   1. Store latency (hit update vs. cold insert vs. LRU eviction)
 *   2. Lookup latency (cache hit vs. miss vs. expired)
 *   3. Semaphore contention under concurrent access
 *
 * Build:  make bench-cache
 * Run:    ./bin/bench_cache
 *
 * NOTE: Creates /dev/shm/bench_cache_* segments — cleaned up on exit.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "cache/SharedCache.h"
#include "sys/Logger.h"

#define STORE_ITERS   5000
#define LOOKUP_ITERS  10000
#define CONCURRENT_THREADS 8
#define CONCURRENT_OPS     50000

/* ── timer ────────────────────────────────────────────────────────────── */

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static void print_stats(const char *label, double *s, int n)
{
    qsort(s, n, sizeof(double), cmp_double);
    double sum = 0;
    for (int i = 0; i < n; i++) sum += s[i];
    double avg = sum / n;
    printf("  %-40s  avg %6.0f ns  p50 %6.0f  p95 %6.0f  p99 %6.0f  max %6.0f ns\n",
           label,
           avg,
           s[n / 2],
           s[(int)(n * 0.95)],
           s[(int)(n * 0.99)],
           s[n - 1]);
}

/* Builds a payload string of given size filled with 'x' */
static void make_payload(char *buf, size_t len)
{
    memset(buf, 'x', len - 1);
    buf[len - 1] = '\0';
}

/* ── store benchmark ──────────────────────────────────────────────────── */

static void bench_store(void)
{
    SharedCache cache;
    SharedCache_Initiate(&cache, "bench_store", 300);

    double *cold_samples   = malloc(STORE_ITERS * sizeof(double));
    double *update_samples = malloc(STORE_ITERS * sizeof(double));
    double *evict_samples  = malloc(STORE_ITERS * sizeof(double));

    /* small payload — typical key lookup */
    char small_data[256];
    make_payload(small_data, sizeof(small_data));

    /* large payload — weather JSON (32 KB) */
    char large_data[SHARED_CACHE_DATA_MAX];
    make_payload(large_data, sizeof(large_data));

    printf("\n--- Store latency ---\n");

    /* Cold insert: 16 unique keys (fills the cache) */
    for (int i = 0; i < STORE_ITERS; i++)
    {
        char key[32];
        snprintf(key, sizeof(key), "key_%04d", i % SHARED_CACHE_MAX_ENTRIES);
        double t0 = now_ns();
        SharedCache_Store(&cache, key, small_data);
        cold_samples[i] = now_ns() - t0;
    }
    print_stats("cold insert (16 keys, 256 B payload)", cold_samples, STORE_ITERS);

    /* Update existing key — sem + linear scan + in-place write */
    for (int i = 0; i < STORE_ITERS; i++)
    {
        double t0 = now_ns();
        SharedCache_Store(&cache, "key_0000", small_data);
        update_samples[i] = now_ns() - t0;
    }
    print_stats("update existing key (hit path)", update_samples, STORE_ITERS);

    /* LRU eviction: 17+ unique keys forces eviction every time */
    for (int i = 0; i < STORE_ITERS; i++)
    {
        char key[32];
        snprintf(key, sizeof(key), "evict_%06d", i);
        double t0 = now_ns();
        SharedCache_Store(&cache, key, small_data);
        evict_samples[i] = now_ns() - t0;
    }
    print_stats("LRU eviction (cache always full)", evict_samples, STORE_ITERS);

    /* Large payload store */
    double *large_samples = malloc(1000 * sizeof(double));
    for (int i = 0; i < 1000; i++)
    {
        char key[32];
        snprintf(key, sizeof(key), "large_%03d", i % SHARED_CACHE_MAX_ENTRIES);
        double t0 = now_ns();
        SharedCache_Store(&cache, key, large_data);
        large_samples[i] = now_ns() - t0;
    }
    print_stats("store large payload (32 KB)", large_samples, 1000);

    free(cold_samples); free(update_samples);
    free(evict_samples); free(large_samples);
    SharedCache_Cleanup(&cache);
}

/* ── lookup benchmark ─────────────────────────────────────────────────── */

static void bench_lookup(void)
{
    SharedCache cache;
    SharedCache_Initiate(&cache, "bench_lookup", 300);

    char small_data[256];
    make_payload(small_data, sizeof(small_data));

    /* Pre-fill 8 entries */
    for (int i = 0; i < 8; i++)
    {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i);
        SharedCache_Store(&cache, key, small_data);
    }

    double *hit_samples   = malloc(LOOKUP_ITERS * sizeof(double));
    double *miss_samples  = malloc(LOOKUP_ITERS * sizeof(double));
    char out[SHARED_CACHE_DATA_MAX];

    printf("\n--- Lookup latency ---\n");

    /* Cache hit */
    for (int i = 0; i < LOOKUP_ITERS; i++)
    {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i % 8);
        double t0 = now_ns();
        SharedCache_Lookup(&cache, key, out, sizeof(out));
        hit_samples[i] = now_ns() - t0;
    }
    print_stats("cache hit (8 entries, linear scan)", hit_samples, LOOKUP_ITERS);

    /* Cache miss (key never stored) */
    for (int i = 0; i < LOOKUP_ITERS; i++)
    {
        double t0 = now_ns();
        SharedCache_Lookup(&cache, "nonexistent_key_xyz", out, sizeof(out));
        miss_samples[i] = now_ns() - t0;
    }
    print_stats("cache miss (full scan, no match)", miss_samples, LOOKUP_ITERS);

    /* Worst-case hit: last slot (scan all 16 entries) */
    SharedCache_Cleanup(&cache);
    SharedCache_Initiate(&cache, "bench_lookup_worst", 300);
    for (int i = 0; i < SHARED_CACHE_MAX_ENTRIES; i++)
    {
        char key[32];
        snprintf(key, sizeof(key), "slot_%02d", i);
        SharedCache_Store(&cache, key, small_data);
    }
    double *worst_samples = malloc(LOOKUP_ITERS * sizeof(double));
    char last_key[32];
    snprintf(last_key, sizeof(last_key), "slot_%02d", SHARED_CACHE_MAX_ENTRIES - 1);
    for (int i = 0; i < LOOKUP_ITERS; i++)
    {
        double t0 = now_ns();
        SharedCache_Lookup(&cache, last_key, out, sizeof(out));
        worst_samples[i] = now_ns() - t0;
    }
    print_stats("worst-case hit (last of 16 slots)", worst_samples, LOOKUP_ITERS);

    free(hit_samples); free(miss_samples); free(worst_samples);
    SharedCache_Cleanup(&cache);
}

/* ── concurrent throughput ────────────────────────────────────────────── */

typedef struct
{
    SharedCache *cache;
    int          ops;
    double       elapsed_ns;
} ConcurrentArg;

static void *concurrent_lookup_fn(void *arg)
{
    ConcurrentArg *a = arg;
    char out[SHARED_CACHE_DATA_MAX];
    char key[32];
    double t0 = now_ns();
    for (int i = 0; i < a->ops; i++)
    {
        snprintf(key, sizeof(key), "key_%d", i % 8);
        SharedCache_Lookup(a->cache, key, out, sizeof(out));
    }
    a->elapsed_ns = now_ns() - t0;
    return NULL;
}

static void bench_concurrent(void)
{
    SharedCache cache;
    SharedCache_Initiate(&cache, "bench_concurrent", 300);

    char small_data[256];
    make_payload(small_data, sizeof(small_data));
    for (int i = 0; i < 8; i++)
    {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i);
        SharedCache_Store(&cache, key, small_data);
    }

    printf("\n--- Concurrent lookup throughput (%d threads, %d ops/thread) ---\n",
           CONCURRENT_THREADS, CONCURRENT_OPS / CONCURRENT_THREADS);

    pthread_t     *threads = malloc(CONCURRENT_THREADS * sizeof(pthread_t));
    ConcurrentArg *args    = malloc(CONCURRENT_THREADS * sizeof(ConcurrentArg));

    int ops_per_thread = CONCURRENT_OPS / CONCURRENT_THREADS;
    double wall_start  = now_ns();

    for (int i = 0; i < CONCURRENT_THREADS; i++)
    {
        args[i].cache = &cache;
        args[i].ops   = ops_per_thread;
        pthread_create(&threads[i], NULL, concurrent_lookup_fn, &args[i]);
    }
    for (int i = 0; i < CONCURRENT_THREADS; i++)
        pthread_join(threads[i], NULL);

    double wall_ms = (now_ns() - wall_start) / 1e6;
    double total_ops = CONCURRENT_THREADS * ops_per_thread;

    printf("  total ops  : %.0f\n", total_ops);
    printf("  wall time  : %.2f ms\n", wall_ms);
    printf("  throughput : %.0f lookups/sec\n", total_ops / (wall_ms / 1000.0));

    /* Per-thread breakdown */
    printf("  per-thread avg latency:\n");
    for (int i = 0; i < CONCURRENT_THREADS; i++)
    {
        printf("    thread %d: %.2f µs/op\n",
               i, args[i].elapsed_ns / ops_per_thread / 1000.0);
    }

    free(threads); free(args);
    SharedCache_Cleanup(&cache);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    Logger_Initiate(NULL, LOG_LEVEL_ERROR);

    printf("============================================================\n");
    printf("bench_cache — SharedCache store/lookup performance\n");
    printf("============================================================\n");
    printf("max entries    : %d\n", SHARED_CACHE_MAX_ENTRIES);
    printf("max data size  : %d bytes (%.0f KB)\n",
           SHARED_CACHE_DATA_MAX, SHARED_CACHE_DATA_MAX / 1024.0);
    printf("key max length : %d chars\n", SHARED_CACHE_KEY_MAX);

    bench_store();
    bench_lookup();
    bench_concurrent();

    printf("\n============================================================\n");

    Logger_Shutdown();
    return 0;
}
