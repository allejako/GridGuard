// RuntimeConfig Tests
// Verifies the three-level fallback chain: config file → env var → default

#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

extern "C" {
#include "config/RuntimeConfig.h"
}

class RuntimeConfigTest : public ::testing::Test {
protected:
    void TearDown() override {
        RuntimeConfig_Shutdown();
    }
};

// Without a config file, the default value must be returned
TEST_F(RuntimeConfigTest, FallsBackToDefault) {
    ASSERT_EQ(RuntimeConfig_Load(nullptr), 0);
    const char *val = RuntimeConfig_Get("nonexistent.key", nullptr, "my_default");
    EXPECT_STREQ(val, "my_default");
}

// When the env var is set and no file key exists, the env var wins over the default
TEST_F(RuntimeConfigTest, FallsBackToEnvVar) {
    setenv("GRIDGUARD_TEST_VAL", "from_env", 1);
    ASSERT_EQ(RuntimeConfig_Load(nullptr), 0);
    const char *val = RuntimeConfig_Get("nonexistent.key", "GRIDGUARD_TEST_VAL", "default");
    EXPECT_STREQ(val, "from_env");
    unsetenv("GRIDGUARD_TEST_VAL");
}

// GetInt must return the integer default when the key is not found
TEST_F(RuntimeConfigTest, GetIntDefault) {
    ASSERT_EQ(RuntimeConfig_Load(nullptr), 0);
    EXPECT_EQ(RuntimeConfig_GetInt("nonexistent.key", nullptr, 42), 42);
}

// GetInt must parse the env var as an integer
TEST_F(RuntimeConfigTest, GetIntFromEnvVar) {
    setenv("GRIDGUARD_TEST_INT", "1234", 1);
    ASSERT_EQ(RuntimeConfig_Load(nullptr), 0);
    EXPECT_EQ(RuntimeConfig_GetInt("nonexistent.key", "GRIDGUARD_TEST_INT", 0), 1234);
    unsetenv("GRIDGUARD_TEST_INT");
}

// Values from the config file must be returned correctly
TEST_F(RuntimeConfigTest, LoadFromFile) {
    const char *path = "/tmp/gg_test_rc1.conf";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fprintf(f, "[server]\nport = 7777\n");
    fclose(f);

    ASSERT_EQ(RuntimeConfig_Load(path), 0);
    EXPECT_STREQ(RuntimeConfig_Get("server.port", nullptr, "8080"), "7777");
    remove(path);
}

// GetInt must parse values from the config file
TEST_F(RuntimeConfigTest, GetIntFromFile) {
    const char *path = "/tmp/gg_test_rc2.conf";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fprintf(f, "[network]\ntimeout = 45\n");
    fclose(f);

    ASSERT_EQ(RuntimeConfig_Load(path), 0);
    EXPECT_EQ(RuntimeConfig_GetInt("network.timeout", nullptr, 30), 45);
    remove(path);
}

// Config file must take priority over env var
TEST_F(RuntimeConfigTest, ConfigFileBeatsEnvVar) {
    const char *path = "/tmp/gg_test_rc3.conf";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fprintf(f, "[server]\nport = 5555\n");
    fclose(f);

    setenv("GRIDGUARD_PORT_TEST", "6666", 1);
    ASSERT_EQ(RuntimeConfig_Load(path), 0);
    EXPECT_STREQ(RuntimeConfig_Get("server.port", "GRIDGUARD_PORT_TEST", "8080"), "5555");
    unsetenv("GRIDGUARD_PORT_TEST");
    remove(path);
}

// Reload must pick up new values written to the config file
TEST_F(RuntimeConfigTest, ReloadUpdatesValues) {
    const char *path = "/tmp/gg_test_rc4.conf";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fprintf(f, "[server]\nport = 1111\n");
    fclose(f);

    ASSERT_EQ(RuntimeConfig_Load(path), 0);
    EXPECT_STREQ(RuntimeConfig_Get("server.port", nullptr, "0"), "1111");

    f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fprintf(f, "[server]\nport = 2222\n");
    fclose(f);

    EXPECT_EQ(RuntimeConfig_Reload(), 0);
    EXPECT_STREQ(RuntimeConfig_Get("server.port", nullptr, "0"), "2222");
    remove(path);
}

// Multiple keys from different sections must all be accessible
TEST_F(RuntimeConfigTest, MultipleKeysFromMultipleSections) {
    const char *path = "/tmp/gg_test_rc5.conf";
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fprintf(f, "[server]\nport = 9090\n[database]\ndb_path = /data/test.db\n");
    fclose(f);

    ASSERT_EQ(RuntimeConfig_Load(path), 0);
    EXPECT_STREQ(RuntimeConfig_Get("server.port",    nullptr, "0"),    "9090");
    EXPECT_STREQ(RuntimeConfig_Get("database.db_path", nullptr, ""), "/data/test.db");
    remove(path);
}
