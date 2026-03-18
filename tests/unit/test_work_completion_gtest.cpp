// WorkCompletion Tests
// Verifies the one-shot signal/wait synchronization primitive

#include <gtest/gtest.h>
#include <cstring>
#include <pthread.h>
#include <time.h>

extern "C" {
#include "sys/WorkCompletion.h"
}

struct SignalArgs {
    WorkCompletion *wc;
    const char     *json;
    bool            isError;
    int             delayMs;
};

static void *signal_thread(void *arg) {
    SignalArgs *a = (SignalArgs *)arg;
    struct timespec ts = {0, (long)a->delayMs * 1000000L};
    nanosleep(&ts, nullptr);
    if (a->isError)
        WorkCompletion_SignalError(a->wc);
    else
        WorkCompletion_Signal(a->wc, a->json);
    return nullptr;
}

// A successful signal from a background thread must wake Wait() with return 0
TEST(WorkCompletionTest, SignalAndWait) {
    WorkCompletion wc;
    ASSERT_EQ(WorkCompletion_Initiate(&wc), 0);

    SignalArgs args = {&wc, "{\"status\":\"ok\"}", false, 10};
    pthread_t t;
    pthread_create(&t, nullptr, signal_thread, &args);

    EXPECT_EQ(WorkCompletion_Wait(&wc), 0);
    EXPECT_STREQ(wc.json, "{\"status\":\"ok\"}");

    pthread_join(t, nullptr);
    WorkCompletion_Shutdown(&wc);
}

// An error signal must cause Wait() to return -1
TEST(WorkCompletionTest, SignalErrorReturnsMinusOne) {
    WorkCompletion wc;
    ASSERT_EQ(WorkCompletion_Initiate(&wc), 0);

    SignalArgs args = {&wc, nullptr, true, 10};
    pthread_t t;
    pthread_create(&t, nullptr, signal_thread, &args);

    EXPECT_EQ(WorkCompletion_Wait(&wc), -1);

    pthread_join(t, nullptr);
    WorkCompletion_Shutdown(&wc);
}

// Calling Signal before Wait must still return 0 (no blocking)
TEST(WorkCompletionTest, PreSignaledWaitReturnsImmediately) {
    WorkCompletion wc;
    ASSERT_EQ(WorkCompletion_Initiate(&wc), 0);

    WorkCompletion_Signal(&wc, "{\"pre\":true}");
    EXPECT_EQ(WorkCompletion_Wait(&wc), 0);
    EXPECT_STREQ(wc.json, "{\"pre\":true}");

    WorkCompletion_Shutdown(&wc);
}

// JSON content must be faithfully copied into the struct
TEST(WorkCompletionTest, JsonContentCopied) {
    WorkCompletion wc;
    ASSERT_EQ(WorkCompletion_Initiate(&wc), 0);

    const char *json = "{\"value\":42,\"unit\":\"kWh\"}";
    SignalArgs args = {&wc, json, false, 5};
    pthread_t t;
    pthread_create(&t, nullptr, signal_thread, &args);

    ASSERT_EQ(WorkCompletion_Wait(&wc), 0);
    EXPECT_STREQ(wc.json, json);

    pthread_join(t, nullptr);
    WorkCompletion_Shutdown(&wc);
}

// Null pointer inputs must not crash and return appropriate error values
TEST(WorkCompletionTest, NullPointerSafety) {
    EXPECT_EQ(WorkCompletion_Initiate(nullptr), -1);
    EXPECT_EQ(WorkCompletion_Wait(nullptr), -1);

    // These must be no-ops and not crash
    WorkCompletion_Signal(nullptr, "{}");
    WorkCompletion_SignalError(nullptr);
    WorkCompletion_Shutdown(nullptr);
}
