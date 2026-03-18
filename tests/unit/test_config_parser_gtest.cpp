#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
    #include "config/ConfigParser.h"
}

class ConfigParserTest : public ::testing::Test {
protected:
    const char* testConfigPath = "/tmp/gridguard_test.conf";
    ConfigParser parser;

    void SetUp() override {
        std::remove(testConfigPath);
        std::memset(&parser, 0, sizeof(parser));
    }

    void TearDown() override {
        ConfigParser_Shutdown(&parser);
        std::remove(testConfigPath);
    }

    void CreateTestConfig() {
        FILE* f = std::fopen(testConfigPath, "w");
        ASSERT_NE(f, nullptr);

        std::fprintf(f, "# GridGuard test configuration\n");
        std::fprintf(f, "\n");
        std::fprintf(f, "[server]\n");
        std::fprintf(f, "port=9090\n");
        std::fprintf(f, "host=0.0.0.0\n");
        std::fprintf(f, "log_level=DEBUG\n");
        std::fprintf(f, "\n");
        std::fprintf(f, "[database]\n");
        std::fprintf(f, "db_path=/var/lib/gridguard.db\n");
        std::fprintf(f, "platform_db_path=/var/lib/platform.db\n");
        std::fprintf(f, "\n");
        std::fprintf(f, "[jwt]\n");
        std::fprintf(f, "jwt_secret=test_secret_key_123\n");
        std::fprintf(f, "\n");
        std::fprintf(f, "[network]\n");
        std::fprintf(f, "timeout=60\n");
        std::fprintf(f, "max_connections=200\n");
        std::fprintf(f, "\n");
        std::fprintf(f, "[cache]\n");
        std::fprintf(f, "weather_ttl=1800\n");
        std::fprintf(f, "price_ttl=7200\n");
        std::fprintf(f, "forecast_ttl=3600\n");

        std::fclose(f);
    }
};

TEST_F(ConfigParserTest, InitiateSuccess) {
    int result = ConfigParser_Initiate(&parser);
    EXPECT_EQ(result, 0);
    EXPECT_NE(parser.entries, nullptr);
    EXPECT_EQ(parser.count, 0);
    EXPECT_GT(parser.capacity, 0);
}

TEST_F(ConfigParserTest, ParseFileNotFound) {
    ASSERT_EQ(ConfigParser_Initiate(&parser), 0);
    int result = ConfigParser_ParseFile(&parser, "/nonexistent/path.conf");
    EXPECT_EQ(result, -1);
}

TEST_F(ConfigParserTest, ParseFileSuccess) {
    CreateTestConfig();
    ASSERT_EQ(ConfigParser_Initiate(&parser), 0);

    int result = ConfigParser_ParseFile(&parser, testConfigPath);
    EXPECT_EQ(result, 0);
    EXPECT_GT(parser.count, 0);
}

TEST_F(ConfigParserTest, GetServerConfig) {
    CreateTestConfig();
    ConfigParser_Initiate(&parser);
    ConfigParser_ParseFile(&parser, testConfigPath);

    const char* port = ConfigParser_Get(&parser, "server.port");
    ASSERT_NE(port, nullptr);
    EXPECT_STREQ(port, "9090");

    const char* host = ConfigParser_Get(&parser, "server.host");
    ASSERT_NE(host, nullptr);
    EXPECT_STREQ(host, "0.0.0.0");

    const char* logLevel = ConfigParser_Get(&parser, "server.log_level");
    ASSERT_NE(logLevel, nullptr);
    EXPECT_STREQ(logLevel, "DEBUG");
}

TEST_F(ConfigParserTest, GetDatabaseConfig) {
    CreateTestConfig();
    ConfigParser_Initiate(&parser);
    ConfigParser_ParseFile(&parser, testConfigPath);

    const char* dbPath = ConfigParser_Get(&parser, "database.db_path");
    ASSERT_NE(dbPath, nullptr);
    EXPECT_STREQ(dbPath, "/var/lib/gridguard.db");

    const char* platformPath = ConfigParser_Get(&parser, "database.platform_db_path");
    ASSERT_NE(platformPath, nullptr);
    EXPECT_STREQ(platformPath, "/var/lib/platform.db");
}

