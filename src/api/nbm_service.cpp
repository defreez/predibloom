#include "nbm_service.hpp"
#include "../core/datetime.hpp"

#include <netcdf.h>
#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>

namespace predibloom::api {

namespace {

std::string defaultGridBaseDir() {
    const char* home = std::getenv("HOME");
    if (!home) return "/tmp/predibloom/nbm";
    return std::string(home) + "/.cache/predibloom/nbm";
}

std::string defaultDbPath() {
    const char* home = std::getenv("HOME");
    if (!home) return "/tmp/predibloom/predibloom.db";
    return std::string(home) + "/.local/share/predibloom/predibloom.db";
}

// Convert YYYY-MM-DD to YYYYMMDD
std::string dateToCompact(const std::string& date) {
    std::string compact = date;
    compact.erase(std::remove(compact.begin(), compact.end(), '-'), compact.end());
    return compact;
}

// NetCDF path for a grid file
std::string ncPath(const std::string& base_dir, const std::string& cycle_date,
                   int cycle_hour, int forecast_hour) {
    std::ostringstream ss;
    ss << base_dir << "/grids/blend." << dateToCompact(cycle_date)
       << "/" << std::setw(2) << std::setfill('0') << cycle_hour
       << "/2t.f" << std::setw(3) << std::setfill('0') << forecast_hour
       << ".nc";
    return ss.str();
}

// Check netCDF return code
void checkNc(int status, const char* msg) {
    if (status != NC_NOERR) {
        throw std::runtime_error(std::string(msg) + ": " + nc_strerror(status));
    }
}

}  // namespace

NbmService::NbmService()
    : grid_base_dir_(defaultGridBaseDir()),
      grid_index_db_(grid_base_dir_ + "/index.db") {
    try {
        db_ = std::make_unique<ForecastDb>(defaultDbPath());
    } catch (...) {
        // Database open failed
    }
    downloader_ = std::make_unique<NbmDownloader>();
    parser_ = std::make_unique<NbmGribParser>();
    grid_reader_ = std::make_unique<NbmGridReader>(grid_base_dir_);
}

NbmService::NbmService(const std::string& db_path, const std::string& cache_dir)
    : grid_base_dir_(cache_dir.empty() ? defaultGridBaseDir() : cache_dir),
      grid_index_db_(grid_base_dir_ + "/index.db") {
    if (!db_path.empty()) {
        try {
            db_ = std::make_unique<ForecastDb>(db_path);
        } catch (...) {
            // Database open failed
        }
    }
    downloader_ = std::make_unique<NbmDownloader>(grid_base_dir_ + "/grib2");
    parser_ = std::make_unique<NbmGribParser>();
    grid_reader_ = std::make_unique<NbmGridReader>(grid_base_dir_);
}

NbmService::~NbmService() = default;

void NbmService::setProgressCallback(CaptureProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

const std::string& NbmService::dbPath() const {
    static std::string empty;
    return db_ ? db_->db_path() : empty;
}

const std::string& NbmService::cacheDir() const {
    return downloader_->cacheDir();
}

const std::string& NbmService::gridBaseDir() const {
    return grid_base_dir_;
}

// =========================================================================
// Point forecast operations
// =========================================================================

Result<DailyForecast> NbmService::fetchDailyForecast(const std::string& target_date,
                                                       double lat, double lon,
                                                       int utc_offset_hours,
                                                       const std::string& asOf_iso) {
    using core::DateTime;
    using core::NbmCycle;

    // Determine which cycle to use
    std::string cycle_date;
    int cycle_hour;

    if (asOf_iso.empty()) {
        auto cycle = NbmCycle::forTargetDate(target_date);
        cycle_date = cycle.date();
        cycle_hour = cycle.hour();
    } else {
        auto as_of = DateTime::parseIso(asOf_iso);
        if (!as_of) {
            return Error(ApiError::ParseError, "Invalid as-of timestamp: " + asOf_iso);
        }
        auto cycle = NbmCycle::availableAt(*as_of);
        cycle_date = cycle.date();
        cycle_hour = cycle.hour();
    }

    // Check SQLite cache first
    if (db_ && db_->is_open()) {
        auto cached = db_->getNbm(target_date, cycle_hour, lat, lon);
        if (cached) {
            return *cached;
        }
    }

    // Try to fetch from grid files
    if (grid_reader_) {
        core::NbmCycle cycle(cycle_date, cycle_hour);
        auto hours = cycle.forecastHoursFor(target_date, utc_offset_hours);

        std::vector<double> temps_k;
        std::vector<int> valid_hours;

        for (int fhr : hours) {
            auto temp = grid_reader_->getTemp(cycle_date, cycle_hour, fhr, lat, lon);
            if (temp) {
                temps_k.push_back(*temp);
                valid_hours.push_back(fhr);
            }
        }

        if (!temps_k.empty()) {
            // Convert to Fahrenheit
            std::vector<double> temps_f;
            temps_f.reserve(temps_k.size());
            for (double k : temps_k) {
                temps_f.push_back((k - 273.15) * 9.0 / 5.0 + 32.0);
            }

            auto max_it = std::max_element(temps_f.begin(), temps_f.end());
            auto min_it = std::min_element(temps_f.begin(), temps_f.end());
            double temp_max = std::round(*max_it * 10.0) / 10.0;
            double temp_min = std::round(*min_it * 10.0) / 10.0;

            size_t max_idx = std::distance(temps_f.begin(), max_it);
            size_t min_idx = std::distance(temps_f.begin(), min_it);

            // Compute local time of max/min
            auto cycle_dt = DateTime::parseDate(cycle_date);
            if (!cycle_dt) {
                return Error(ApiError::ParseError, "Invalid cycle date");
            }
            DateTime cycle_time = cycle_dt->addHours(cycle_hour);

            auto format_local_time = [&](size_t idx) -> std::string {
                int fhr = valid_hours[idx];
                DateTime valid_utc = cycle_time.addHours(fhr);
                DateTime local_time = valid_utc.addHours(utc_offset_hours);
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%02d:%02d",
                              local_time.hour(), local_time.minute());
                return buf;
            };

            DailyForecast f;
            f.source = "nbm";
            f.target_date = target_date;
            f.latitude = lat;
            f.longitude = lon;
            f.temp_max_f = temp_max;
            f.temp_min_f = temp_min;
            f.cycle_hour = cycle_hour;
            f.cycle_date = cycle_date;
            f.hours_fetched = static_cast<int>(temps_k.size());
            f.time_of_max = format_local_time(max_idx);
            f.time_of_min = format_local_time(min_idx);

            // Store to SQLite
            if (db_ && db_->is_open()) {
                db_->putNbm(f);
            }

            return f;
        }
    }

    // Fall back to downloading from S3 if we have no local data
    core::NbmCycle cycle(cycle_date, cycle_hour);
    auto hours = cycle.forecastHoursFor(target_date, utc_offset_hours);

    if (hours.empty()) {
        return Error(ApiError::DataError, "No forecast hours available for " + target_date);
    }

    std::vector<double> temps_k;
    std::vector<int> valid_hours;

    for (int fhr : hours) {
        auto grib_result = downloader_->downloadGrib(cycle_date, cycle_hour, fhr);
        if (!grib_result.ok()) continue;

        auto temp_result = parser_->getTempAtPoint(grib_result.value(), lat, lon);
        if (temp_result.ok()) {
            temps_k.push_back(temp_result.value());
            valid_hours.push_back(fhr);
        }
    }

    if (temps_k.empty()) {
        return Error(ApiError::DataError, "No temperature data retrieved for " + target_date);
    }

    // Convert to Fahrenheit
    std::vector<double> temps_f;
    temps_f.reserve(temps_k.size());
    for (double k : temps_k) {
        temps_f.push_back((k - 273.15) * 9.0 / 5.0 + 32.0);
    }

    auto max_it = std::max_element(temps_f.begin(), temps_f.end());
    auto min_it = std::min_element(temps_f.begin(), temps_f.end());
    double temp_max = std::round(*max_it * 10.0) / 10.0;
    double temp_min = std::round(*min_it * 10.0) / 10.0;

    size_t max_idx = std::distance(temps_f.begin(), max_it);
    size_t min_idx = std::distance(temps_f.begin(), min_it);

    // Compute local time of max/min
    auto cycle_dt = core::DateTime::parseDate(cycle_date);
    if (!cycle_dt) {
        return Error(ApiError::ParseError, "Invalid cycle date");
    }
    core::DateTime cycle_time = cycle_dt->addHours(cycle_hour);

    auto format_local_time = [&](size_t idx) -> std::string {
        int fhr = valid_hours[idx];
        core::DateTime valid_utc = cycle_time.addHours(fhr);
        core::DateTime local_time = valid_utc.addHours(utc_offset_hours);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d:%02d",
                      local_time.hour(), local_time.minute());
        return buf;
    };

