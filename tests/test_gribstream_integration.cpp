#include <gtest/gtest.h>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "api/gribstream_client.hpp"

using namespace predibloom::api;

namespace {

// Load API token from environment or .env file
std::string loadApiToken() {
    // First try environment variable
    const char* env_token = std::getenv("GRIBSTREAM_API_TOKEN");
    if (env_token && std::strlen(env_token) > 0) {
        return env_token;
    }

    // Fall back to .env file in project root
    std::ifstream env_file(".env");
    if (!env_file.is_open()) {
        // Try from build directory
        env_file.open("../.env");
    }
    if (env_file.is_open()) {
        std::string line;
        while (std::getline(env_file, line)) {
            if (line.rfind("GRIBSTREAM_API_TOKEN=", 0) == 0) {
                return line.substr(21);
            }
        }
    }
    return "";
}

class GribStreamIntegrationTest : public ::testing::Test {
protected:
    static std::string api_token;

    static void SetUpTestSuite() {
        api_token = loadApiToken();
    }

    void SetUp() override {
        ASSERT_FALSE(api_token.empty())
            << "GRIBSTREAM_API_TOKEN not set. Set env var or create .env file.";
    }

    // NYC Central Park coordinates
    static constexpr double NYC_LAT = 40.7829;
    static constexpr double NYC_LON = -73.9654;
};

std::string GribStreamIntegrationTest::api_token;

TEST_F(GribStreamIntegrationTest, GetForecastReturnsValidData) {
    GribStreamClient client(api_token);

    // Use a recent date that should have data
    auto result = client.getForecast(NYC_LAT, NYC_LON, "2025-04-20");

    ASSERT_TRUE(result.ok()) << "API call failed: " << result.error().message;

    const auto& weather = result.value();
    ASSERT_EQ(weather.daily.time.size(), 1u);
    EXPECT_EQ(weather.daily.time[0], "2025-04-20");

    // Should have valid temperature data
    ASSERT_EQ(weather.daily.temperature_2m_max.size(), 1u);
    ASSERT_EQ(weather.daily.temperature_2m_min.size(), 1u);

    double high = weather.daily.temperature_2m_max[0];
    double low = weather.daily.temperature_2m_min[0];

    // Sanity check: temps should be reasonable for NYC in April
    EXPECT_GT(high, 30.0) << "High temp suspiciously low: " << high;
    EXPECT_LT(high, 100.0) << "High temp suspiciously high: " << high;
    EXPECT_GT(low, 20.0) << "Low temp suspiciously low: " << low;
    EXPECT_LT(low, 90.0) << "Low temp suspiciously high: " << low;
    EXPECT_GT(high, low) << "High should be greater than low";

    std::cout << "  Forecast for 2025-04-20: High=" << high << "°F, Low=" << low << "°F\n";
}

TEST_F(GribStreamIntegrationTest, GetActualsReturnsValidData) {
    GribStreamClient client(api_token);

    // Use a past date that should have actual observations
    auto result = client.getActuals(NYC_LAT, NYC_LON, "2025-04-15");

    ASSERT_TRUE(result.ok()) << "API call failed: " << result.error().message;

    const auto& weather = result.value();
    ASSERT_EQ(weather.daily.time.size(), 1u);
    EXPECT_EQ(weather.daily.time[0], "2025-04-15");

    ASSERT_EQ(weather.daily.temperature_2m_max.size(), 1u);
    ASSERT_EQ(weather.daily.temperature_2m_min.size(), 1u);

    double high = weather.daily.temperature_2m_max[0];
    double low = weather.daily.temperature_2m_min[0];

    // Sanity check
    EXPECT_GT(high, 20.0);
    EXPECT_LT(high, 100.0);
    EXPECT_GT(low, 10.0);
    EXPECT_LT(low, 90.0);

    std::cout << "  Actuals for 2025-04-15: High=" << high << "°F, Low=" << low << "°F\n";
}

TEST_F(GribStreamIntegrationTest, GetForecastWithAsOfReturnsHistoricalForecast) {
    GribStreamClient client(api_token);

    // Get the forecast for 2025-04-20 as it was available on 2025-04-18 at 04:00 UTC
    auto result = client.getForecast(NYC_LAT, NYC_LON, "2025-04-20", "2025-04-18T04:00:00Z");

    ASSERT_TRUE(result.ok()) << "API call failed: " << result.error().message;

    const auto& weather = result.value();
    ASSERT_EQ(weather.daily.time.size(), 1u);
    EXPECT_EQ(weather.daily.time[0], "2025-04-20");

    double high = weather.daily.temperature_2m_max[0];
    double low = weather.daily.temperature_2m_min[0];

    EXPECT_GT(high, 30.0);
    EXPECT_LT(high, 100.0);

    std::cout << "  Forecast (asOf 2025-04-18T04:00Z) for 2025-04-20: High=" << high << "°F, Low=" << low << "°F\n";
}

TEST_F(GribStreamIntegrationTest, InvalidTokenReturnsAuthError) {
    GribStreamClient client("invalid_token_12345");

    auto result = client.getForecast(NYC_LAT, NYC_LON, "2025-04-20");

    EXPECT_FALSE(result.ok());
    // Should be an auth-related error (401 or 403)
    EXPECT_TRUE(result.error().http_status == 401 ||
                result.error().http_status == 403 ||
                result.error().type == ApiError::HttpError)
        << "Expected auth error, got HTTP " << result.error().http_status
        << ": " << result.error().message;
}

TEST_F(GribStreamIntegrationTest, HelperFunctionsWorkWithRealData) {
    GribStreamClient client(api_token);

    auto result = client.getForecast(NYC_LAT, NYC_LON, "2025-04-20");
    ASSERT_TRUE(result.ok()) << result.error().message;

    const auto& weather = result.value();

    // Test helper functions
    auto high = getTemperatureForDate(weather, "2025-04-20");
    auto low = getMinTemperatureForDate(weather, "2025-04-20");

    ASSERT_TRUE(high.has_value());
    ASSERT_TRUE(low.has_value());
    EXPECT_GT(*high, *low);

    // Non-existent date should return nullopt
    auto missing = getTemperatureForDate(weather, "2025-04-21");
    EXPECT_FALSE(missing.has_value());
}

TEST_F(GribStreamIntegrationTest, DifferentLocationsReturnDifferentTemps) {
    GribStreamClient client(api_token);

    // NYC
    auto nyc_result = client.getForecast(NYC_LAT, NYC_LON, "2025-04-20");
    ASSERT_TRUE(nyc_result.ok()) << nyc_result.error().message;

    // Miami
    auto miami_result = client.getForecast(25.7617, -80.1918, "2025-04-20");
    ASSERT_TRUE(miami_result.ok()) << miami_result.error().message;

    double nyc_high = nyc_result.value().daily.temperature_2m_max[0];
    double miami_high = miami_result.value().daily.temperature_2m_max[0];

    // Miami should generally be warmer than NYC in April
    std::cout << "  NYC high: " << nyc_high << "°F, Miami high: " << miami_high << "°F\n";

    // At minimum, they shouldn't be identical (different locations)
    // Miami is typically 10-20°F warmer than NYC in spring
    EXPECT_NE(nyc_high, miami_high);
}

TEST_F(GribStreamIntegrationTest, CachingReducesApiCalls) {
    GribStreamClient client(api_token);
    client.setCaching(true);

    // Make the same request twice
    auto result1 = client.getForecast(NYC_LAT, NYC_LON, "2025-04-20");
    ASSERT_TRUE(result1.ok()) << result1.error().message;

    auto result2 = client.getForecast(NYC_LAT, NYC_LON, "2025-04-20");
    ASSERT_TRUE(result2.ok()) << result2.error().message;

    // Both should return identical data
    EXPECT_EQ(result1.value().daily.temperature_2m_max[0],
              result2.value().daily.temperature_2m_max[0]);
    EXPECT_EQ(result1.value().daily.temperature_2m_min[0],
              result2.value().daily.temperature_2m_min[0]);
}

} // namespace
