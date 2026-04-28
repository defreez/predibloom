#include "forecast_db.hpp"

#include <sqlite3.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace predibloom::api {

namespace {

std::string default_db_path() {
    const char* home = std::getenv("HOME");
    if (!home) {
        throw std::runtime_error("HOME environment variable not set");
    }
    return std::string(home) + "/.local/share/predibloom/predibloom.db";
}

}  // namespace

ForecastDb::ForecastDb() {
    open(default_db_path());
}

ForecastDb::ForecastDb(const std::string& db_path) {
    open(db_path);
}

ForecastDb::~ForecastDb() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void ForecastDb::open(const std::string& path) {
    db_path_ = path;

    // Create parent directories if needed.
    std::filesystem::path p(path);
    std::filesystem::create_directories(p.parent_path());

    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "unknown error";
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error("Failed to open database: " + err);
    }

    create_schema();
}

void ForecastDb::create_schema() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS nbm_forecasts (
            target_date   TEXT NOT NULL,
            cycle_hour    INTEGER NOT NULL,
            latitude      REAL NOT NULL,
            longitude     REAL NOT NULL,
            temp_max_f    REAL NOT NULL,
            temp_min_f    REAL NOT NULL,
            cycle_date    TEXT NOT NULL,
            hours_fetched INTEGER NOT NULL,
            time_of_max   TEXT,
            time_of_min   TEXT,
            created_at    TEXT DEFAULT (datetime('now')),
            PRIMARY KEY (target_date, cycle_hour, latitude, longitude)
        );
    )";

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "unknown error";
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to create schema: " + err);
    }

    // Add columns if they don't exist (for existing databases)
    sqlite3_exec(db_, "ALTER TABLE nbm_forecasts ADD COLUMN time_of_max TEXT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "ALTER TABLE nbm_forecasts ADD COLUMN time_of_min TEXT;", nullptr, nullptr, nullptr);

    // Purge rows from prior versions that accepted partial-coverage cycles.
    // Anything under 24 hours can have the daily extreme in the missing
    // window — better to refetch from grid files than serve a wrong row.
    sqlite3_exec(db_, "DELETE FROM nbm_forecasts WHERE hours_fetched < 24;",
                 nullptr, nullptr, nullptr);
}

std::optional<DailyForecast> ForecastDb::getNbm(const std::string& target_date,
                                                 int cycle_hour,
                                                 double latitude,
                                                 double longitude) {
    if (!db_) return std::nullopt;

    // Use tolerance of 0.0005 degrees (~55m).
    const char* sql = R"(
        SELECT target_date, cycle_hour, latitude, longitude,
               temp_max_f, temp_min_f, cycle_date, hours_fetched,
               time_of_max, time_of_min
        FROM nbm_forecasts
        WHERE target_date = ?
          AND cycle_hour = ?
          AND ABS(latitude - ?) < 0.0005
          AND ABS(longitude - ?) < 0.0005
        LIMIT 1;
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, target_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, cycle_hour);
    sqlite3_bind_double(stmt, 3, latitude);
    sqlite3_bind_double(stmt, 4, longitude);

    std::optional<DailyForecast> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        DailyForecast f;
        f.source = "nbm";
        f.target_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        f.cycle_hour = sqlite3_column_int(stmt, 1);
        f.latitude = sqlite3_column_double(stmt, 2);
        f.longitude = sqlite3_column_double(stmt, 3);
        f.temp_max_f = sqlite3_column_double(stmt, 4);
        f.temp_min_f = sqlite3_column_double(stmt, 5);
        f.cycle_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        f.hours_fetched = sqlite3_column_int(stmt, 7);
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
            f.time_of_max = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        }
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) {
            f.time_of_min = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        }
        result = f;
    }

    sqlite3_finalize(stmt);
    return result;
}

bool ForecastDb::putNbm(const DailyForecast& forecast) {
    if (!db_) return false;

    const char* sql = R"(
        INSERT OR REPLACE INTO nbm_forecasts
            (target_date, cycle_hour, latitude, longitude,
             temp_max_f, temp_min_f, cycle_date, hours_fetched,
             time_of_max, time_of_min)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, forecast.target_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, forecast.cycle_hour);
    sqlite3_bind_double(stmt, 3, forecast.latitude);
    sqlite3_bind_double(stmt, 4, forecast.longitude);
    sqlite3_bind_double(stmt, 5, forecast.temp_max_f);
    sqlite3_bind_double(stmt, 6, forecast.temp_min_f);
    sqlite3_bind_text(stmt, 7, forecast.cycle_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, forecast.hours_fetched);
    if (forecast.time_of_max.empty()) {
        sqlite3_bind_null(stmt, 9);
    } else {
        sqlite3_bind_text(stmt, 9, forecast.time_of_max.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (forecast.time_of_min.empty()) {
        sqlite3_bind_null(stmt, 10);
    } else {
        sqlite3_bind_text(stmt, 10, forecast.time_of_min.c_str(), -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

}  // namespace predibloom::api
