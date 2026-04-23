#include <gtest/gtest.h>
#include <cmath>
#include "api/gribstream_types.hpp"

using namespace predibloom::api;

// --- kelvinToFahrenheit ---

TEST(KelvinToFahrenheit, FreezingPoint) {
    EXPECT_NEAR(kelvinToFahrenheit(273.15), 32.0, 1e-6);
}

TEST(KelvinToFahrenheit, BoilingPoint) {
    EXPECT_NEAR(kelvinToFahrenheit(373.15), 212.0, 1e-6);
}

TEST(KelvinToFahrenheit, TypicalNycHigh) {
    // ~22°C = 71.6°F
    EXPECT_NEAR(kelvinToFahrenheit(295.15), 71.6, 1e-6);
}

// --- parseGribstreamCsvTemps ---

TEST(ParseGribstreamCsvTemps, ParsesSingleRow) {
    // Aliased temp column is the last field.
    std::string csv =
        "forecasted_at,forecasted_time,lat,lon,name,temp\n"
        "2025-05-01T00:00:00Z,2025-05-01T01:00:00Z,40.7580,-73.9850,TimesSquare,291.6000\n";
    std::vector<double> out;
    ASSERT_TRUE(parseGribstreamCsvTemps(csv, out));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_NEAR(out[0], 291.6, 1e-6);
}

TEST(ParseGribstreamCsvTemps, ParsesMultipleRows) {
    std::string csv =
        "forecasted_at,forecasted_time,lat,lon,name,temp\n"
        "2025-05-01T00:00:00Z,2025-05-01T01:00:00Z,40.0,-74.0,Home,291.6\n"
        "2025-05-01T01:00:00Z,2025-05-01T02:00:00Z,40.0,-74.0,Home,290.1\n"
        "2025-05-01T02:00:00Z,2025-05-01T03:00:00Z,40.0,-74.0,Home,289.2\n";
    std::vector<double> out;
    ASSERT_TRUE(parseGribstreamCsvTemps(csv, out));
    ASSERT_EQ(out.size(), 3u);
    EXPECT_NEAR(out[0], 291.6, 1e-6);
    EXPECT_NEAR(out[1], 290.1, 1e-6);
    EXPECT_NEAR(out[2], 289.2, 1e-6);
}

TEST(ParseGribstreamCsvTemps, HandlesLeadingWhitespace) {
    // GribStream sometimes indents rows in its documented output.
    std::string csv =
        "forecasted_at,forecasted_time,lat,lon,name,temp\n"
        "            2025-05-01T00:00:00Z,2025-05-01T12:00:00Z,40.7580,-73.9850,TimesSquare,286.67\n";
    std::vector<double> out;
    ASSERT_TRUE(parseGribstreamCsvTemps(csv, out));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_NEAR(out[0], 286.67, 1e-6);
}

TEST(ParseGribstreamCsvTemps, SkipsEmptyLines) {
    std::string csv =
        "forecasted_at,forecasted_time,lat,lon,name,temp\n"
        "2025-05-01T00:00:00Z,2025-05-01T01:00:00Z,40.0,-74.0,Home,291.6\n"
        "\n"
        "2025-05-01T01:00:00Z,2025-05-01T02:00:00Z,40.0,-74.0,Home,290.1\n";
    std::vector<double> out;
    ASSERT_TRUE(parseGribstreamCsvTemps(csv, out));
    EXPECT_EQ(out.size(), 2u);
}

TEST(ParseGribstreamCsvTemps, HeaderOnlyReturnsEmpty) {
    std::string csv = "forecasted_at,forecasted_time,lat,lon,name,temp\n";
    std::vector<double> out;
    ASSERT_TRUE(parseGribstreamCsvTemps(csv, out));
    EXPECT_TRUE(out.empty());
}

TEST(ParseGribstreamCsvTemps, EmptyStringReturnsFalse) {
    std::string csv;
    std::vector<double> out;
    EXPECT_FALSE(parseGribstreamCsvTemps(csv, out));
}

