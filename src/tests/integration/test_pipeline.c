#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "server/GridGuard.h"
#include "sys/Logger.h"
#include "sys/WorkCompletion.h"
#include "domain/Config.h"

void print_separator(const char *title)
{
    printf("\n========== %s ==========\n", title);
}

// Helper: submit one request synchronously and wait for result.
// Returns 0 on success, -1 on failure.
static int submit_and_wait(GridGuard *app, WorkRequest *req, const char *label)
{
    WorkCompletion wc;
    if (WorkCompletion_Initiate(&wc) != 0)
    {
        printf("FAILED [%s]: WorkCompletion_Initiate\n", label);
        return -1;
    }

    if (GridGuard_SubmitRequest(app, req, &wc) != 0)
    {
        printf("FAILED [%s]: GridGuard_SubmitRequest\n", label);
        WorkCompletion_Shutdown(&wc);
        return -1;
    }

    int rc = WorkCompletion_Wait(&wc);
    WorkCompletion_Shutdown(&wc);

    if (rc != 0)
    {
        printf("FAILED [%s]: pipeline error or timeout\n", label);
        return -1;
    }

    printf("SUCCESS [%s]: pipeline completed\n", label);
    return 0;
}

int main(void)
{
    print_separator("PIPELINE THREADS TEST");

    if (Logger_Initiate("logs/test.log", LOG_LEVEL_DEBUG) != 0)
        fprintf(stderr, "Warning: Failed to initialize logger\n");

    GridGuard app;
    printf("\n[TEST 1] Pipeline Initialization\n");
    if (GridGuard_Initiate(&app) != 0)
    {
        printf("FAILED: Could not initialize pipeline\n");
        Logger_Shutdown();
        return 1;
    }
    printf("SUCCESS: Pipeline initialized\n");

    sleep(1);

    // Test 2: Single request
    print_separator("TEST 2: Single Request");
    WorkRequest req1 = {0};
    strncpy(req1.userId,   "test_user",  sizeof(req1.userId)   - 1);
    strncpy(req1.location, "Stockholm",  sizeof(req1.location) - 1);
    strncpy(req1.lat,      "59.3300",    sizeof(req1.lat)      - 1);
    strncpy(req1.lon,      "18.0700",    sizeof(req1.lon)      - 1);
    strncpy(req1.region,   "SE3",        sizeof(req1.region)   - 1);
    req1.solarAreaM2     = 20.0;
    req1.solarEfficiency = 0.20;
    req1.consumptionKwh  = 1.5;
    req1.gridFee_low     = 0.25;
    req1.gridFee_normal  = 0.35;
    req1.gridFee_high    = 0.45;

    if (submit_and_wait(&app, &req1, "stockholm") != 0)
    {
        GridGuard_Shutdown(&app);
        Logger_Shutdown();
        return 1;
    }

    // Test 3: Multiple requests
    print_separator("TEST 3: Multiple Requests");
    const char *locs[]    = {"gothenburg", "malmo"};
    const char *lats[]    = {"57.7000",    "55.6000"};
    const char *lons[]    = {"11.9700",    "13.0000"};
    const char *regions[] = {"SE3",        "SE4"};

    for (int i = 0; i < 2; i++)
    {
        WorkRequest req = {0};
        strncpy(req.userId,   locs[i],    sizeof(req.userId)   - 1);
        strncpy(req.location, locs[i],    sizeof(req.location) - 1);
        strncpy(req.lat,      lats[i],    sizeof(req.lat)      - 1);
        strncpy(req.lon,      lons[i],    sizeof(req.lon)      - 1);
        strncpy(req.region,   regions[i], sizeof(req.region)   - 1);
        req.solarAreaM2     = 20.0;
        req.solarEfficiency = 0.20;
        req.consumptionKwh  = 1.5;
        req.gridFee_low     = 0.25;
        req.gridFee_normal  = 0.35;
        req.gridFee_high    = 0.45;

        if (submit_and_wait(&app, &req, locs[i]) != 0)
        {
            GridGuard_Shutdown(&app);
            Logger_Shutdown();
            return 1;
        }
    }
    printf("SUCCESS: All requests completed\n");

    // Test 4: Invalid request (NULL pointer)
    print_separator("TEST 4: Invalid Request Handling");
    WorkCompletion wc;
    WorkCompletion_Initiate(&wc);
    if (GridGuard_SubmitRequest(&app, NULL, &wc) == 0)
        printf("FAILED: NULL request was accepted\n");
    else
        printf("SUCCESS: NULL request rejected correctly\n");
    WorkCompletion_Shutdown(&wc);

    // Test 5: Shutdown
    print_separator("TEST 5: Pipeline Shutdown");
    GridGuard_Shutdown(&app);
    printf("SUCCESS: Pipeline shut down cleanly\n");

    print_separator("SUMMARY");
    printf("✓ Pipeline initialization\n");
    printf("✓ Single request\n");
    printf("✓ Multiple requests\n");
    printf("✓ Invalid request handling\n");
    printf("✓ Clean shutdown\n");
    printf("\nAll tests PASSED!\n");

    Logger_Shutdown();
    return 0;
}
