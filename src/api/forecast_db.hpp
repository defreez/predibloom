#pragma once

#include <optional>
#include <string>

struct sqlite3;

namespace predibloom::api {

// Stored daily forecast record.
struct DailyForecast {
    std::string source;       // e.g., "nbm", "gribstream"
    std::string target_date;  // YYYY-MM-DD
    double latitude;
    double longitude;
    double temp_max_f;
    double temp_min_f;

    // NBM-specific fields (nullable for other sources).
    int cycle_hour = 0;          // 1, 7, 13, or 19
    std::string cycle_date;      // YYYY-MM-DD of the cycle
    int hours_fetched = 0;
};

// SQLite database for forecast data.
// Default location: ~/.cache/predibloom/forecasts.db
class ForecastDb {
public:
    ForecastDb();
    explicit ForecastDb(const std::string& db_path);
    ~ForecastDb();

    ForecastDb(const ForecastDb&) = delete;
    ForecastDb& operator=(const ForecastDb&) = delete;

    // Retrieve an NBM forecast. Returns nullopt if not found.
    // Uses lat/lon tolerance of 0.0005 degrees (~55m).
    std::optional<DailyForecast> getNbm(const std::string& target_date,
                                        int cycle_hour,
                                        double latitude,
                                        double longitude);

    // Store an NBM forecast. Overwrites existing entry if present.
    bool putNbm(const DailyForecast& forecast);

    // Check if database is open and valid.
    bool is_open() const { return db_ != nullptr; }

    // Get the database path.
    const std::string& db_path() const { return db_path_; }

private:
    void open(const std::string& path);
    void create_schema();

    sqlite3* db_ = nullptr;
    std::string db_path_;
};

}  // namespace predibloom::api