TEST(ParseGribstreamCsvTemps, HandlesCrlfLineEndings) {
    std::string csv =
        "forecasted_at,forecasted_time,lat,lon,name,temp\r\n"
        "2025-05-01T00:00:00Z,2025-05-01T01:00:00Z,40.0,-74.0,Home,291.6\r\n";
    std::vector<double> out;
    ASSERT_TRUE(parseGribstreamCsvTemps(csv, out));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_NEAR(out[0], 291.6, 1e-6);
}

TEST(ParseGribstreamCsvTemps, ExtractsForecastedAt) {
    std::string csv =
        "forecasted_at,forecasted_time,lat,lon,name,temp\n"
        "2025-05-01T06:00:00Z,2025-05-01T12:00:00Z,40.0,-74.0,Home,291.6\n";
    std::vector<double> out;
    std::string forecasted_at;
    ASSERT_TRUE(parseGribstreamCsvTemps(csv, out, &forecasted_at));
    EXPECT_EQ(forecasted_at, "2025-05-01T06:00:00Z");
}

TEST(ParseGribstreamCsvTemps, ExtractsMostRecentForecastedAt) {
    // Multiple rows with different forecasted_at - should return most recent
    std::string csv =
        "forecasted_at,forecasted_time,lat,lon,name,temp\n"
        "2025-05-01T06:00:00Z,2025-05-01T12:00:00Z,40.0,-74.0,Home,291.6\n"
        "2025-05-01T12:00:00Z,2025-05-01T13:00:00Z,40.0,-74.0,Home,293.1\n"
        "2025-05-01T06:00:00Z,2025-05-01T14:00:00Z,40.0,-74.0,Home,294.2\n";
    std::vector<double> out;
    std::string forecasted_at;
    ASSERT_TRUE(parseGribstreamCsvTemps(csv, out, &forecasted_at));
    EXPECT_EQ(forecasted_at, "2025-05-01T12:00:00Z");  // Most recent
}

TEST(ParseGribstreamCsvTemps, NullForecastedAtPointerDoesNotCrash) {
    std::string csv =
        "forecasted_at,forecasted_time,lat,lon,name,temp\n"
        "2025-05-01T06:00:00Z,2025-05-01T12:00:00Z,40.0,-74.0,Home,291.6\n";
    std::vector<double> out;
    ASSERT_TRUE(parseGribstreamCsvTemps(csv, out, nullptr));
    EXPECT_EQ(out.size(), 1u);
}

// --- getTemperatureForDate / getMinTemperatureForDate ---

static WeatherResponse makeResponse(const std::string& date, double hi, double lo) {
    WeatherResponse r;
    r.daily.time = {date};
    r.daily.temperature_2m_max = {hi};
    r.daily.temperature_2m_min = {lo};
    return r;
}

TEST(GetTemperatureForDate, MatchingDate) {
    auto r = makeResponse("2026-04-17", 85.0, 65.0);
    auto t = getTemperatureForDate(r, "2026-04-17");
    ASSERT_TRUE(t.has_value());
    EXPECT_DOUBLE_EQ(*t, 85.0);
}

TEST(GetTemperatureForDate, MissingDate) {
    auto r = makeResponse("2026-04-17", 85.0, 65.0);
    EXPECT_FALSE(getTemperatureForDate(r, "2026-04-18").has_value());
}

TEST(GetTemperatureForDate, NanReturnsNullopt) {
    auto r = makeResponse("2026-04-17",
                          std::numeric_limits<double>::quiet_NaN(), 65.0);
    EXPECT_FALSE(getTemperatureForDate(r, "2026-04-17").has_value());
}

TEST(GetMinTemperatureForDate, MatchingDate) {
    auto r = makeResponse("2026-04-17", 85.0, 65.0);
    auto t = getMinTemperatureForDate(r, "2026-04-17");
    ASSERT_TRUE(t.has_value());
    EXPECT_DOUBLE_EQ(*t, 65.0);
}
