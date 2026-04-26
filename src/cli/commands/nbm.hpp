#pragma once

#include "../../core/config.hpp"
#include <string>

namespace predibloom::cli {

// Bulk-download NBM data for every configured series with weather_source=local_nbm.
int runNbmDownload(const core::Config& config,
                   const std::string& start_date,
                   const std::string& end_date);

// List forecasts already stored in the local NBM database.
int runNbmList(const std::string& date,
               const std::string& lat,
               const std::string& lon,
               const std::string& format);

// List (date, cycle) pairs available on NOAA S3. Cross-references local
// storage to flag each row as present or missing when local_cache_dir is non-empty.
int runNbmRemote(const std::string& date,
                 int days,
                 const std::string& local_cache_dir,
                 const std::string& format);

// Ad-hoc single fetch for one (lat, lon, date).
int runNbmFetch(double lat,
                double lon,
                const std::string& date,
                const std::string& as_of,
                bool force);

// List GRIB2 variables in a single NBM file (downloads it on first access).
int runNbmInventory(const std::string& date,
                    int cycle,
                    int forecast_hour,
                    const std::string& format);

// Capture full NBM cycle(s) to local NetCDF4 storage.
int runNbmCapture(const std::string& date,
                  int cycle,
                  const std::string& forecast_hours,
                  const std::string& format);

// Scan S3 for available cycles and download missing ones.
int runNbmCaptureMissing(int days,
                         const std::string& format);

// Delete grid files older than specified days.
int runNbmCleanup(int older_than_days,
                  const std::string& format);

// List captured grid cycles.
int runNbmGrids(const std::string& format);

// Print info about NBM.
int runNbmAbout();

}  // namespace predibloom::cli
