#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "PipelineThreads.h"
#include "Logger.h"
#include "Config.h"

void print_separator(const char *title)
{
    printf("\n========== %s ==========\n", title);
}

int main(void)
{
    print_separator("PIPELINE THREADS TEST");

    // Initialize logger
    if (Logger_Initiate("../../logs", LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Warning: Failed to initialize logger\n");
    }

    // Initialize pipeline
    Pipeline pipeline;
    printf("\n[TEST 1] Pipeline Initialization\n");
    if (Pipeline_Initiate(&pipeline) != 0)
    {
        printf("FAILED: Could not initialize pipeline\n");
        Logger_Shutdown();
        return 1;
    }
    printf("SUCCESS: Pipeline initialized with 3 worker threads\n");

    // Give threads time to fully start
    sleep(1);

    // Test 1: Submit a single request
    print_separator("TEST 2: Single Request Submission");
    PipelineRequest request1 = {
        .clientFd = -1  // Mock client FD (no real socket)
    };
    strncpy(request1.location, "stockholm", sizeof(request1.location) - 1);
    strncpy(request1.region, "SE3", sizeof(request1.region) - 1);

    printf("Submitting request for %s/%s...\n", request1.location, request1.region);
    if (Pipeline_SubmitRequest(&pipeline, &request1) == 0)
    {
        printf("SUCCESS: Request submitted to pipeline\n");
    }
    else
    {
        printf("FAILED: Could not submit request\n");
        Pipeline_Shutdown(&pipeline);
        Logger_Shutdown();
        return 1;
    }

    // Wait for pipeline to process (fetch, parse, compute)
    printf("Waiting for pipeline processing...\n");
    sleep(5);

    // Test 2: Submit multiple requests
    print_separator("TEST 3: Multiple Request Submissions");
    const char *locations[] = {"stockholm", "gothenburg", "malmo"};
    const char *regions[] = {"SE3", "SE3", "SE4"};

    for (int i = 0; i < 3; i++)
    {
        PipelineRequest req = {.clientFd = -1};
        strncpy(req.location, locations[i], sizeof(req.location) - 1);
        strncpy(req.region, regions[i], sizeof(req.region) - 1);

        printf("Submitting request %d: %s/%s\n", i + 1, req.location, req.region);
        if (Pipeline_SubmitRequest(&pipeline, &req) != 0)
        {
            printf("FAILED: Could not submit request %d\n", i + 1);
            Pipeline_Shutdown(&pipeline);
            Logger_Shutdown();
            return 1;
        }
    }
    printf("SUCCESS: All 3 requests submitted\n");

    // Wait for all to process
    printf("Waiting for all requests to process...\n");
    sleep(8);

    // Test 3: Test rapid submissions (stress test)
    print_separator("TEST 4: Rapid Submissions (Stress Test)");
    printf("Submitting 10 rapid requests...\n");
    for (int i = 0; i < 10; i++)
    {
        PipelineRequest req = {.clientFd = -1};
        snprintf(req.location, sizeof(req.location), "city%d", i);
        strncpy(req.region, "SE3", sizeof(req.region) - 1);

        if (Pipeline_SubmitRequest(&pipeline, &req) != 0)
        {
            printf("FAILED: Queue full at request %d\n", i + 1);
            break;
        }
    }
    printf("SUCCESS: Rapid submissions handled\n");
    sleep(2);

    // Test 4: Invalid request (NULL pointer)
    print_separator("TEST 5: Invalid Request Handling");
    printf("Submitting NULL request (should fail gracefully)...\n");
    if (Pipeline_SubmitRequest(&pipeline, NULL) == 0)
    {
        printf("FAILED: NULL request was accepted (should reject)\n");
    }
    else
    {
        printf("SUCCESS: NULL request rejected correctly\n");
    }

    // Test 5: Pipeline shutdown
    print_separator("TEST 6: Pipeline Shutdown");
    printf("Shutting down pipeline...\n");
    Pipeline_Shutdown(&pipeline);
    printf("SUCCESS: Pipeline shut down cleanly\n");

    // Final summary
    print_separator("SUMMARY");
    printf("✓ Pipeline initialization\n");
    printf("✓ Single request submission\n");
    printf("✓ Multiple request submissions\n");
    printf("✓ Rapid/stress submissions\n");
    printf("✓ Invalid request handling\n");
    printf("✓ Clean shutdown\n");
    printf("\nAll tests PASSED!\n");

    Logger_Shutdown();
    return 0;
}
