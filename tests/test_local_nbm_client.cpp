#include <gtest/gtest.h>
#include "api/local_nbm_client.hpp"
#include "api/weather_client.hpp"
#include "core/config.hpp"

using namespace predibloom::api;
using namespace predibloom::core;

// --- WeatherClient factory tests ---

TEST(WeatherClientFactory, CreateGribStream) {
    auto client = WeatherClient::create(WeatherSource::GribStream, "test_token");
    EXPECT_NE(client, nullptr);
}

TEST(WeatherClientFactory, CreateLocalNbm) {
    auto client = WeatherClient::create(WeatherSource::LocalNbm, "");
    EXPECT_NE(client, nullptr);
}

// --- LocalNbmClient interface tests ---
// These test the interface without requiring actual Python dependencies

TEST(LocalNbmClient, ConstructorDestructor) {
    LocalNbmClient client;
    // Should construct/destruct without issues
}

TEST(LocalNbmClient, SetScriptPath) {
    LocalNbmClient client;
    client.setScriptPath("custom/path/nbm_fetch.py");
    // No crash
}

TEST(LocalNbmClient, SetCacheDir) {
    LocalNbmClient client;
    client.setCacheDir(".cache/custom_nbm");
    // No crash
}

TEST(LocalNbmClient, SetCaching) {
    LocalNbmClient client;
    client.setCaching(false);
    client.setCaching(true);
    // No crash
}

// --- WeatherSource config parsing test ---

TEST(WeatherSourceConfig, DefaultIsGribStream) {
    TrackedSeries ts;
    EXPECT_EQ(ts.weather_source, WeatherSource::GribStream);
}
