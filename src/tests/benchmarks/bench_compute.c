/*
 * bench_compute.c — Benchmark for Compute_GenerateEnergyPlan()
 *
 * Measures min/avg/p50/p95/p99/max latency over N iterations
 * using clock_gettime(CLOCK_MONOTONIC) with nanosecond resolution.
 *
 * Build:  make bench-compute
 * Run:    ./bin/bench_compute
 */

#define _POSIX_C_SOURCE 200809L
#define _USE_MATH_DEFINES

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "compute/Compute.h"
#include "domain/Forecast.h"
#include "domain/Energy.h"
#include "sys/Logger.h"

/* Under Valgrind: färre iterationer (10–50x långsammare) */
#ifdef VALGRIND_RUN
#  define ITERATIONS      50
#  define WARMUP_ITERS     5
#else
#  define ITERATIONS   10000
#  define WARMUP_ITERS   200
#endif
#define FORECAST_HOURS  96

/* ── helpers ──────────────────────────────────────────────────────────── */

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

/* ── synthetic data ───────────────────────────────────────────────────── */

static void fill_forecast_realistic(ForecastData *f)
{
    f->count       = FORECAST_HOURS;
    f->lastUpdated = time(NULL);
    time_t base    = f->lastUpdated;

    for (int i = 0; i < FORECAST_HOURS; i++)
    {
        int hour = i % 24;
        /* Solar irradiance peaks midday */
        double solar = (hour >= 6 && hour <= 18)
            ? 800.0 * sin(M_PI * (hour - 6) / 12.0)
            : 0.0;

        f->entries[i].timestamp      = base + i * 3600;
        f->entries[i].solarIrradiance = solar;
        f->entries[i].cloudCover      = 20.0 + 40.0 * sin(i * 0.25);
        f->entries[i].temperature     = 12.0 + 8.0  * sin(i * 0.15);
        f->entries[i].windSpeed       = 3.5  + 2.0  * sin(i * 0.30);
        f->entries[i].humidity        = 65.0;
        /* Spot price: base + peak surge morning/evening */
        f->entries[i].spotPriceSek    = 0.40
            + 0.60 * ((hour >= 7 && hour <= 9) || (hour >= 17 && hour <= 20));
        f->entries[i].valid = true;
    }
}

/* fill_forecast_worst: all valid, max-spread prices — stresses qsort + loop */
static void fill_forecast_worst(ForecastData *f)
{
    f->count       = FORECAST_HOURS;
    f->lastUpdated = time(NULL);
    time_t base    = f->lastUpdated;

    for (int i = 0; i < FORECAST_HOURS; i++)
    {
        f->entries[i].timestamp       = base + i * 3600;
        f->entries[i].solarIrradiance = 500.0;
        f->entries[i].cloudCover      = 0.0;
        f->entries[i].temperature     = 25.0;
        f->entries[i].windSpeed       = 5.0;
        f->entries[i].humidity        = 50.0;
        /* Alternating prices → forces buy-window scan to cover entire array */
        f->entries[i].spotPriceSek    = (i % 2 == 0) ? 2.50 : 0.10;
        f->entries[i].valid           = true;
    }
}

/* ── print stats ──────────────────────────────────────────────────────── */