    DailyForecast f;
    f.source = "nbm";
    f.target_date = target_date;
    f.latitude = lat;
    f.longitude = lon;
    f.temp_max_f = temp_max;
    f.temp_min_f = temp_min;
    f.cycle_hour = cycle_hour;
    f.cycle_date = cycle_date;
    f.hours_fetched = static_cast<int>(temps_k.size());
    f.time_of_max = format_local_time(max_idx);
    f.time_of_min = format_local_time(min_idx);

    // Store to SQLite
    if (db_ && db_->is_open()) {
        db_->putNbm(f);
    }

    return f;
}

// =========================================================================
// Cache listing operations
// =========================================================================

std::vector<DailyForecast> NbmService::listForecasts(const std::string& date,
                                                       double lat, double lon) {
    std::vector<DailyForecast> results;

    if (!db_ || !db_->is_open()) {
        return results;
    }

    // Query the database directly
    sqlite3* db = nullptr;
    if (sqlite3_open(db_->db_path().c_str(), &db) != SQLITE_OK) {
        return results;
    }

    std::string sql = R"(
        SELECT target_date, cycle_hour, cycle_date, latitude, longitude,
               temp_max_f, temp_min_f, hours_fetched, time_of_max, time_of_min
        FROM nbm_forecasts
        WHERE 1=1
    )";

    std::vector<std::string> params;
    if (!date.empty()) {
        sql += " AND target_date = ?";
        params.push_back(date);
    }
    if (lat != 0.0) {
        sql += " AND ABS(latitude - ?) < 0.001";
    }
    if (lon != 0.0) {
        sql += " AND ABS(longitude - ?) < 0.001";
    }
    sql += " ORDER BY target_date, cycle_hour";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        int idx = 1;
        for (const auto& p : params) {
            sqlite3_bind_text(stmt, idx++, p.c_str(), -1, SQLITE_TRANSIENT);
        }
        if (lat != 0.0) {
            sqlite3_bind_double(stmt, idx++, lat);
        }
        if (lon != 0.0) {
            sqlite3_bind_double(stmt, idx++, lon);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            DailyForecast f;
            f.source = "nbm";
            const char* td = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            f.target_date = td ? td : "";
            f.cycle_hour = sqlite3_column_int(stmt, 1);
            const char* cd = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            f.cycle_date = cd ? cd : "";
            f.latitude = sqlite3_column_double(stmt, 3);
            f.longitude = sqlite3_column_double(stmt, 4);
            f.temp_max_f = sqlite3_column_double(stmt, 5);
            f.temp_min_f = sqlite3_column_double(stmt, 6);
            f.hours_fetched = sqlite3_column_int(stmt, 7);
            if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
                const char* tm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
                f.time_of_max = tm ? tm : "";
            }
            if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) {
                const char* tm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
                f.time_of_min = tm ? tm : "";
            }
            results.push_back(f);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return results;
}

