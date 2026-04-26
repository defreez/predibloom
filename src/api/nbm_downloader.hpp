#pragma once

#include "result.hpp"

#include <string>
#include <vector>

namespace predibloom::api {

// Info about an available NBM cycle on S3
struct NbmCycleInfo {
    std::string date;       // YYYY-MM-DD
    int cycle_hour;         // 1, 7, 13, or 19
    std::string s3_prefix;  // Full S3 URL prefix
};

// Statistics from a capture operation
struct CaptureStats {
    int success = 0;
    int failed = 0;
    int skipped = 0;
    std::string error;  // Non-empty if entire operation failed
};

// Statistics from a cleanup operation
struct CleanupStats {
    int deleted_files = 0;
    int deleted_cycles = 0;
};

// Downloads NBM GRIB2 files from NOAA S3.
// S3 bucket is public and supports anonymous HTTP access.
class NbmDownloader {
public:
    NbmDownloader();
    explicit NbmDownloader(const std::string& cache_dir);
    ~NbmDownloader();

    // Download a single GRIB2 file from S3.
    // Returns local file path on success.
    // s3://noaa-nbm-grib2-pds/blend.YYYYMMDD/HH/core/blend.tHHz.core.fFFF.co.grib2
    Result<std::string> downloadGrib(const std::string& cycle_date,
                                      int cycle_hour,
                                      int forecast_hour);

    // List available cycles on S3 (last N days)
    Result<std::vector<NbmCycleInfo>> listAvailableCycles(int days = 10);

    // Check if a specific cycle is available on S3
    bool isCycleAvailable(const std::string& date, int cycle_hour);

    // Get local cache directory
    const std::string& cacheDir() const { return cache_dir_; }

    // Build S3 HTTPS URL for a GRIB2 file
    static std::string s3Url(const std::string& cycle_date,
                              int cycle_hour,
                              int forecast_hour);

    // Build local cache path for a GRIB2 file
    std::string localPath(const std::string& cycle_date,
                           int cycle_hour,
                           int forecast_hour) const;

private:
    std::string cache_dir_;  // ~/.cache/predibloom/grib2/
};

}  // namespace predibloom::api
