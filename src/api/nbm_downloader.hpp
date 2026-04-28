#pragma once

#include "result.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace predibloom::api {

// Per-worker state
struct WorkerState {
    std::atomic<int> file_id{-1};          // forecast hour being downloaded (-1 = idle)
    std::atomic<int64_t> bytes_done{0};
    std::atomic<int64_t> bytes_total{0};
};

// Shared progress tracker for parallel downloads.
// Uses fixed-size array because std::atomic isn't copyable/movable (can't use vector).
// Allocate max slots; callers use only the first N based on --parallel.
struct DownloadProgress {
    static constexpr int NUM_WORKERS = 36;

    std::atomic<int64_t> bytes_downloaded{0};
    std::atomic<int64_t> bytes_total{0};
    std::atomic<int> files_done{0};
    int files_total{0};

    WorkerState workers[NUM_WORKERS];
    std::atomic<int> next_worker{0};

    // Get a worker slot for a new download
    int claimWorker(int file_id, int64_t total_bytes) {
        int slot = next_worker.fetch_add(1) % NUM_WORKERS;
        workers[slot].file_id = file_id;
        workers[slot].bytes_done = 0;
        workers[slot].bytes_total = total_bytes;
        return slot;
    }

    void updateWorker(int slot, int64_t bytes) {
        workers[slot].bytes_done = bytes;
    }

    void releaseWorker(int slot) {
        workers[slot].file_id = -1;
        workers[slot].bytes_done = 0;
        workers[slot].bytes_total = 0;
    }
};

// Info about an available NBM cycle on S3
struct NbmCycleInfo {
    std::string date;       // YYYY-MM-DD
    int cycle_hour;         // 1, 7, 13, or 19
    std::string s3_prefix;  // Full S3 URL prefix
};

// Info about a single GRIB2 file on S3
struct NbmFileInfo {
    int forecast_hour;
    int64_t size_bytes;
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
                                      int forecast_hour,
                                      DownloadProgress* progress = nullptr);

    // List available cycles on S3 (last N days)
    Result<std::vector<NbmCycleInfo>> listAvailableCycles(int days = 10);

    // Check if a specific cycle is available on S3
    bool isCycleAvailable(const std::string& date, int cycle_hour);

    // List files in a cycle with their sizes (single S3 list request)
    Result<std::vector<NbmFileInfo>> listCycleFiles(const std::string& date, int cycle_hour);

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