// =========================================================================
// Remote listing operations
// =========================================================================

Result<std::vector<NbmCycleInfo>> NbmService::listRemoteCycles(int days) {
    return downloader_->listAvailableCycles(days);
}

// =========================================================================
// Grid capture operations
// =========================================================================

Result<std::string> NbmService::extractToNetCDF(const std::string& grib_path,
                                                  const std::string& cycle_date,
                                                  int cycle_hour,
                                                  int forecast_hour) {
    std::string nc_path = ncPath(grid_base_dir_, cycle_date, cycle_hour, forecast_hour);

    // Check if already exists
    if (std::filesystem::exists(nc_path)) {
        return nc_path;
    }

    // Parse GRIB2
    auto grid_result = parser_->getFullGrid(grib_path);
    if (!grid_result.ok()) {
        return Error(grid_result.error().type, grid_result.error().message);
    }

    const TempGrid& grid = grid_result.value();

    // Create parent directories
    std::filesystem::path p(nc_path);
    std::filesystem::create_directories(p.parent_path());

    // Write NetCDF4
    int ncid;
    checkNc(nc_create(nc_path.c_str(), NC_NETCDF4 | NC_CLOBBER, &ncid), "create nc");

    try {
        // Create dimensions
        int y_dimid, x_dimid;
        checkNc(nc_def_dim(ncid, "y", grid.ny, &y_dimid), "def y dim");
        checkNc(nc_def_dim(ncid, "x", grid.nx, &x_dimid), "def x dim");

        int dims[2] = {y_dimid, x_dimid};

        // Create variables
        int lat_varid, lon_varid, temp_varid;
        checkNc(nc_def_var(ncid, "latitude", NC_FLOAT, 2, dims, &lat_varid), "def lat");
        checkNc(nc_def_var(ncid, "longitude", NC_FLOAT, 2, dims, &lon_varid), "def lon");
        checkNc(nc_def_var(ncid, "temperature_2m", NC_FLOAT, 2, dims, &temp_varid), "def temp");

        // Enable compression
        checkNc(nc_def_var_deflate(ncid, lat_varid, 1, 1, 4), "deflate lat");
        checkNc(nc_def_var_deflate(ncid, lon_varid, 1, 1, 4), "deflate lon");
        checkNc(nc_def_var_deflate(ncid, temp_varid, 1, 1, 4), "deflate temp");

        // Set attributes
        checkNc(nc_put_att_text(ncid, NC_GLOBAL, "title",
                                 strlen("NBM 2m Temperature"), "NBM 2m Temperature"), "title");
        checkNc(nc_put_att_text(ncid, lat_varid, "units",
                                 strlen("degrees_north"), "degrees_north"), "lat units");
        checkNc(nc_put_att_text(ncid, lon_varid, "units",
                                 strlen("degrees_east"), "degrees_east"), "lon units");
        checkNc(nc_put_att_text(ncid, temp_varid, "units", strlen("K"), "K"), "temp units");

        // End define mode
        checkNc(nc_enddef(ncid), "enddef");

        // Write data
        checkNc(nc_put_var_float(ncid, lat_varid, grid.lats.data()), "put lat");
        checkNc(nc_put_var_float(ncid, lon_varid, grid.lons.data()), "put lon");
        checkNc(nc_put_var_float(ncid, temp_varid, grid.temps.data()), "put temp");

        nc_close(ncid);

        // Update grid index
        std::filesystem::create_directories(std::filesystem::path(grid_index_db_).parent_path());
        sqlite3* idx_db = nullptr;
        if (sqlite3_open(grid_index_db_.c_str(), &idx_db) == SQLITE_OK) {
            sqlite3_exec(idx_db, R"(
                CREATE TABLE IF NOT EXISTS nbm_grids (
                    id INTEGER PRIMARY KEY,
                    cycle_date TEXT NOT NULL,
                    cycle_hour INTEGER NOT NULL,
                    forecast_hour INTEGER NOT NULL,
                    variable TEXT NOT NULL,
                    file_path TEXT NOT NULL,
                    grid_shape TEXT,
                    created_at TEXT DEFAULT (datetime('now')),
                    UNIQUE (cycle_date, cycle_hour, forecast_hour, variable)
                )
            )", nullptr, nullptr, nullptr);

            std::string rel_path = std::filesystem::relative(nc_path, grid_base_dir_).string();
            std::ostringstream shape_ss;
            shape_ss << grid.ny << "x" << grid.nx;
            std::string grid_shape = shape_ss.str();

            sqlite3_stmt* stmt;
            const char* sql = R"(
                INSERT OR REPLACE INTO nbm_grids
                    (cycle_date, cycle_hour, forecast_hour, variable, file_path, grid_shape)
                VALUES (?, ?, ?, ?, ?, ?)
            )";
            if (sqlite3_prepare_v2(idx_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, cycle_date.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, cycle_hour);
                sqlite3_bind_int(stmt, 3, forecast_hour);
                sqlite3_bind_text(stmt, 4, "2t", -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 5, rel_path.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 6, grid_shape.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            sqlite3_close(idx_db);
        }

        return nc_path;

    } catch (const std::exception& e) {
        nc_close(ncid);
        // Clean up partial file
        std::filesystem::remove(nc_path);
        return Error(ApiError::NetworkError, std::string("NetCDF write failed: ") + e.what());
    }
}

Result<CaptureStats> NbmService::captureCycle(const std::string& date, int cycle_hour,
                                                const std::vector<int>& forecast_hours) {
    // Check if cycle is available
    if (!downloader_->isCycleAvailable(date, cycle_hour)) {
        CaptureStats stats;
        stats.error = "Cycle " + date + " " + std::to_string(cycle_hour) +
                      "Z not yet available on S3";
        return stats;
    }

    // Default to hours 1-36 (all hours with 2t)
    std::vector<int> hours = forecast_hours;
    if (hours.empty()) {
        for (int h = 1; h <= 36; ++h) {
            hours.push_back(h);
        }
    }

    CaptureStats stats;
    int total = static_cast<int>(hours.size());

    for (int i = 0; i < total; ++i) {
        int fhr = hours[i];
        std::string nc_path = ncPath(grid_base_dir_, date, cycle_hour, fhr);
        std::string status;

        if (std::filesystem::exists(nc_path)) {
            stats.skipped++;
            status = "skip";
        } else {
            // Download GRIB2
            auto grib_result = downloader_->downloadGrib(date, cycle_hour, fhr);
            if (!grib_result.ok()) {
                if (grib_result.error().http_status == 404) {
                    // f001-f036 should have 2t, but higher hours may not
                    stats.skipped++;
                    status = "no2t";
                } else {
                    stats.failed++;
                    status = "FAIL";
                }
            } else {
                // Extract to NetCDF
                auto nc_result = extractToNetCDF(grib_result.value(), date, cycle_hour, fhr);
                if (nc_result.ok()) {
                    stats.success++;
                    status = "ok";
                } else {
                    if (nc_result.error().message.find("No 2m temperature") != std::string::npos) {
                        stats.skipped++;
                        status = "no2t";
                    } else {
                        stats.failed++;
                        status = "FAIL";
                    }
                }
            }
        }

        if (progress_callback_) {
            progress_callback_(i + 1, total, date, cycle_hour, fhr, status);
        }
    }

    return stats;
}

Result<CaptureStats> NbmService::captureMissing(int days) {
    // Get available cycles from S3
    auto remote_result = downloader_->listAvailableCycles(days);
    if (!remote_result.ok()) {
        CaptureStats stats;
        stats.error = remote_result.error().message;
        return stats;
    }

    // Get captured cycles
    auto captured = listCapturedGrids();
    std::set<std::pair<std::string, int>> captured_set;
    for (const auto& c : captured) {
        if (c.file_count >= 10) {  // Consider captured if at least 10 hours
            captured_set.insert({c.cycle_date, c.cycle_hour});
        }
    }

    // Find missing
    std::vector<std::pair<std::string, int>> missing;
    for (const auto& r : remote_result.value()) {
        if (captured_set.count({r.date, r.cycle_hour}) == 0) {
            missing.push_back({r.date, r.cycle_hour});
        }
    }

    CaptureStats total_stats;

    for (const auto& [date, hour] : missing) {
        auto result = captureCycle(date, hour, {});
        if (result.ok()) {
            total_stats.success += result.value().success;
            total_stats.failed += result.value().failed;
            total_stats.skipped += result.value().skipped;
        }
    }

    return total_stats;
}

// =========================================================================
// Cleanup operations
// =========================================================================

Result<CleanupStats> NbmService::cleanup(int older_than_days) {
    CleanupStats stats;

    std::time_t now = std::time(nullptr);
    std::tm* utc = std::gmtime(&now);
    utc->tm_mday -= older_than_days;
    std::mktime(utc);

    char cutoff_buf[11];
    std::strftime(cutoff_buf, sizeof(cutoff_buf), "%Y-%m-%d", utc);
    std::string cutoff_date(cutoff_buf);

    // Get old entries from index
    sqlite3* idx_db = nullptr;
    if (sqlite3_open(grid_index_db_.c_str(), &idx_db) != SQLITE_OK) {
        return stats;
    }

    std::vector<std::pair<int, std::string>> to_delete;
    std::set<std::pair<std::string, int>> cycles_deleted;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, cycle_date, cycle_hour, file_path FROM nbm_grids WHERE cycle_date < ?";
    if (sqlite3_prepare_v2(idx_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cutoff_date.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const char* date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            int hour = sqlite3_column_int(stmt, 2);
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            to_delete.push_back({id, path ? path : ""});
            cycles_deleted.insert({date ? date : "", hour});
        }
        sqlite3_finalize(stmt);
    }

    // Delete files and index entries
    for (const auto& [id, rel_path] : to_delete) {
        std::string full_path = grid_base_dir_ + "/" + rel_path;
        if (std::filesystem::exists(full_path)) {
            std::filesystem::remove(full_path);
            stats.deleted_files++;
        }

        std::string del_sql = "DELETE FROM nbm_grids WHERE id = ?";
        sqlite3_stmt* del_stmt;
        if (sqlite3_prepare_v2(idx_db, del_sql.c_str(), -1, &del_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(del_stmt, 1, id);
            sqlite3_step(del_stmt);
            sqlite3_finalize(del_stmt);
        }
    }

    sqlite3_close(idx_db);

    stats.deleted_cycles = static_cast<int>(cycles_deleted.size());

    // Clean up empty directories
    std::filesystem::path grids_dir = grid_base_dir_ + "/grids";
    if (std::filesystem::exists(grids_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(grids_dir)) {
            if (entry.is_directory()) {
                bool has_files = false;
                for (const auto& _ : std::filesystem::recursive_directory_iterator(entry.path())) {
                    (void)_;
                    has_files = true;
                    break;
                }
                if (!has_files) {
                    std::filesystem::remove_all(entry.path());
                }
            }
        }
    }

    return stats;
}

// =========================================================================
// Grid listing operations
// =========================================================================

std::vector<GridCycleInfo> NbmService::listCapturedGrids() {
    std::vector<GridCycleInfo> results;

    sqlite3* idx_db = nullptr;
    if (sqlite3_open(grid_index_db_.c_str(), &idx_db) != SQLITE_OK) {
        return results;
    }

    const char* sql = R"(
        SELECT cycle_date, cycle_hour, COUNT(*) as file_count,
               MIN(forecast_hour) as fhr_min, MAX(forecast_hour) as fhr_max
        FROM nbm_grids
        GROUP BY cycle_date, cycle_hour
        ORDER BY cycle_date DESC, cycle_hour DESC
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(idx_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            GridCycleInfo info;
            const char* date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            info.cycle_date = date ? date : "";
            info.cycle_hour = sqlite3_column_int(stmt, 1);
            info.file_count = sqlite3_column_int(stmt, 2);
            info.fhr_min = sqlite3_column_int(stmt, 3);
            info.fhr_max = sqlite3_column_int(stmt, 4);
            results.push_back(info);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(idx_db);
    return results;
}

// =========================================================================
// Inventory operations
// =========================================================================

Result<std::vector<GribVariable>> NbmService::inventory(const std::string& date,
                                                          int cycle_hour,
                                                          int forecast_hour) {
    // Download if needed
    auto grib_result = downloader_->downloadGrib(date, cycle_hour, forecast_hour);
    if (!grib_result.ok()) {
        return Error(grib_result.error().type, grib_result.error().message);
    }

    return parser_->listVariables(grib_result.value());
}

}  // namespace predibloom::api
