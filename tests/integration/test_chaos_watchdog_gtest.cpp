// Chaos Engineering Tests: SIGKILL and Watchdog Recovery
//
// Simulates worst-case crashes — kernel killing processes (OOM, hardware fault,
// operator kill -9) — and verifies that:
//   1. SIGKILL is truly unblockable
//   2. The heartbeat pipe signals process death to the watchdog
//   3. RestartPolicy correctly records each crash and applies exponential backoff
//   4. Simultaneous kills of all three processes don't corrupt watchdog state

#include <gtest/gtest.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "watchdog/RestartPolicy.h"
#include "watchdog/Heartbeat.h"
#include "sys/Logger.h"
}

class ChaosTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger_Initiate("/dev/null", LOG_LEVEL_ERROR);
    }
    void TearDown() override {
        Logger_Shutdown();
    }

    // Fork a child that ignores SIGTERM and sleeps until killed.
    // Uses a ready-pipe to synchronize: parent waits until the child has
    // installed SIG_IGN before returning, eliminating the SIGTERM race.
    pid_t spawnSleeper() {
        int ready[2];
        if (pipe(ready) < 0) return -1;

        pid_t pid = fork();
        if (pid == 0) {
            close(ready[0]);
            signal(SIGTERM, SIG_IGN);
            // Signal parent that SIG_IGN is installed
            char b = 1;
            write(ready[1], &b, 1);
            close(ready[1]);
            pause();
            _exit(0);
        }

        // Parent waits for child to install SIG_IGN before returning
        close(ready[1]);
        char b;
        read(ready[0], &b, 1);
        close(ready[0]);
        return pid;
    }
};

// ── SIGKILL Fundamentals ─────────────────────────────────────────────────────

// SIGTERM can be ignored; SIGKILL cannot.
// This is the fundamental reason the watchdog uses SIGKILL as a last resort.
TEST_F(ChaosTest, SIGKILLCannotBeIgnoredUnlikeSIGTERM) {
    pid_t pid = spawnSleeper(); // child ignores SIGTERM
    ASSERT_GT(pid, 0);

    // SIGTERM ignored — process stays alive
    kill(pid, SIGTERM);
    usleep(50000); // 50ms
    EXPECT_EQ(kill(pid, 0), 0) << "Process died from SIGTERM despite ignoring it";

    // SIGKILL cannot be ignored
    kill(pid, SIGKILL);
    int status;
    pid_t w = waitpid(pid, &status, 0);
    EXPECT_EQ(w, pid);
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGKILL);
}

// SIGKILL is instant — the process must be gone before waitpid returns.
TEST_F(ChaosTest, SIGKILLTerminatesImmediately) {
    pid_t pid = spawnSleeper();
    ASSERT_GT(pid, 0);

    kill(pid, SIGKILL);
    int status;
    pid_t w = waitpid(pid, &status, 0);

    EXPECT_EQ(w, pid);
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGKILL);
    // After waitpid returns, process is guaranteed gone
    EXPECT_EQ(kill(pid, 0), -1);
    EXPECT_EQ(errno, ESRCH);
}

// ── Heartbeat Pipe Detects SIGKILL ───────────────────────────────────────────

// When a process is killed, the kernel auto-closes all its file descriptors.
// The heartbeat pipe write-end closing is how the watchdog detects a dead process.
TEST_F(ChaosTest, HeartbeatPipeClosedOnSIGKILL) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        Heartbeat_CloseReadFd(&hb);
        pause(); // hold write end open — simulates running process
        _exit(0);
    }

    Heartbeat_CloseWriteFd(&hb);

    // Process alive, no beat sent — timeout
    int result = Heartbeat_Check(&hb, 0);
    EXPECT_EQ(result, 0) << "Expected timeout while process is alive and silent";

    // SIGKILL: kernel closes write end
    kill(pid, SIGKILL);
    int status;
    waitpid(pid, &status, 0);

    // Now watchdog detects EOF on the pipe
    result = Heartbeat_Check(&hb, 1);
    EXPECT_LE(result, 0) << "Expected EOF/error after SIGKILL closed the pipe";

    Heartbeat_Shutdown(&hb);
}

// ── RestartPolicy Under Crash Storm ─────────────────────────────────────────

