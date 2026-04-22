#pragma once

#include "gribstream_types.hpp"
#include "rate_limiter.hpp"
#include "result.hpp"
#include <memory>
#include <string>

namespace httplib {
class SSLClient;
}

namespace predibloom::api {

// Thin client for the GribStream /api/v2/nbm/timeseries endpoint.
// Each call fetches hourly TMP @ 2 m above ground over a single local
// America/New_York day, aggregates to daily max/min in Fahrenheit, and
// returns the familiar WeatherResponse shape.
class GribStreamClient {
public:
    explicit GribStreamClient(const std::string& api_token);
    ~GribStreamClient();

    GribStreamClient(const GribStreamClient&) = delete;
    GribStreamClient& operator=(const GribStreamClient&) = delete;

    // Forecast for a single local NY date.
    // If asOf_iso is non-empty, restricts to forecasts issued on or before that moment
    // (ISO-8601 UTC, e.g., "2025-05-01T04:00:00Z").
    Result<WeatherResponse> getForecast(double latitude,
                                         double longitude,
                                         const std::string& date,
                                         const std::string& asOf_iso = "");

    // Shortest-lead (near-analysis) temps for a single local NY date, used as actuals.
    Result<WeatherResponse> getActuals(double latitude,
                                        double longitude,
                                        const std::string& date);

    void setCaching(bool enabled) { caching_ = enabled; }

private:
    Result<WeatherResponse> fetchAggregated(double latitude,
                                             double longitude,
                                             const std::string& date,
                                             const std::string& asOf_iso,
                                             bool actuals);

    std::unique_ptr<httplib::SSLClient> client_;
    RateLimiter rate_limiter_;
    std::string api_token_;
    bool caching_ = false;
};

} // namespace predibloom::api
