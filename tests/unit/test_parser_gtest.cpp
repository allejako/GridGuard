// GridGuard Parser Tests
// Tests ISO 8601 timestamp parsing — critical for correct solar irradiance alignment.
// The 04:45-bug: parse_iso8601() was using mktime() (local timezone) and then
// subtracting the explicit +01:00 offset, causing a double-subtract on a
// Stockholm-timezone server. With timegm() the result is always UTC-correct.

#include <gtest/gtest.h>
#include <ctime>
#include <cstring>

extern "C" {
    #include "parser/Parser.h"
}

// Helper: build a UTC time_t from explicit calendar values
static time_t make_utc(int year, int month, int day, int hour, int min, int sec)
{
    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon  = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = min;
    tm.tm_sec  = sec;
    tm.tm_isdst = 0;
    return timegm(&tm);
}

// ── Basic parsing ────────────────────────────────────────────────────────────

// Open-Meteo with timezone=UTC returns timestamps without offset, e.g.
// "2026-03-16T13:00:00" — should be treated as UTC directly.
TEST(ParserISO8601, NaiveTimestampTreatedAsUTC)
{
    time_t result   = parse_iso8601("2026-03-16T13:00:00");
    time_t expected = make_utc(2026, 3, 16, 13, 0, 0);
    EXPECT_EQ(result, expected);
}

// Explicit Z suffix — same as naive UTC.
TEST(ParserISO8601, ZSuffixTreatedAsUTC)
{
    time_t result   = parse_iso8601("2026-03-16T13:00:00Z");
    time_t expected = make_utc(2026, 3, 16, 13, 0, 0);
    EXPECT_EQ(result, expected);
}

// Elpriset returns timestamps with +01:00 offset (CET).
// "2026-03-16T00:00:00+01:00" = 23:00 UTC the previous day.
TEST(ParserISO8601, PositiveOffsetConvertedToUTC)
{
    time_t result   = parse_iso8601("2026-03-16T00:00:00+01:00");
    time_t expected = make_utc(2026, 3, 15, 23, 0, 0);
    EXPECT_EQ(result, expected);
}

// +02:00 (CEST, summer time)
TEST(ParserISO8601, CEST_OffsetConvertedToUTC)
{
    time_t result   = parse_iso8601("2026-06-21T12:00:00+02:00");
    time_t expected = make_utc(2026, 6, 21, 10, 0, 0);
    EXPECT_EQ(result, expected);
}

// Negative offset (e.g. Americas)
TEST(ParserISO8601, NegativeOffsetConvertedToUTC)
{
    time_t result   = parse_iso8601("2026-03-16T10:00:00-05:00");
    time_t expected = make_utc(2026, 3, 16, 15, 0, 0);
    EXPECT_EQ(result, expected);
}

// ── The 04:45-bug regression ─────────────────────────────────────────────────

// Before the fix, Elpriset's "2026-03-17T14:00:00+01:00" (13:00 UTC) was
// parsed as 12:00 UTC on a Stockholm server (double-subtract of 3600s).
// Solar irradiance at 14:00 local time would then be assigned to the 12:00
// slot, appearing as a nighttime value at 04:45 after further offset errors.
TEST(ParserISO8601, Regression_NoDoubleSubtract)
{
    // 14:00 CET = 13:00 UTC
    time_t result   = parse_iso8601("2026-03-17T14:00:00+01:00");
    time_t expected = make_utc(2026, 3, 17, 13, 0, 0);
    EXPECT_EQ(result, expected);

    // Before fix this would have returned make_utc(2026,3,17,12,0,0) — 1h wrong
    time_t wrong = make_utc(2026, 3, 17, 12, 0, 0);
    EXPECT_NE(result, wrong) << "Double-subtract regression detected!";
}

// ── Midnight boundary ────────────────────────────────────────────────────────

// Midnight UTC
TEST(ParserISO8601, MidnightUTC)
{
    time_t result   = parse_iso8601("2026-03-16T00:00:00");
    time_t expected = make_utc(2026, 3, 16, 0, 0, 0);
    EXPECT_EQ(result, expected);
}

// Midnight CET crosses to previous UTC day
TEST(ParserISO8601, MidnightCET_CrossesDayBoundary)
{
    time_t result   = parse_iso8601("2026-03-16T00:00:00+01:00");
    time_t expected = make_utc(2026, 3, 15, 23, 0, 0);
    EXPECT_EQ(result, expected);

    // Must NOT equal same-day midnight UTC
    EXPECT_NE(result, make_utc(2026, 3, 16, 0, 0, 0));
}

// ── Solar irradiance alignment ───────────────────────────────────────────────

// Verify that the solar peak at 12:00 UTC stays at 12:00 UTC after parsing —
// not shifted to a nighttime slot. This is the observable symptom of the bug.
TEST(ParserISO8601, SolarPeakTimestampAligned)
{
    // Open-Meteo (timezone=UTC) solar peak
    time_t t_om = parse_iso8601("2026-03-17T12:00:00");
    // Elpriset same hour (with +01:00 offset — 13:00 CET = 12:00 UTC)
    time_t t_el = parse_iso8601("2026-03-17T13:00:00+01:00");

    // Both should resolve to 12:00 UTC
    time_t expected = make_utc(2026, 3, 17, 12, 0, 0);
    EXPECT_EQ(t_om, expected) << "Open-Meteo solar peak timestamp wrong";
    EXPECT_EQ(t_el, expected) << "Elpriset solar peak timestamp wrong";

    // And they should match each other — enabling correct data alignment
    EXPECT_EQ(t_om, t_el) << "Open-Meteo and Elpriset timestamps do not align!";
}

// Nighttime (02:00 UTC) should NOT equal a daytime slot (12:00 UTC)
TEST(ParserISO8601, NighttimeDoesNotEqualDaytime)
{
    time_t night = parse_iso8601("2026-03-17T02:00:00");
    time_t day   = parse_iso8601("2026-03-17T12:00:00");
    EXPECT_NE(night, day);
    EXPECT_LT(night, day);
}
