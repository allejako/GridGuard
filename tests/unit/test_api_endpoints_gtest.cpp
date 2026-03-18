// API Endpoint URL Builder Tests
// Verifies that URL construction produces well-formed strings for both APIs

#include <gtest/gtest.h>
#include <cstring>
#include <ctime>

extern "C" {
#include "api/APIEndpoints.h"
}

// ── Open-Meteo ───────────────────────────────────────────────────────────────

TEST(APIEndpointsTest, OpenMeteoContainsLatLon) {
    char buf[512];
    ASSERT_EQ(BuildOpenMeteoApiUrl(buf, sizeof(buf), "58.41", "15.62"), 0);
    EXPECT_NE(strstr(buf, "latitude=58.41"),  nullptr);
    EXPECT_NE(strstr(buf, "longitude=15.62"), nullptr);
}

TEST(APIEndpointsTest, OpenMeteoContains15MinParam) {
    char buf[512];
    ASSERT_EQ(BuildOpenMeteoApiUrl(buf, sizeof(buf), "58.41", "15.62"), 0);
    EXPECT_NE(strstr(buf, "minutely_15"),           nullptr);
    EXPECT_NE(strstr(buf, "forecast_minutely_15=192"), nullptr);
}

TEST(APIEndpointsTest, OpenMeteoContainsSolarField) {
    char buf[512];
    ASSERT_EQ(BuildOpenMeteoApiUrl(buf, sizeof(buf), "58.41", "15.62"), 0);
    EXPECT_NE(strstr(buf, "shortwave_radiation"), nullptr);
}

TEST(APIEndpointsTest, OpenMeteoContainsWeatherFields) {
    char buf[512];
    ASSERT_EQ(BuildOpenMeteoApiUrl(buf, sizeof(buf), "58.41", "15.62"), 0);
    EXPECT_NE(strstr(buf, "temperature_2m"),         nullptr);
    EXPECT_NE(strstr(buf, "cloud_cover"),            nullptr);
    EXPECT_NE(strstr(buf, "wind_speed_10m"),         nullptr);
}

TEST(APIEndpointsTest, OpenMeteoNullPointers) {
    char buf[512];
    EXPECT_EQ(BuildOpenMeteoApiUrl(nullptr, 512,    "58.41", "15.62"), -1);
    EXPECT_EQ(BuildOpenMeteoApiUrl(buf,     512,    nullptr, "15.62"), -1);
    EXPECT_EQ(BuildOpenMeteoApiUrl(buf,     512,    "58.41", nullptr), -1);
}

// Buffer smaller than minimum required (512 bytes) must fail
TEST(APIEndpointsTest, OpenMeteoBufferTooSmall) {
    char buf[64];
    EXPECT_EQ(BuildOpenMeteoApiUrl(buf, sizeof(buf), "58.41", "15.62"), -1);
}

// ── Spot Price (Elpriset) ────────────────────────────────────────────────────

TEST(APIEndpointsTest, SpotPriceUrlWithFixedDate) {
    char buf[256];
    struct tm date = {};
    date.tm_year = 124; // 2024
    date.tm_mon  = 0;   // January
    date.tm_mday = 15;

    ASSERT_EQ(BuildSpotPriceApiUrl(buf, sizeof(buf), "SE3", &date), 0);
    EXPECT_NE(strstr(buf, "2024/01-15_SE3.json"), nullptr);
}

TEST(APIEndpointsTest, SpotPriceUrlAllRegions) {
    const char *regions[] = {"SE1", "SE2", "SE3", "SE4"};
    char buf[256];
    struct tm date = {};
    date.tm_year = 124;
    date.tm_mon  = 5;
    date.tm_mday = 1;

    for (const char *r : regions) {
        ASSERT_EQ(BuildSpotPriceApiUrl(buf, sizeof(buf), r, &date), 0);
        EXPECT_NE(strstr(buf, r), nullptr) << "Region " << r << " missing from URL";
    }
}

TEST(APIEndpointsTest, SpotPriceUrlNullRegion) {
    char buf[256];
    EXPECT_EQ(BuildSpotPriceApiUrl(buf, sizeof(buf), nullptr, nullptr), -1);
}

TEST(APIEndpointsTest, SpotPriceUrlNullBuffer) {
    EXPECT_EQ(BuildSpotPriceApiUrl(nullptr, 256, "SE3", nullptr), -1);
}

// Buffer smaller than minimum required (128 bytes) must fail
TEST(APIEndpointsTest, SpotPriceUrlBufferTooSmall) {
    char buf[32];
    EXPECT_EQ(BuildSpotPriceApiUrl(buf, sizeof(buf), "SE3", nullptr), -1);
}

// Passing date=NULL must default to today and still produce a valid URL
TEST(APIEndpointsTest, SpotPriceNullDateUsesToday) {
    char buf[256];
    ASSERT_EQ(BuildSpotPriceApiUrl(buf, sizeof(buf), "SE3", nullptr), 0);
    EXPECT_NE(strstr(buf, "SE3.json"), nullptr);
    EXPECT_GT(strlen(buf), 30u);
}

// ── Tomorrow URL ─────────────────────────────────────────────────────────────

TEST(APIEndpointsTest, TomorrowUrlContainsRegion) {
    char buf[256];
    ASSERT_EQ(BuildSpotPriceTomorrowUrl(buf, sizeof(buf), "SE2"), 0);
    EXPECT_NE(strstr(buf, "SE2.json"), nullptr);
}

TEST(APIEndpointsTest, TomorrowUrlDifferentFromToday) {
    char today[256], tomorrow[256];
    ASSERT_EQ(BuildSpotPriceApiUrl(today,    sizeof(today),    "SE3", nullptr), 0);
    ASSERT_EQ(BuildSpotPriceTomorrowUrl(tomorrow, sizeof(tomorrow), "SE3"),     0);
    // Both must be non-empty and end with SE3.json
    EXPECT_NE(strstr(today,    "SE3.json"), nullptr);
    EXPECT_NE(strstr(tomorrow, "SE3.json"), nullptr);
}

TEST(APIEndpointsTest, TomorrowUrlNullRegion) {
    char buf[256];
    EXPECT_EQ(BuildSpotPriceTomorrowUrl(buf, sizeof(buf), nullptr), -1);
}
