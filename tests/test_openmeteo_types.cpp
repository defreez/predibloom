#include <gtest/gtest.h>
#include <cmath>
#include <nlohmann/json.hpp>
#include "api/openmeteo_types.hpp"

using namespace predibloom::api;

// Helper to build a WeatherResponse for testing
static WeatherResponse makeResponse(
    const std::vector<std::string>& dates,
    const std::vector<double>& highs,
    const std::vector<double>& lows) {
    WeatherResponse r;
    r.daily.time = dates;
    r.daily.temperature_2m_max = highs;
    r.daily.temperature_2m_min = lows;
    return r;
}

// --- getTemperatureForDate ---

TEST(GetTemperatureForDateTest, ValidDate_ReturnsTemp) {
    auto r = makeResponse(
        {"2026-04-16", "2026-04-17", "2026-04-18"},
        {80.0, 85.0, 90.0},
        {60.0, 65.0, 70.0});
    auto temp = getTemperatureForDate(r, "2026-04-17");
    ASSERT_TRUE(temp.has_value());
    EXPECT_DOUBLE_EQ(temp.value(), 85.0);
}

TEST(GetTemperatureForDateTest, FirstDate) {
    auto r = makeResponse({"2026-04-16"}, {80.0}, {60.0});
    auto temp = getTemperatureForDate(r, "2026-04-16");
    ASSERT_TRUE(temp.has_value());
    EXPECT_DOUBLE_EQ(temp.value(), 80.0);
}

TEST(GetTemperatureForDateTest, LastDate) {
    auto r = makeResponse(
        {"2026-04-16", "2026-04-17", "2026-04-18"},
        {80.0, 85.0, 90.0},
        {60.0, 65.0, 70.0});
    auto temp = getTemperatureForDate(r, "2026-04-18");
    ASSERT_TRUE(temp.has_value());
    EXPECT_DOUBLE_EQ(temp.value(), 90.0);
}

TEST(GetTemperatureForDateTest, MissingDate_ReturnsNullopt) {
    auto r = makeResponse({"2026-04-16"}, {80.0}, {60.0});
    auto temp = getTemperatureForDate(r, "2026-04-20");
    EXPECT_FALSE(temp.has_value());
}

TEST(GetTemperatureForDateTest, NaNTemp_ReturnsNullopt) {
    auto r = makeResponse(
        {"2026-04-16"},
        {std::numeric_limits<double>::quiet_NaN()},
        {60.0});
    auto temp = getTemperatureForDate(r, "2026-04-16");
    EXPECT_FALSE(temp.has_value());
}

TEST(GetTemperatureForDateTest, EmptyData_ReturnsNullopt) {
    auto r = makeResponse({}, {}, {});
    auto temp = getTemperatureForDate(r, "2026-04-16");
    EXPECT_FALSE(temp.has_value());
}

// --- getMinTemperatureForDate ---

TEST(GetMinTemperatureForDateTest, ValidDate_ReturnsTemp) {
    auto r = makeResponse(
        {"2026-04-16", "2026-04-17"},
        {80.0, 85.0},
        {60.0, 65.0});
    auto temp = getMinTemperatureForDate(r, "2026-04-17");
    ASSERT_TRUE(temp.has_value());
    EXPECT_DOUBLE_EQ(temp.value(), 65.0);
}

TEST(GetMinTemperatureForDateTest, NaNTemp_ReturnsNullopt) {
    auto r = makeResponse(
        {"2026-04-16"},
        {80.0},
        {std::numeric_limits<double>::quiet_NaN()});
    auto temp = getMinTemperatureForDate(r, "2026-04-16");
    EXPECT_FALSE(temp.has_value());
}

TEST(GetMinTemperatureForDateTest, MissingDate_ReturnsNullopt) {
    auto r = makeResponse({"2026-04-16"}, {80.0}, {60.0});
    auto temp = getMinTemperatureForDate(r, "2026-04-20");
    EXPECT_FALSE(temp.has_value());
}

// --- parseTemps (via JSON deserialization) ---

TEST(ParseTempsTest, NullInJson_BecomesNaN) {
    nlohmann::json j = {
        {"time", {"2026-04-16", "2026-04-17"}},
        {"temperature_2m_max", {85.0, nullptr}},
        {"temperature_2m_min", {60.0, nullptr}}
    };

    DailyWeatherData d = j.get<DailyWeatherData>();
    EXPECT_EQ(d.temperature_2m_max.size(), 2u);
    EXPECT_DOUBLE_EQ(d.temperature_2m_max[0], 85.0);
    EXPECT_TRUE(std::isnan(d.temperature_2m_max[1]));
    EXPECT_TRUE(std::isnan(d.temperature_2m_min[1]));
}

TEST(ParseTempsTest, AllValidTemps) {
    nlohmann::json j = {
        {"time", {"2026-04-16"}},
        {"temperature_2m_max", {85.0}},
        {"temperature_2m_min", {60.0}}
    };

    DailyWeatherData d = j.get<DailyWeatherData>();
    EXPECT_DOUBLE_EQ(d.temperature_2m_max[0], 85.0);
    EXPECT_DOUBLE_EQ(d.temperature_2m_min[0], 60.0);
}

TEST(WeatherResponseJsonTest, FullResponse) {
    nlohmann::json j = {
        {"latitude", 40.7128},
        {"longitude", -74.006},
        {"timezone", "America/New_York"},
        {"daily", {
            {"time", {"2026-04-16"}},
            {"temperature_2m_max", {85.0}},
            {"temperature_2m_min", {60.0}}
        }}
    };

    WeatherResponse r = j.get<WeatherResponse>();
    EXPECT_DOUBLE_EQ(r.latitude, 40.7128);
    EXPECT_EQ(r.timezone, "America/New_York");
    EXPECT_EQ(r.daily.time.size(), 1u);
}

// --- Edge case: mismatched array sizes ---

TEST(GetTemperatureForDateTest, MismatchedArrays_SafeIteration) {
    // times has 3 entries, temps has 2 — should iterate min(3,2)=2
    WeatherResponse r;
    r.daily.time = {"2026-04-16", "2026-04-17", "2026-04-18"};
    r.daily.temperature_2m_max = {80.0, 85.0};  // one short
    r.daily.temperature_2m_min = {60.0, 65.0};

    // Date at index 2 should not be found (temps array too short)
    auto temp = getTemperatureForDate(r, "2026-04-18");
    EXPECT_FALSE(temp.has_value());

    // Date at index 1 should still work
    temp = getTemperatureForDate(r, "2026-04-17");
    ASSERT_TRUE(temp.has_value());
    EXPECT_DOUBLE_EQ(temp.value(), 85.0);
}
