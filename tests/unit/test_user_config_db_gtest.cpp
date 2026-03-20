// UserConfigDB Tests
// Uses in-memory SQLite so no disk state persists between tests

#include <gtest/gtest.h>
#include <cstring>
#include <ctime>

extern "C" {
#include "db/ClientDB.h"
#include "db/UserConfigDB.h"
}

class UserConfigDBTest : public ::testing::Test {
protected:
    ClientDB db;

    void SetUp() override {
        ASSERT_EQ(ClientDB_Initiate(&db, ":memory:"), 0);
    }
    void TearDown() override {
        ClientDB_Shutdown(&db);
    }

    UserConfig MakeConfig(const char *userId) {
        UserConfig c = {};
        strncpy(c.userId,   userId,      sizeof(c.userId)   - 1);
        strncpy(c.location, "Linköping", sizeof(c.location) - 1);
        c.latitude       = 58.41;
        c.longitude      = 15.62;
        strncpy(c.region, "SE3", sizeof(c.region) - 1);
        c.solarAreaM2    = 10.0;
        c.solarEfficiency= 0.18;
        c.consumptionKwh = 1.5;
        c.gridFeeLow    = 0.03;
        c.gridFeeNormal = 0.05;
        c.gridFeeHigh   = 0.10;
        c.updatedAt      = (long)time(NULL);
        return c;
    }
};

// Insert a config and retrieve it; basic scalar fields must survive the round-trip
TEST_F(UserConfigDBTest, UpsertAndGet) {
    UserConfig cfg = MakeConfig("user_a");
    ASSERT_EQ(UserConfigDB_Upsert(&db, &cfg), 0);

    UserConfig out;
    ASSERT_EQ(UserConfigDB_Get(&db, "user_a", &out), 0);
    EXPECT_STREQ(out.userId,   "user_a");
    EXPECT_STREQ(out.location, "Linköping");
    EXPECT_STREQ(out.region,   "SE3");
    EXPECT_NEAR(out.latitude,  58.41, 0.001);
    EXPECT_NEAR(out.longitude, 15.62, 0.001);
}

// Querying a user that does not exist must return 1 (not found)
TEST_F(UserConfigDBTest, GetNotFound) {
    UserConfig out;
    EXPECT_EQ(UserConfigDB_Get(&db, "no_such_user", &out), 1);
}

// A second upsert for the same user must overwrite the previous values
TEST_F(UserConfigDBTest, UpsertUpdatesExistingConfig) {
    UserConfig cfg = MakeConfig("user_b");
    ASSERT_EQ(UserConfigDB_Upsert(&db, &cfg), 0);

    cfg.solarAreaM2 = 25.0;
    ASSERT_EQ(UserConfigDB_Upsert(&db, &cfg), 0);

    UserConfig out;
    ASSERT_EQ(UserConfigDB_Get(&db, "user_b", &out), 0);
    EXPECT_NEAR(out.solarAreaM2, 25.0, 0.001);
}

// Solar panel parameters must persist with full floating-point precision
TEST_F(UserConfigDBTest, SolarParamsPersist) {
    UserConfig cfg = MakeConfig("user_c");
    cfg.solarAreaM2    = 15.5;
    cfg.solarEfficiency= 0.21;
    ASSERT_EQ(UserConfigDB_Upsert(&db, &cfg), 0);

    UserConfig out;
    ASSERT_EQ(UserConfigDB_Get(&db, "user_c", &out), 0);
    EXPECT_NEAR(out.solarAreaM2,     15.5, 0.001);
    EXPECT_NEAR(out.solarEfficiency, 0.21, 0.001);
}

// All three time-of-use grid fee bands must persist correctly
TEST_F(UserConfigDBTest, GridFeesPersist) {
    UserConfig cfg = MakeConfig("user_d");
    cfg.gridFeeLow    = 0.02;
    cfg.gridFeeNormal = 0.06;
    cfg.gridFeeHigh   = 0.12;
    ASSERT_EQ(UserConfigDB_Upsert(&db, &cfg), 0);

    UserConfig out;
    ASSERT_EQ(UserConfigDB_Get(&db, "user_d", &out), 0);
    EXPECT_NEAR(out.gridFeeLow,    0.02, 0.001);
    EXPECT_NEAR(out.gridFeeNormal, 0.06, 0.001);
    EXPECT_NEAR(out.gridFeeHigh,   0.12, 0.001);
}

// Different users must have their configs stored independently
TEST_F(UserConfigDBTest, MultipleUsersStoreIndependently) {
    UserConfig c1 = MakeConfig("stockholm");
    UserConfig c2 = MakeConfig("gothenburg");
    c1.latitude = 59.33;
    c2.latitude = 57.71;
    ASSERT_EQ(UserConfigDB_Upsert(&db, &c1), 0);
    ASSERT_EQ(UserConfigDB_Upsert(&db, &c2), 0);

    UserConfig o1, o2;
    ASSERT_EQ(UserConfigDB_Get(&db, "stockholm",  &o1), 0);
    ASSERT_EQ(UserConfigDB_Get(&db, "gothenburg", &o2), 0);
    EXPECT_NEAR(o1.latitude, 59.33, 0.001);
    EXPECT_NEAR(o2.latitude, 57.71, 0.001);
}

// consumptionKwh must survive the round-trip
TEST_F(UserConfigDBTest, ConsumptionKwhPersists) {
    UserConfig cfg = MakeConfig("user_e");
    cfg.consumptionKwh = 2.75;
    ASSERT_EQ(UserConfigDB_Upsert(&db, &cfg), 0);

    UserConfig out;
    ASSERT_EQ(UserConfigDB_Get(&db, "user_e", &out), 0);
    EXPECT_NEAR(out.consumptionKwh, 2.75, 0.001);
}
