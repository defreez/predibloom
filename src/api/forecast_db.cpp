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
    return std::string(home) + "/.cache/predibloom/forecasts.db";
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
}

std::optional<DailyForecast> ForecastDb::getNbm(const std::string& target_date,
                                                 int cycle_hour,
                                                 double latitude,
                                                 double longitude) {
    if (!db_) return std::nullopt;

    // Use tolerance of 0.0005 degrees (~55m).
    const char* sql = R"(
        SELECT target_date, cycle_hour, latitude, longitude,
               temp_max_f, temp_min_f, cycle_date, hours_fetched
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
             temp_max_f, temp_min_f, cycle_date, hours_fetched)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?);
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

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

}  // namespace predibloom::api
