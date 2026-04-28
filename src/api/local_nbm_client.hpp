#pragma once

#include "forecast_db.hpp"
#include "gribstream_types.hpp"
#include "nbm_grid_reader.hpp"
#include "result.hpp"
#include <memory>
#include <string>

namespace predibloom::api {

// Client for reading NBM forecasts from the local database.
// Fetches from grid files and stores to SQLite for fast queries.
// Returns WeatherResponse matching GribStreamClient's interface.
class LocalNbmClient {
public:
    LocalNbmClient();
    explicit LocalNbmClient(const std::string& db_path);
    ~LocalNbmClient();

    LocalNbmClient(const LocalNbmClient&) = delete;
    LocalNbmClient& operator=(const LocalNbmClient&) = delete;

    // Forecast for a single local date.
    // timezone: IANA name (e.g., "America/New_York"). Resolves which UTC hours
    //           cover the local calendar day; handles DST automatically.
    // If asOf_iso is non-empty, restricts to forecasts issued on or before that moment
    // (ISO-8601 UTC, e.g., "2025-05-01T04:00:00Z").
    Result<WeatherResponse> getForecast(double latitude,
                                         double longitude,
                                         const std::string& date,
                                         const std::string& timezone,
                                         const std::string& asOf_iso = "");

    // Shortest-lead (near-analysis) temps for a single local date, used as actuals.
    // For local NBM, this fetches with asOf = date + 23:59 to get the most recent forecast.
    Result<WeatherResponse> getActuals(double latitude,
                                        double longitude,
                                        const std::string& date,
                                        const std::string& timezone);

    // Set the database path (for testing).
    void setDbPath(const std::string& path);

    // Set the grid base path (for testing). Pass empty to disable grid fallback.
    void setGridPath(const std::string& path);

    // Check if database is open.
    bool is_open() const;

    // No-op for interface compatibility.
    void setCaching(bool /*enabled*/) {}

private:
    // Find the best cycle for a target date given an as-of constraint.
    std::pair<std::string, int> findBestCycle(const std::string& target_date,
                                               const std::string& asOf_iso);

    // Compute forecast hours needed to cover a target date.
    std::vector<int> computeForecastHours(const std::string& cycle_date,
                                           int cycle_hour,
                                           const std::string& target_date,
                                           const std::string& timezone);

    // Fetch from grid files and store to SQLite.
    Result<WeatherResponse> fetchFromGrid(double latitude,
                                           double longitude,
                                           const std::string& target_date,
                                           const std::string& cycle_date,
                                           int cycle_hour,
                                           const std::string& timezone);

    std::unique_ptr<ForecastDb> db_;
    std::unique_ptr<NbmGridReader> grid_reader_;
};

}  // namespace predibloom::api