TEST_F(ConfigParserTest, GetJWTConfig) {
    CreateTestConfig();
    ConfigParser_Initiate(&parser);
    ConfigParser_ParseFile(&parser, testConfigPath);

    const char* secret = ConfigParser_Get(&parser, "jwt.jwt_secret");
    ASSERT_NE(secret, nullptr);
    EXPECT_STREQ(secret, "test_secret_key_123");
}

TEST_F(ConfigParserTest, GetNetworkConfig) {
    CreateTestConfig();
    ConfigParser_Initiate(&parser);
    ConfigParser_ParseFile(&parser, testConfigPath);

    const char* timeout = ConfigParser_Get(&parser, "network.timeout");
    ASSERT_NE(timeout, nullptr);
    EXPECT_STREQ(timeout, "60");

    const char* maxConn = ConfigParser_Get(&parser, "network.max_connections");
    ASSERT_NE(maxConn, nullptr);
    EXPECT_STREQ(maxConn, "200");
}

TEST_F(ConfigParserTest, GetCacheConfig) {
    CreateTestConfig();
    ConfigParser_Initiate(&parser);
    ConfigParser_ParseFile(&parser, testConfigPath);

    const char* weatherTTL = ConfigParser_Get(&parser, "cache.weather_ttl");
    ASSERT_NE(weatherTTL, nullptr);
    EXPECT_STREQ(weatherTTL, "1800");

    const char* priceTTL = ConfigParser_Get(&parser, "cache.price_ttl");
    ASSERT_NE(priceTTL, nullptr);
    EXPECT_STREQ(priceTTL, "7200");

    const char* forecastTTL = ConfigParser_Get(&parser, "cache.forecast_ttl");
    ASSERT_NE(forecastTTL, nullptr);
    EXPECT_STREQ(forecastTTL, "3600");
}

TEST_F(ConfigParserTest, GetNonexistentKey) {
    CreateTestConfig();
    ConfigParser_Initiate(&parser);
    ConfigParser_ParseFile(&parser, testConfigPath);

    const char* value = ConfigParser_Get(&parser, "nonexistent.key");
    EXPECT_EQ(value, nullptr);
}

TEST_F(ConfigParserTest, GetOrDefaultExistingKey) {
    CreateTestConfig();
    ConfigParser_Initiate(&parser);
    ConfigParser_ParseFile(&parser, testConfigPath);

    const char* port = ConfigParser_GetOrDefault(&parser, "server.port", "8080");
    EXPECT_STREQ(port, "9090");
}

TEST_F(ConfigParserTest, GetOrDefaultNonexistentKey) {
    CreateTestConfig();
    ConfigParser_Initiate(&parser);
    ConfigParser_ParseFile(&parser, testConfigPath);

    const char* timeout = ConfigParser_GetOrDefault(&parser, "server.timeout", "30");
    EXPECT_STREQ(timeout, "30");
}

TEST_F(ConfigParserTest, ParseCommentsAndEmptyLines) {
    FILE* f = std::fopen(testConfigPath, "w");
    ASSERT_NE(f, nullptr);
    std::fprintf(f, "# Comment line\n");
    std::fprintf(f, "\n");
    std::fprintf(f, "[section]\n");
    std::fprintf(f, "# Another comment\n");
    std::fprintf(f, "key=value\n");
    std::fprintf(f, "\n");
    std::fclose(f);

    ConfigParser_Initiate(&parser);
    ConfigParser_ParseFile(&parser, testConfigPath);

    EXPECT_EQ(parser.count, 1);
    const char* value = ConfigParser_Get(&parser, "section.key");
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "value");
}

TEST_F(ConfigParserTest, ParseTrimWhitespace) {
    FILE* f = std::fopen(testConfigPath, "w");
    ASSERT_NE(f, nullptr);
    std::fprintf(f, "[section]\n");
    std::fprintf(f, "  key1  =  value1  \n");
    std::fprintf(f, "key2=value2\n");
    std::fclose(f);

    ConfigParser_Initiate(&parser);
    ConfigParser_ParseFile(&parser, testConfigPath);

    const char* value1 = ConfigParser_Get(&parser, "section.key1");
    ASSERT_NE(value1, nullptr);
    EXPECT_STREQ(value1, "value1");

    const char* value2 = ConfigParser_Get(&parser, "section.key2");
    ASSERT_NE(value2, nullptr);
    EXPECT_STREQ(value2, "value2");
}
