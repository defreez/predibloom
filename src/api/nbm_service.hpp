#pragma once

#include "forecast_db.hpp"
#include "nbm_downloader.hpp"
#include "nbm_grib_parser.hpp"
#include "nbm_grid_reader.hpp"
#include "result.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace predibloom::api {

// Info about a captured cycle in the local grid index
struct GridCycleInfo {
    std::string cycle_date;
    int cycle_hour;
    int file_count;
    int fhr_min;
    int fhr_max;
};

// Progress callback signature: (current_file, total_files, cycle_date, cycle_hour, forecast_hour, status)
// status: "ok", "skip", "no2t", "FAIL"
using CaptureProgressCallback = std::function<void(int, int, const std::string&, int, int, const std::string&)>;

// High-level NBM operations - replaces all Python functionality.
class NbmService {
public:
    NbmService();
    NbmService(const std::string& db_path, const std::string& cache_dir);
    ~NbmService();

    // Set progress callback for capture operations
    void setProgressCallback(CaptureProgressCallback callback);

    // Set number of parallel downloads (default 8)
    void setParallelDownloads(int n);

    // =========================================================================
    // Point forecast operations (replaces 'fetch' command)
    // =========================================================================

    // Fetch daily forecast for a location.
    // timezone: IANA name (e.g., "America/New_York") of the settlement station.
    // If asOf_iso is non-empty, use the NBM cycle available at that time.
    Result<DailyForecast> fetchDailyForecast(const std::string& target_date,
                                              double lat, double lon,
                                              const std::string& timezone = "America/New_York",
                                              const std::string& asOf_iso = "");

    // =========================================================================
    // Cache listing operations (replaces 'list-cache' command)
    // =========================================================================

    // List point forecasts from SQLite
    std::vector<DailyForecast> listForecasts(const std::string& date = "",
                                              double lat = 0.0, double lon = 0.0);

    // =========================================================================
    // Remote listing operations (replaces 'list-remote' command)
    // =========================================================================

    // List available S3 cycles
    Result<std::vector<NbmCycleInfo>> listRemoteCycles(int days = 10);

    // =========================================================================
    // Grid capture operations (replaces 'capture', 'update' commands)
    // =========================================================================

    // Capture a single NBM cycle to local storage.
    // Downloads GRIB2 files, extracts 2m temp to NetCDF4, updates index.
    // If forecast_hours is empty, defaults to 1-36 (all hours with 2t).
    // If progress is non-null, updates it with download progress.
    Result<CaptureStats> captureCycle(const std::string& date, int cycle_hour,
                                       const std::vector<int>& forecast_hours = {},
                                       DownloadProgress* progress = nullptr);

    // Capture all missing cycles (scan S3, download what's not captured)
    Result<CaptureStats> captureMissing(int days = 10);

    // =========================================================================
    // Cleanup operations (replaces 'cleanup' command)
    // =========================================================================

    // Delete grid files older than specified days
    Result<CleanupStats> cleanup(int older_than_days = 30);

    // =========================================================================
    // Grid listing operations (replaces 'grids' command)
    // =========================================================================

    // List captured grid cycles
    std::vector<GridCycleInfo> listCapturedGrids();

    // =========================================================================
    // Inventory operations (replaces 'inventory' command)
    // =========================================================================

    // List variables in a GRIB2 file
    Result<std::vector<GribVariable>> inventory(const std::string& date,
                                                  int cycle_hour,
                                                  int forecast_hour);

    // =========================================================================
    // Path accessors
    // =========================================================================

    const std::string& dbPath() const;
    const std::string& cacheDir() const;
    const std::string& gridBaseDir() const;

private:
    // Extract 2m temp from GRIB2 and write to NetCDF4
    Result<std::string> extractToNetCDF(const std::string& grib_path,
                                         const std::string& cycle_date,
                                         int cycle_hour,
                                         int forecast_hour);

    std::unique_ptr<ForecastDb> db_;
    std::unique_ptr<NbmDownloader> downloader_;
    std::unique_ptr<NbmGribParser> parser_;
    std::unique_ptr<NbmGridReader> grid_reader_;

    std::string grid_base_dir_;  // ~/.cache/predibloom/nbm
    std::string grid_index_db_;  // ~/.cache/predibloom/nbm/index.db

    CaptureProgressCallback progress_callback_;
    int parallel_downloads_ = 8;
};

}  // namespace predibloom::api
