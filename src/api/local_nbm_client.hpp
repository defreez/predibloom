#pragma once

#include "gribstream_types.hpp"
#include "result.hpp"
#include <string>

namespace predibloom::api {

// Client for fetching NBM forecasts from NOAA S3 via the nbm_fetch.py script.
// Returns WeatherResponse matching GribStreamClient's interface.
class LocalNbmClient {
public:
    LocalNbmClient();
    ~LocalNbmClient();

    LocalNbmClient(const LocalNbmClient&) = delete;
    LocalNbmClient& operator=(const LocalNbmClient&) = delete;

    // Forecast for a single local NY date.
    // If asOf_iso is non-empty, restricts to forecasts issued on or before that moment
    // (ISO-8601 UTC, e.g., "2025-05-01T04:00:00Z").
    Result<WeatherResponse> getForecast(double latitude,
                                         double longitude,
                                         const std::string& date,
                                         const std::string& asOf_iso = "");

    // Shortest-lead (near-analysis) temps for a single local NY date, used as actuals.
    // For local NBM, this fetches with asOf = date + 23:59 to get the most recent forecast.
    Result<WeatherResponse> getActuals(double latitude,
                                        double longitude,
                                        const std::string& date);

    // Set the path to the nbm_fetch.py script (default: scripts/nbm_fetch.py)
    void setScriptPath(const std::string& path) { script_path_ = path; }

    // Set the cache directory (default: .cache/nbm)
    void setCacheDir(const std::string& dir) { cache_dir_ = dir; }

    // Enable/disable caching (default: enabled)
    void setCaching(bool enabled) { caching_ = enabled; }

private:
    Result<WeatherResponse> runScript(double latitude,
                                       double longitude,
                                       const std::string& date,
                                       const std::string& asOf_iso);

    std::string script_path_ = "scripts/nbm_fetch.py";
    std::string cache_dir_ = ".cache/nbm";
    bool caching_ = true;
};

} // namespace predibloom::api
