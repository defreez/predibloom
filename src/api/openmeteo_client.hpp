#pragma once

#include "result.hpp"
#include "openmeteo_types.hpp"
#include "rate_limiter.hpp"
#include <memory>
#include <string>

namespace httplib {
class SSLClient;
}

namespace predibloom::api {

class OpenMeteoClient {
public:
    OpenMeteoClient();
    ~OpenMeteoClient();

    OpenMeteoClient(const OpenMeteoClient&) = delete;
    OpenMeteoClient& operator=(const OpenMeteoClient&) = delete;

    // Get actual observed weather from archive API
    // Returns daily high temperatures in Fahrenheit
    Result<WeatherResponse> getHistoricalWeather(
        double latitude,
        double longitude,
        const std::string& start_date,  // YYYY-MM-DD
        const std::string& end_date);   // YYYY-MM-DD

    // Get archived forecasts from historical forecast API
    // Shows what was predicted at the time
    Result<WeatherResponse> getHistoricalForecast(
        double latitude,
        double longitude,
        const std::string& start_date,
        const std::string& end_date);

    void setCaching(bool enabled) { caching_ = enabled; }

private:
    Result<WeatherResponse> fetchWeather(
        const std::string& host,
        double latitude,
        double longitude,
        const std::string& start_date,
        const std::string& end_date);

    std::unique_ptr<httplib::SSLClient> archive_client_;
    std::unique_ptr<httplib::SSLClient> forecast_client_;
    RateLimiter rate_limiter_;
    bool caching_ = false;
};

} // namespace predibloom::api
