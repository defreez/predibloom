#include "nbm_downloader.hpp"

#include <httplib.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace predibloom::api {

namespace {

constexpr const char* NBM_BUCKET = "noaa-nbm-grib2-pds";
constexpr const char* S3_HOST = "noaa-nbm-grib2-pds.s3.amazonaws.com";

std::string defaultCacheDir() {
    const char* home = std::getenv("HOME");
    if (!home) return "/tmp/predibloom/grib2";
    return std::string(home) + "/.cache/predibloom/grib2";
}

// Convert YYYY-MM-DD to YYYYMMDD
std::string dateToCompact(const std::string& date) {
    std::string compact = date;
    compact.erase(std::remove(compact.begin(), compact.end(), '-'), compact.end());
    return compact;
}

// Convert YYYYMMDD to YYYY-MM-DD
std::string compactToDate(const std::string& compact) {
    if (compact.size() != 8) return compact;
    return compact.substr(0, 4) + "-" + compact.substr(4, 2) + "-" + compact.substr(6, 2);
}

}  // namespace

NbmDownloader::NbmDownloader()
    : cache_dir_(defaultCacheDir()) {
}

NbmDownloader::NbmDownloader(const std::string& cache_dir)
    : cache_dir_(cache_dir.empty() ? defaultCacheDir() : cache_dir) {
}

NbmDownloader::~NbmDownloader() = default;

std::string NbmDownloader::s3Url(const std::string& cycle_date,
                                  int cycle_hour,
                                  int forecast_hour) {
    std::ostringstream ss;
    ss << "https://" << S3_HOST << "/blend." << dateToCompact(cycle_date)
       << "/" << std::setw(2) << std::setfill('0') << cycle_hour
       << "/core/blend.t" << std::setw(2) << std::setfill('0') << cycle_hour
       << "z.core.f" << std::setw(3) << std::setfill('0') << forecast_hour
       << ".co.grib2";
    return ss.str();
}

std::string NbmDownloader::localPath(const std::string& cycle_date,
                                      int cycle_hour,
                                      int forecast_hour) const {
    std::ostringstream ss;
    ss << cache_dir_ << "/blend." << dateToCompact(cycle_date)
       << "/" << std::setw(2) << std::setfill('0') << cycle_hour
       << "/f" << std::setw(3) << std::setfill('0') << forecast_hour
       << ".grib2";
    return ss.str();
}

Result<std::string> NbmDownloader::downloadGrib(const std::string& cycle_date,
                                                  int cycle_hour,
                                                  int forecast_hour) {
    std::string local_path = localPath(cycle_date, cycle_hour, forecast_hour);

    // Check if already exists
    if (std::filesystem::exists(local_path)) {
        auto size = std::filesystem::file_size(local_path);
        if (size > 0) {
            return local_path;
        }
    }

    // Create parent directories
    std::filesystem::path p(local_path);
    std::filesystem::create_directories(p.parent_path());

    // Build URL path (without host)
    std::ostringstream path_ss;
    path_ss << "/blend." << dateToCompact(cycle_date)
            << "/" << std::setw(2) << std::setfill('0') << cycle_hour
            << "/core/blend.t" << std::setw(2) << std::setfill('0') << cycle_hour
            << "z.core.f" << std::setw(3) << std::setfill('0') << forecast_hour
            << ".co.grib2";
    std::string url_path = path_ss.str();

    // Download via HTTPS
    httplib::SSLClient client(S3_HOST);
    client.set_connection_timeout(30);
    client.set_read_timeout(120);
    client.set_follow_location(true);

    auto res = client.Get(url_path.c_str());

    if (!res) {
        return Error(ApiError::NetworkError,
                     "HTTP request failed: " + httplib::to_string(res.error()));
    }

    if (res->status == 404) {
        return Error(ApiError::HttpError,
                     "File not found on S3 (cycle may not be available yet)",
                     404);
    }

    if (res->status != 200) {
        return Error(ApiError::HttpError,
                     "HTTP " + std::to_string(res->status) + ": " + res->reason,
                     res->status);
    }

    // Write to file
    std::ofstream out(local_path, std::ios::binary);
    if (!out) {
        return Error(ApiError::NetworkError, "Failed to create file: " + local_path);
    }
    out.write(res->body.data(), res->body.size());
    out.close();

    if (!out) {
        return Error(ApiError::NetworkError, "Failed to write file: " + local_path);
    }

    return local_path;
}

bool NbmDownloader::isCycleAvailable(const std::string& date, int cycle_hour) {
    // Check if f001 exists - if so, cycle is available
    std::ostringstream path_ss;
    path_ss << "/blend." << dateToCompact(date)
            << "/" << std::setw(2) << std::setfill('0') << cycle_hour
            << "/core/blend.t" << std::setw(2) << std::setfill('0') << cycle_hour
            << "z.core.f001.co.grib2";

    httplib::SSLClient client(S3_HOST);
    client.set_connection_timeout(10);
    client.set_read_timeout(10);

    auto res = client.Head(path_ss.str().c_str());

    return res && res->status == 200;
}

Result<std::vector<NbmCycleInfo>> NbmDownloader::listAvailableCycles(int days) {
    std::vector<NbmCycleInfo> cycles;
    constexpr int NBM_CYCLES[] = {1, 7, 13, 19};

    // Get current time and work backwards in UTC
    std::time_t now = std::time(nullptr);

    // Check each day
    for (int d = 0; d < days; ++d) {
        std::time_t day_time = now - d * 24 * 3600;
        std::tm* day_utc = std::gmtime(&day_time);

        char date_buf[11];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", day_utc);
        std::string date_str(date_buf);

        // Check each cycle for this day
        for (int ch : NBM_CYCLES) {
            if (isCycleAvailable(date_str, ch)) {
                NbmCycleInfo info;
                info.date = date_str;
                info.cycle_hour = ch;

                std::ostringstream ss;
                ss << "s3://" << NBM_BUCKET << "/blend." << dateToCompact(date_str)
                   << "/" << std::setw(2) << std::setfill('0') << ch << "/core/";
                info.s3_prefix = ss.str();

                cycles.push_back(info);
            }
        }
    }

    // Sort by date and cycle (newest first)
    std::sort(cycles.begin(), cycles.end(), [](const NbmCycleInfo& a, const NbmCycleInfo& b) {
        if (a.date != b.date) return a.date > b.date;
        return a.cycle_hour > b.cycle_hour;
    });

    return cycles;
}

}  // namespace predibloom::api
