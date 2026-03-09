/*
 * bench_queue.c — Benchmark for Queue (producer-consumer throughput)
 *
 * Tests:
 *   1. Single-threaded push+pop latency
 *   2. Multi-producer / multi-consumer throughput (configurable)
 *
 * Build:  make bench-queue
 * Run:    ./bin/bench_queue
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#include "sys/Queue.h"
#include "sys/Logger.h"

#define SINGLE_ITERS      100000
#define MULTI_OPS_TOTAL   200000
#define PAYLOAD_SIZE      sizeof(int)

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

/* ── single-thread latency test ───────────────────────────────────────── */

static void bench_single_thread(void)
{
    Queue q;
    Queue_Initiate(&q);

    double *push_samples = malloc(SINGLE_ITERS * sizeof(double));
    double *pop_samples  = malloc(SINGLE_ITERS * sizeof(double));
    if (!push_samples || !pop_samples)
    {
        fprintf(stderr, "malloc failed\n");
        return;
    }

    printf("\n--- Single-thread: push latency ---\n");
    for (int i = 0; i < SINGLE_ITERS; i++)
    {
        double t0 = now_ns();
        Queue_Push(&q, &i, PAYLOAD_SIZE, DATA_TYPE_REQUEST);
        push_samples[i] = now_ns() - t0;

        /* drain immediately to avoid blocking on full queue */
        QueueItem item;
        Queue_Pop(&q, &item);
        free(item.data);
    }

    qsort(push_samples, SINGLE_ITERS, sizeof(double), cmp_double);
    double sum = 0;
    for (int i = 0; i < SINGLE_ITERS; i++) sum += push_samples[i];

    printf("  iterations : %d\n", SINGLE_ITERS);
    printf("  avg push   : %6.0f ns  (%5.2f µs)\n", sum / SINGLE_ITERS, sum / SINGLE_ITERS / 1000.0);
    printf("  p50 push   : %6.0f ns\n", push_samples[SINGLE_ITERS / 2]);
    printf("  p95 push   : %6.0f ns\n", push_samples[(int)(SINGLE_ITERS * 0.95)]);
    printf("  p99 push   : %6.0f ns\n", push_samples[(int)(SINGLE_ITERS * 0.99)]);

    printf("\n--- Single-thread: round-trip (push+pop) latency ---\n");
    double rtt_sum = 0;
    double rtt_min = 1e18, rtt_max = 0;
    for (int i = 0; i < SINGLE_ITERS; i++)
    {
        double t0 = now_ns();
        Queue_Push(&q, &i, PAYLOAD_SIZE, DATA_TYPE_REQUEST);
        QueueItem item;
        Queue_Pop(&q, &item);
        double rtt = now_ns() - t0;
        free(item.data);
        rtt_sum += rtt;
        if (rtt < rtt_min) rtt_min = rtt;
        if (rtt > rtt_max) rtt_max = rtt;
        pop_samples[i] = rtt;
    }
    qsort(pop_samples, SINGLE_ITERS, sizeof(double), cmp_double);

    printf("  iterations : %d\n", SINGLE_ITERS);
    printf("  avg rtt    : %6.0f ns  (%5.2f µs)\n", rtt_sum / SINGLE_ITERS, rtt_sum / SINGLE_ITERS / 1000.0);
    printf("  p50 rtt    : %6.0f ns\n", pop_samples[SINGLE_ITERS / 2]);
    printf("  p95 rtt    : %6.0f ns\n", pop_samples[(int)(SINGLE_ITERS * 0.95)]);
    printf("  p99 rtt    : %6.0f ns\n", pop_samples[(int)(SINGLE_ITERS * 0.99)]);
    printf("  throughput : %.0f ops/sec\n", 1e9 / (rtt_sum / SINGLE_ITERS));

    Queue_Shutdown(&q);
    free(push_samples);
    free(pop_samples);
}

