// Integration Tests: Multi-Process Pipeline
//
// Tests the full watchdog → server → fetcher → parser architecture at the
// process level. Verifies that processes can be spawned, communicate via
// heartbeat pipes, and are detected as dead when killed.
//
// Unlike the unit-level test_pipeline which tests the in-process ThreadPool
// pipeline, these tests exercise the actual fork()/exec() boundary and IPC.

#include <gtest/gtest.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>

extern "C" {
#include "watchdog/Heartbeat.h"
#include "watchdog/RestartPolicy.h"
#include "sys/Logger.h"
}

class ProcessPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger_Initiate("/dev/null", LOG_LEVEL_ERROR);
    }
    void TearDown() override {
        Logger_Shutdown();
    }
};

// ── Heartbeat IPC ────────────────────────────────────────────────────────────

// Child sends one heartbeat beat → parent Heartbeat_Check returns 1.
TEST_F(ProcessPipelineTest, HeartbeatReceivedFromChild) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        Heartbeat_CloseReadFd(&hb);
        char beat = 1;
        write(hb.writeFd, &beat, 1);
        _exit(0);
    }

    Heartbeat_CloseWriteFd(&hb);
    int result = Heartbeat_Check(&hb, 2);
    EXPECT_EQ(result, 1) << "Expected heartbeat beat from child";

    int status;
    waitpid(pid, &status, 0);
    Heartbeat_Shutdown(&hb);
}

// Child exits without writing → write end auto-closed by kernel → parent
// Heartbeat_Check returns ≤0, which is how the watchdog detects a dead process.
TEST_F(ProcessPipelineTest, HeartbeatDetectsDeadProcess) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        Heartbeat_CloseReadFd(&hb);
        // Exit immediately without writing — kernel closes writeFd
        _exit(0);
    }

    Heartbeat_CloseWriteFd(&hb);

    // Give child time to exit so the pipe write-end is closed
    int status;
    waitpid(pid, &status, 0);

    int result = Heartbeat_Check(&hb, 1);
    EXPECT_LE(result, 0) << "Expected EOF/error when child pipe is closed";

    Heartbeat_Shutdown(&hb);
}

// Child holds pipe open (simulates running process) but sends no beat.
// Heartbeat_Check with timeout=0 should return 0 (timeout, not dead).
TEST_F(ProcessPipelineTest, HeartbeatTimeoutWhenProcessAliveButSilent) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        Heartbeat_CloseReadFd(&hb);
        // Hold writeFd open but don't write — simulates a frozen process
        pause();
        _exit(0);
    }

    Heartbeat_CloseWriteFd(&hb);

    int result = Heartbeat_Check(&hb, 0); // zero-second timeout
    EXPECT_EQ(result, 0) << "Expected timeout, not EOF, while process is alive";

    kill(pid, SIGKILL);
    int status;
    waitpid(pid, &status, 0);
    Heartbeat_Shutdown(&hb);
}

// ── Process Lifecycle ────────────────────────────────────────────────────────

// Spawn N children that each exit cleanly. waitpid() on all must succeed.
TEST_F(ProcessPipelineTest, SpawnAndWaitMultipleProcesses) {
    const int N = 3; // server, fetcher, parser
    pid_t pids[N];

    for (int i = 0; i < N; i++) {
        pids[i] = fork();
        ASSERT_GE(pids[i], 0);
        if (pids[i] == 0) {
            usleep(5000 * (i + 1)); // stagger exits slightly
            _exit(0);
        }
    }

    for (int i = 0; i < N; i++) {
        int status;
        pid_t w = waitpid(pids[i], &status, 0);
        EXPECT_EQ(w, pids[i]);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

// ── Kill → Restart Cycle ─────────────────────────────────────────────────────

// Simulate three crash → restart cycles using SIGTERM.
// RestartPolicy must allow each restart and record it correctly.
TEST_F(ProcessPipelineTest, KillAndRestartCycleWithRestartPolicy) {
    RestartPolicy rp;
    ASSERT_EQ(RestartPolicy_Initiate(&rp, 5, 300, 2), 0);

    for (int round = 0; round < 3; round++) {
        ASSERT_TRUE(RestartPolicy_CanRestart(&rp))
            << "RestartPolicy blocked restart at round " << round;

        pid_t pid = fork();
        ASSERT_GE(pid, 0);
        if (pid == 0) {
            pause(); // wait to be killed
            _exit(0);
        }

        kill(pid, SIGTERM);
        int status;
        waitpid(pid, &status, 0);

        EXPECT_TRUE(WIFSIGNALED(status) || WIFEXITED(status));
        RestartPolicy_RecordRestart(&rp);
    }

    EXPECT_EQ(RestartPolicy_GetCount(&rp), 3);
    RestartPolicy_Shutdown(&rp);
}

// ── Pipeline Heartbeat Integration ───────────────────────────────────────────

// Simulates the watchdog monitoring three child processes via heartbeat pipes.
// All children send one beat; watchdog verifies all three.
TEST_F(ProcessPipelineTest, WatchdogMonitorsThreeProcessesViaHeartbeat) {
    const int N = 3;
    Heartbeat hb[N];
    pid_t     pids[N];

    for (int i = 0; i < N; i++) {
        ASSERT_EQ(Heartbeat_Initiate(&hb[i]), 0);

        pids[i] = fork();
        ASSERT_GE(pids[i], 0);

        if (pids[i] == 0) {
            // Child: close read end, send one beat, exit
            Heartbeat_CloseReadFd(&hb[i]);
            char beat = 1;
            write(hb[i].writeFd, &beat, 1);
            _exit(0);
        }

        // Parent: close write end for this child
        Heartbeat_CloseWriteFd(&hb[i]);
    }

    // Watchdog checks all three
    for (int i = 0; i < N; i++) {
        int result = Heartbeat_Check(&hb[i], 2);
        EXPECT_EQ(result, 1) << "Process " << i << " did not send heartbeat";

        int status;
        waitpid(pids[i], &status, 0);
        Heartbeat_Shutdown(&hb[i]);
    }
}