static void print_stats(const char *label, double *samples, int n)
{
    qsort(samples, n, sizeof(double), cmp_double);

    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += samples[i];
    double avg = sum / n;
    double min = samples[0];
    double max = samples[n - 1];
    double p50 = samples[(int)(n * 0.50)];
    double p95 = samples[(int)(n * 0.95)];
    double p99 = samples[(int)(n * 0.99)];

    printf("\n--- %s ---\n", label);
    printf("  iterations : %d\n", n);
    printf("  min        : %8.0f ns  (%6.2f µs)\n", min, min / 1000.0);
    printf("  avg        : %8.0f ns  (%6.2f µs)\n", avg, avg / 1000.0);
    printf("  p50        : %8.0f ns  (%6.2f µs)\n", p50, p50 / 1000.0);
    printf("  p95        : %8.0f ns  (%6.2f µs)\n", p95, p95 / 1000.0);
    printf("  p99        : %8.0f ns  (%6.2f µs)\n", p99, p99 / 1000.0);
    printf("  max        : %8.0f ns  (%6.2f µs)\n", max, max / 1000.0);
    printf("  throughput : %.0f plans/sec\n", 1e9 / avg);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    /* Suppress INFO logs — they skew timing */
    Logger_Initiate(NULL, LOG_LEVEL_ERROR);

    Compute compute;
    if (Compute_Initiate(&compute) != 0)
    {
        fprintf(stderr, "Compute_Initiate failed\n");
        return 1;
    }

    ForecastData forecast;
    EnergyData   plan;
    double      *samples = malloc(ITERATIONS * sizeof(double));
    if (!samples) { fprintf(stderr, "malloc failed\n"); return 1; }

    printf("============================================================\n");
    printf("bench_compute — Compute_GenerateEnergyPlan()\n");
    printf("============================================================\n");
    printf("forecast hours : %d\n", FORECAST_HOURS);
    printf("iterations     : %d  (warmup: %d)\n", ITERATIONS, WARMUP_ITERS);

    /* ── Scenario 1: realistic forecast ──────────────────────────────── */
    fill_forecast_realistic(&forecast);

    /* warm-up: fills branch predictor & instruction cache */
    for (int i = 0; i < WARMUP_ITERS; i++)
        Compute_GenerateEnergyPlan(&compute, &forecast,
                                   20.0, 0.20, 1.5, 0.08, 0.12, 0.20, &plan);

    for (int i = 0; i < ITERATIONS; i++)
    {
        double t0 = now_ns();
        Compute_GenerateEnergyPlan(&compute, &forecast,
                                   20.0, 0.20, 1.5, 0.08, 0.12, 0.20, &plan);
        samples[i] = now_ns() - t0;
    }
    print_stats("Scenario 1: realistic forecast (solar peaks + price surge)", samples, ITERATIONS);

    /* ── Scenario 2: worst-case (all valid, alternating prices) ─────── */
    fill_forecast_worst(&forecast);

    for (int i = 0; i < WARMUP_ITERS; i++)
        Compute_GenerateEnergyPlan(&compute, &forecast,
                                   20.0, 0.20, 1.5, 0.08, 0.12, 0.20, &plan);

    for (int i = 0; i < ITERATIONS; i++)
    {
        double t0 = now_ns();
        Compute_GenerateEnergyPlan(&compute, &forecast,
                                   20.0, 0.20, 1.5, 0.08, 0.12, 0.20, &plan);
        samples[i] = now_ns() - t0;
    }
    print_stats("Scenario 2: worst-case (max-spread prices, full array)", samples, ITERATIONS);

    /* ── Scenario 3: large solar area (more SELL decisions) ─────────── */
    fill_forecast_realistic(&forecast);

    for (int i = 0; i < WARMUP_ITERS; i++)
        Compute_GenerateEnergyPlan(&compute, &forecast,
                                   100.0, 0.22, 0.5, 0.08, 0.12, 0.20, &plan);

    for (int i = 0; i < ITERATIONS; i++)
    {
        double t0 = now_ns();
        Compute_GenerateEnergyPlan(&compute, &forecast,
                                   100.0, 0.22, 0.5, 0.08, 0.12, 0.20, &plan);
        samples[i] = now_ns() - t0;
    }
    print_stats("Scenario 3: large solar array (heavy SELL path)", samples, ITERATIONS);

    printf("\n============================================================\n");
    printf("Last plan summary (scenario 3):\n");
    printf("  entries      : %d\n", plan.count);
    printf("  totalCost    : %.4f SEK\n", plan.totalCostSek);
    printf("  gridImport   : %.4f kWh\n", plan.totalGridImportKwh);
    printf("  gridExport   : %.4f kWh\n", plan.totalGridExportKwh);
    if (plan.hasBuyWindow)
    {
        printf("  buyWindow    : %d min, avg %.4f SEK/kWh, saves %.4f SEK\n",
               plan.bestBuyWindow.durationMinutes,
               plan.bestBuyWindow.avgCostSek,
               plan.bestBuyWindow.savingsSek);
    }
    printf("============================================================\n");

    free(samples);
    Compute_Shutdown(&compute);
    Logger_Shutdown();
    return 0;
}