// Five consecutive SIGKILL crashes should be recorded, then rate-limit triggers.
TEST_F(ChaosTest, SIGKILLStormHitsRateLimit) {
    RestartPolicy rp;
    ASSERT_EQ(RestartPolicy_Initiate(&rp, 5, 300, 2), 0);

    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(RestartPolicy_CanRestart(&rp))
            << "Rate limit hit too early at crash " << i;

        pid_t pid = spawnSleeper();
        ASSERT_GT(pid, 0);

        kill(pid, SIGKILL);
        int status;
        waitpid(pid, &status, 0);

        EXPECT_TRUE(WIFSIGNALED(status));
        EXPECT_EQ(WTERMSIG(status), SIGKILL);
        RestartPolicy_RecordRestart(&rp);
    }

    // Max restarts reached — watchdog must stop trying
    EXPECT_FALSE(RestartPolicy_CanRestart(&rp));
    EXPECT_EQ(RestartPolicy_GetCount(&rp), 5);

    RestartPolicy_Shutdown(&rp);
}

// Each SIGKILL crash doubles the backoff delay (2→4→8→16→32).
// This prevents the watchdog from thrashing after repeated crashes.
TEST_F(ChaosTest, ExponentialBackoffAfterRepeatedSIGKILL) {
    RestartPolicy rp;
    ASSERT_EQ(RestartPolicy_Initiate(&rp, 5, 300, 2), 0);

    const int expectedBackoffs[] = {2, 4, 8, 16, 32};

    for (int i = 0; i < 5; i++) {
        pid_t pid = spawnSleeper();
        ASSERT_GT(pid, 0);

        kill(pid, SIGKILL);
        int status;
        waitpid(pid, &status, 0);

        RestartPolicy_RecordRestart(&rp);
        EXPECT_EQ(RestartPolicy_GetBackoffDelay(&rp), expectedBackoffs[i])
            << "Wrong backoff after crash " << (i + 1);
    }

    RestartPolicy_Shutdown(&rp);
}

// ── Simultaneous Kill of All Three Processes ─────────────────────────────────

// Simulates the entire GridGuard process group (server + fetcher + parser)
// being killed simultaneously. Watchdog state must not be corrupted.
TEST_F(ChaosTest, SimultaneousKillAllThreeProcesses) {
    const int N = 3;
    pid_t pids[N];
    RestartPolicy rp;
    ASSERT_EQ(RestartPolicy_Initiate(&rp, 5, 300, 2), 0);

    for (int i = 0; i < N; i++) {
        pids[i] = spawnSleeper();
        ASSERT_GT(pids[i], 0);
    }

    // Kill all three at once — no ordering, like a power cut
    for (int i = 0; i < N; i++) {
        kill(pids[i], SIGKILL);
    }

    for (int i = 0; i < N; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        EXPECT_TRUE(WIFSIGNALED(status));
        EXPECT_EQ(WTERMSIG(status), SIGKILL);
        RestartPolicy_RecordRestart(&rp);
    }

    EXPECT_EQ(RestartPolicy_GetCount(&rp), N);
    EXPECT_TRUE(RestartPolicy_CanRestart(&rp)); // 2 slots left before rate limit

    RestartPolicy_Shutdown(&rp);
}

// ── Crash → Restart → Crash cycle ────────────────────────────────────────────

// Three full crash-restart cycles. Each spawned process sends a heartbeat
// before dying, verifying the pipeline can do useful work between crashes.
TEST_F(ChaosTest, CrashRestartCycleWithHeartbeat) {
    RestartPolicy rp;
    ASSERT_EQ(RestartPolicy_Initiate(&rp, 5, 300, 2), 0);

    for (int round = 0; round < 3; round++) {
        ASSERT_TRUE(RestartPolicy_CanRestart(&rp));

        Heartbeat hb;
        ASSERT_EQ(Heartbeat_Initiate(&hb), 0);

        pid_t pid = fork();
        ASSERT_GE(pid, 0);

        if (pid == 0) {
            Heartbeat_CloseReadFd(&hb);
            char beat = 1;
            write(hb.writeFd, &beat, 1); // process is alive and working
            pause();
            _exit(0);
        }

        Heartbeat_CloseWriteFd(&hb);

        // Watchdog sees the process is healthy
        int result = Heartbeat_Check(&hb, 2);
        EXPECT_EQ(result, 1) << "Round " << round << ": no heartbeat received";

        // Something kills the process
        kill(pid, SIGKILL);
        int status;
        waitpid(pid, &status, 0);

        EXPECT_TRUE(WIFSIGNALED(status));
        EXPECT_EQ(WTERMSIG(status), SIGKILL);

        Heartbeat_Shutdown(&hb);
        RestartPolicy_RecordRestart(&rp);
    }

    EXPECT_EQ(RestartPolicy_GetCount(&rp), 3);
    RestartPolicy_Shutdown(&rp);
}