/* ── multi-thread throughput test ─────────────────────────────────────── */

typedef struct
{
    Queue        *queue;
    int           ops;         /* operations per thread */
    atomic_int   *done_count;  /* shared counter for consumers */
    int           total_ops;   /* total ops consumers should drain */
} WorkerArg;

static void *producer_fn(void *arg)
{
    WorkerArg *w = arg;
    int val = 0;
    for (int i = 0; i < w->ops; i++)
        Queue_Push(w->queue, &val, PAYLOAD_SIZE, DATA_TYPE_REQUEST);
    return NULL;
}

static void *consumer_fn(void *arg)
{
    WorkerArg *w = arg;
    QueueItem item;
    /* Queue_Pop returns -1 when queue is shut down and empty */
    while (Queue_Pop(w->queue, &item) == 0)
    {
        free(item.data);
        atomic_fetch_add(w->done_count, 1);
    }
    return NULL;
}

static void bench_multi_thread(int nproducers, int nconsumers)
{
    Queue      q;
    Queue_Initiate(&q);

    int total_produce = MULTI_OPS_TOTAL;
    int ops_per_prod  = total_produce / nproducers;

    pthread_t  *prod_threads = malloc(nproducers * sizeof(pthread_t));
    pthread_t  *cons_threads = malloc(nconsumers * sizeof(pthread_t));
    WorkerArg  *prod_args    = malloc(nproducers * sizeof(WorkerArg));
    WorkerArg  *cons_args    = malloc(nconsumers * sizeof(WorkerArg));
    atomic_int  done_count;
    atomic_init(&done_count, 0);

    double t_start = now_ns();

    for (int i = 0; i < nproducers; i++)
    {
        prod_args[i] = (WorkerArg){ &q, ops_per_prod, &done_count, total_produce };
        pthread_create(&prod_threads[i], NULL, producer_fn, &prod_args[i]);
    }
    for (int i = 0; i < nconsumers; i++)
    {
        cons_args[i] = (WorkerArg){ &q, 0, &done_count, total_produce };
        pthread_create(&cons_threads[i], NULL, consumer_fn, &cons_args[i]);
    }

    for (int i = 0; i < nproducers; i++) pthread_join(prod_threads[i], NULL);
    /* Signal consumers: no more items will arrive */
    Queue_Shutdown(&q);
    for (int i = 0; i < nconsumers; i++) pthread_join(cons_threads[i], NULL);

    double elapsed_ms = (now_ns() - t_start) / 1e6;
    double throughput = total_produce / (elapsed_ms / 1000.0);

    printf("\n--- Multi-thread: %dp / %dc (total %d ops) ---\n",
           nproducers, nconsumers, total_produce);
    printf("  elapsed    : %.2f ms\n", elapsed_ms);
    printf("  throughput : %.0f ops/sec\n", throughput);
    printf("  latency    : %.2f µs avg\n", elapsed_ms * 1000.0 / total_produce);

    free(prod_threads); free(cons_threads);
    free(prod_args);    free(cons_args);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    Logger_Initiate(NULL, LOG_LEVEL_ERROR);

    printf("============================================================\n");
    printf("bench_queue — Queue push/pop performance\n");
    printf("============================================================\n");
    printf("queue capacity : %d items\n", QUEUE_SIZE);
    printf("payload size   : %zu bytes\n", PAYLOAD_SIZE);

    bench_single_thread();

    printf("\n============================================================\n");
    printf("Multi-thread throughput (producers / consumers)\n");
    printf("============================================================\n");
    bench_multi_thread(1, 1);
    bench_multi_thread(2, 2);
    bench_multi_thread(4, 4);
    bench_multi_thread(1, 4);   /* 1 prod, 4 cons — consumers starve */
    bench_multi_thread(4, 1);   /* 4 prod, 1 cons — backpressure */

    printf("\n============================================================\n");

    Logger_Shutdown();
    return 0;
}
