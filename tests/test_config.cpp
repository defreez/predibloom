#include <gtest/gtest.h>
#include "core/config.hpp"

using namespace predibloom::core;

// --- isLowTempSeries ---

TEST(IsLowTempSeriesTest, LowTempTicker) {
    EXPECT_TRUE(isLowTempSeries("KXLOWTCHI"));
}

TEST(IsLowTempSeriesTest, HighTempTicker) {
    EXPECT_FALSE(isLowTempSeries("KXHIGHNY"));
}

TEST(IsLowTempSeriesTest, EmptyString) {
    EXPECT_FALSE(isLowTempSeries(""));
}

TEST(IsLowTempSeriesTest, PartialMatch) {
    EXPECT_FALSE(isLowTempSeries("KXLOW"));  // Missing the T
}

// --- TrackedSeries ---

TEST(TrackedSeriesTest, IsLowTemp_Delegates) {
    TrackedSeries ts;
    ts.series_ticker = "KXLOWTCHI";
    EXPECT_TRUE(ts.isLowTemp());

    ts.series_ticker = "KXHIGHNY";
    EXPECT_FALSE(ts.isLowTemp());
}

TEST(TrackedSeriesTest, EffectiveEntryHour_Default) {
    TrackedSeries ts;
    ts.entry_hour = -1;
    EXPECT_EQ(ts.effectiveEntryHour(), 4);
}

TEST(TrackedSeriesTest, EffectiveEntryHour_Custom) {
    TrackedSeries ts;
    ts.entry_hour = 8;
    EXPECT_EQ(ts.effectiveEntryHour(), 8);
}

TEST(TrackedSeriesTest, EffectiveEntryHour_Zero) {
    TrackedSeries ts;
    ts.entry_hour = 0;
    EXPECT_EQ(ts.effectiveEntryHour(), 0);
}

TEST(TrackedSeriesTest, DefaultValues) {
    TrackedSeries ts;
    EXPECT_DOUBLE_EQ(ts.offset, 2.0);
    EXPECT_EQ(ts.entry_hour, -1);
    EXPECT_DOUBLE_EQ(ts.latitude, 0.0);
    EXPECT_DOUBLE_EQ(ts.longitude, 0.0);
}

// --- Config::findSeries ---

TEST(ConfigFindSeriesTest, Found) {
    Config config;
    Tab tab;
    tab.name = "Test";
    TrackedSeries ts;
    ts.series_ticker = "KXHIGHNY";
    ts.label = "NYC";
    tab.series.push_back(ts);
    config.tabs.push_back(tab);

    const TrackedSeries* found = config.findSeries("KXHIGHNY");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->label, "NYC");
}

TEST(ConfigFindSeriesTest, NotFound) {
    Config config;
    Tab tab;
    tab.name = "Test";
    config.tabs.push_back(tab);

    EXPECT_EQ(config.findSeries("NONEXISTENT"), nullptr);
}

TEST(ConfigFindSeriesTest, EmptyConfig) {
    Config config;
    EXPECT_EQ(config.findSeries("KXHIGHNY"), nullptr);
}

TEST(ConfigFindSeriesTest, FindsInSecondTab) {
    Config config;

    Tab tab1;
    tab1.name = "Tab1";
    TrackedSeries ts1;
    ts1.series_ticker = "A";
    tab1.series.push_back(ts1);
    config.tabs.push_back(tab1);

    Tab tab2;
    tab2.name = "Tab2";
    TrackedSeries ts2;
    ts2.series_ticker = "B";
    ts2.label = "Found";
    tab2.series.push_back(ts2);
    config.tabs.push_back(tab2);

    const TrackedSeries* found = config.findSeries("B");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->label, "Found");
}

// --- Config::loadFromFile ---

// Get path to test data relative to the test binary
// CMake builds in build/, test data is in tests/test_data/
static std::string testDataPath(const std::string& filename) {
    return std::string(TEST_DATA_DIR) + "/" + filename;
}

TEST(ConfigLoadTest, ValidFile) {
    Config config = Config::loadFromFile(testDataPath("valid_config.json"));
    ASSERT_EQ(config.tabs.size(), 2u);

    // First tab
    EXPECT_EQ(config.tabs[0].name, "Climate");
    ASSERT_EQ(config.tabs[0].series.size(), 2u);

    const auto& nyc = config.tabs[0].series[0];
    EXPECT_EQ(nyc.series_ticker, "KXHIGHNY");
    EXPECT_EQ(nyc.label, "NYC High Temp");
    EXPECT_DOUBLE_EQ(nyc.latitude, 40.7128);
    EXPECT_DOUBLE_EQ(nyc.longitude, -74.006);
    EXPECT_EQ(nyc.nws_station, "KNYC");
    EXPECT_DOUBLE_EQ(nyc.offset, 2.5);
    EXPECT_EQ(nyc.entry_hour, 5);

    const auto& chi = config.tabs[0].series[1];
    EXPECT_EQ(chi.series_ticker, "KXLOWTCHI");
    EXPECT_EQ(chi.entry_hour, -1);  // Not specified, stays default

    // Second tab
    EXPECT_EQ(config.tabs[1].name, "Politics");
    ASSERT_EQ(config.tabs[1].series.size(), 1u);
}

TEST(ConfigLoadTest, MissingFile_ReturnsEmptyConfig) {
    Config config = Config::loadFromFile("/nonexistent/path/config.json");
    EXPECT_TRUE(config.tabs.empty());
}

TEST(ConfigLoadTest, FindSeriesAcrossTabs) {
    Config config = Config::loadFromFile(testDataPath("valid_config.json"));
    const TrackedSeries* found = config.findSeries("KXLOWTCHI");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->label, "Chicago Low Temp");
}
