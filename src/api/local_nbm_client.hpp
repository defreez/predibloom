#pragma once

#include "forecast_db.hpp"
#include "gribstream_types.hpp"
#include "nbm_grid_reader.hpp"
#include "result.hpp"
#include <memory>
#include <string>

namespace predibloom::api {

// Client for reading NBM forecasts from the local ForecastDb.
// Returns WeatherResponse matching GribStreamClient's interface.
// Data must be pre-populated via `nbm download` command.
class LocalNbmClient {
public:
    LocalNbmClient();
    explicit LocalNbmClient(const std::string& db_path);
    ~LocalNbmClient();

    LocalNbmClient(const LocalNbmClient&) = delete;
    LocalNbmClient& operator=(const LocalNbmClient&) = delete;

    // Forecast for a single local date.
    // utc_offset_hours: timezone offset from UTC (e.g., -5 for EST, -8 for PST)
    //                   Used to determine which hours of the day to fetch.
    // If asOf_iso is non-empty, restricts to forecasts issued on or before that moment
    // (ISO-8601 UTC, e.g., "2025-05-01T04:00:00Z").
    Result<WeatherResponse> getForecast(double latitude,
                                         double longitude,
                                         const std::string& date,
                                         int utc_offset_hours,
                                         const std::string& asOf_iso = "");

    // Shortest-lead (near-analysis) temps for a single local date, used as actuals.
    // For local NBM, this fetches with asOf = date + 23:59 to get the most recent forecast.
    Result<WeatherResponse> getActuals(double latitude,
                                        double longitude,
                                        const std::string& date,
                                        int utc_offset_hours);

    // Set the database path (for testing).
    void setDbPath(const std::string& path);

    // Set the grid base path (for testing). Pass empty to disable grid fallback.
    void setGridPath(const std::string& path);

    // Check if database is open.
    bool is_open() const;

    // No-op for interface compatibility. SQLite data is always persistent.
    void setCaching(bool /*enabled*/) {}

private:
    // Find the best cycle for a target date given an as-of constraint.
    std::pair<std::string, int> findBestCycle(const std::string& target_date,
                                               const std::string& asOf_iso);

    // Compute forecast hours needed to cover a target date.
    std::vector<int> computeForecastHours(const std::string& cycle_date,
                                           int cycle_hour,
                                           const std::string& target_date,
                                           int utc_offset_hours);

    // Fetch from grid files and cache to SQLite.
    Result<WeatherResponse> fetchFromGrid(double latitude,
                                           double longitude,
                                           const std::string& target_date,
                                           const std::string& cycle_date,
                                           int cycle_hour,
                                           int utc_offset_hours);

    std::unique_ptr<ForecastDb> db_;
    std::unique_ptr<NbmGridReader> grid_reader_;
};

}  // namespace predibloom::api
