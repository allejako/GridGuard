// ScheduleDB Tests
// Uses in-memory SQLite so no disk state persists between tests

#include <gtest/gtest.h>
#include <cstring>
#include <ctime>

extern "C" {
#include "db/ClientDB.h"
#include "db/ScheduleDB.h"
}

class ScheduleDBTest : public ::testing::Test {
protected:
    ClientDB db;

    void SetUp() override {
        ASSERT_EQ(ClientDB_Initiate(&db, ":memory:"), 0);
    }
    void TearDown() override {
        ClientDB_Shutdown(&db);
    }

    ScheduleEntry MakeEntry(const char *userId, const char *loadId, time_t start, int seq = 0) {
        ScheduleEntry e = {};
        snprintf(e.scheduleId, sizeof(e.scheduleId), "%s_%ld_%d", userId, (long)start, seq);
        strncpy(e.userId,  userId, sizeof(e.userId)  - 1);
        strncpy(e.loadId,  loadId, sizeof(e.loadId)  - 1);
        e.scheduledStart   = start;
        e.durationMinutes  = 60;
        e.powerKw          = 2.0;
        e.estimatedCostSek = 5.0;
        e.savingsSek       = 1.5;
        strncpy(e.status, "pending", sizeof(e.status) - 1);
        e.createdAt = time(NULL);
        return e;
    }
};

// Insert one entry and retrieve it back
TEST_F(ScheduleDBTest, InsertAndRetrieve) {
    ScheduleEntry entry = MakeEntry("user1", "laundry", 1672531200);
    ASSERT_EQ(ScheduleDB_Insert(&db, &entry), 0);

    ScheduleEntry out[10];
    int count = 0;
    ASSERT_EQ(ScheduleDB_GetByUser(&db, "user1", out, 10, &count), 0);
    EXPECT_EQ(count, 1);
    EXPECT_STREQ(out[0].userId, "user1");
    EXPECT_STREQ(out[0].loadId, "laundry");
    EXPECT_EQ(out[0].durationMinutes, 60);
}

// Multiple inserts for the same user must all be returned
TEST_F(ScheduleDBTest, MultipleEntriesForUser) {
    for (int i = 0; i < 3; i++) {
        ScheduleEntry e = MakeEntry("user2", "dishwasher", 1672531200 + i * 3600, i);
        ASSERT_EQ(ScheduleDB_Insert(&db, &e), 0);
    }
    ScheduleEntry out[10];
    int count = 0;
    ASSERT_EQ(ScheduleDB_GetByUser(&db, "user2", out, 10, &count), 0);
    EXPECT_EQ(count, 3);
}

// GetByUser must only return entries that belong to the queried user
TEST_F(ScheduleDBTest, GetByUserIsolation) {
    ScheduleEntry e_alice = MakeEntry("alice", "ev", 1672531200);
    ScheduleEntry e_bob   = MakeEntry("bob",   "ev", 1672531200);
    ASSERT_EQ(ScheduleDB_Insert(&db, &e_alice), 0);
    ASSERT_EQ(ScheduleDB_Insert(&db, &e_bob),   0);

    ScheduleEntry out[10];
    int count = 0;
    ASSERT_EQ(ScheduleDB_GetByUser(&db, "alice", out, 10, &count), 0);
    EXPECT_EQ(count, 1);
    EXPECT_STREQ(out[0].userId, "alice");
}

// Deleting one's own schedule must remove it from future queries
TEST_F(ScheduleDBTest, DeleteOwnSchedule) {
    ScheduleEntry entry = MakeEntry("user3", "load1", 1672531200);
    ASSERT_EQ(ScheduleDB_Insert(&db, &entry), 0);

    EXPECT_EQ(ScheduleDB_Delete(&db, entry.scheduleId, "user3"), 0);

    ScheduleEntry out[10];
    int count = 0;
    ASSERT_EQ(ScheduleDB_GetByUser(&db, "user3", out, 10, &count), 0);
    EXPECT_EQ(count, 0);
}

// A different user trying to delete another user's schedule must fail (returns != 0)
// and the entry must remain visible to the owner
TEST_F(ScheduleDBTest, DeleteWrongUserFails) {
    ScheduleEntry entry = MakeEntry("user4", "load2", 1672531200);
    ASSERT_EQ(ScheduleDB_Insert(&db, &entry), 0);

    int result = ScheduleDB_Delete(&db, entry.scheduleId, "attacker");
    EXPECT_NE(result, 0); // 1 = not found for this user

    ScheduleEntry out[10];
    int count = 0;
    ASSERT_EQ(ScheduleDB_GetByUser(&db, "user4", out, 10, &count), 0);
    EXPECT_EQ(count, 1);
}

// Deleting a schedule that does not exist must return 1 (not found)
TEST_F(ScheduleDBTest, DeleteNonExistentScheduleReturnsNotFound) {
    EXPECT_EQ(ScheduleDB_Delete(&db, "does_not_exist", "nobody"), 1);
}

// GetByUser for an unknown user on an empty DB must return 0 entries without error
TEST_F(ScheduleDBTest, GetByUserOnEmptyDatabase) {
    ScheduleEntry out[10];
    int count = 0;
    ASSERT_EQ(ScheduleDB_GetByUser(&db, "ghost_user", out, 10, &count), 0);
    EXPECT_EQ(count, 0);
}

// Numeric fields must survive a round-trip through the database
TEST_F(ScheduleDBTest, NumericFieldsRoundtrip) {
    ScheduleEntry entry = MakeEntry("user5", "ev_charger", 1672531200);
    entry.powerKw          = 3.7;
    entry.estimatedCostSek = 12.5;
    entry.savingsSek       = 4.25;
    entry.durationMinutes  = 480;
    ASSERT_EQ(ScheduleDB_Insert(&db, &entry), 0);

    ScheduleEntry out[10];
    int count = 0;
    ASSERT_EQ(ScheduleDB_GetByUser(&db, "user5", out, 10, &count), 0);
    ASSERT_EQ(count, 1);
    EXPECT_NEAR(out[0].powerKw,          3.7,   0.001);
    EXPECT_NEAR(out[0].estimatedCostSek, 12.5,  0.001);
    EXPECT_NEAR(out[0].savingsSek,       4.25,  0.001);
    EXPECT_EQ(out[0].durationMinutes, 480);
}
