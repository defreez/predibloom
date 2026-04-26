#include "nbm.hpp"
#include "../../api/local_nbm_client.hpp"
#include "../../api/nbm_service.hpp"
#include "../../core/datetime.hpp"
#include "../formatters.hpp"

#include <nlohmann/json.hpp>

#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace predibloom::cli {

namespace {

void printListTable(const std::vector<api::DailyForecast>& rows) {
    std::cout << std::left
              << std::setw(12) << "Target"
              << std::setw(6)  << "UTC"
              << std::setw(18) << "Cycle (PT)"
              << std::right
              << std::setw(10) << "Lat"
              << std::setw(11) << "Lon"
              << std::setw(8)  << "Max"
              << std::setw(8)  << "Min"
              << std::setw(7)  << "Hrs"
              << "\n";
    std::cout << std::string(82, '-') << "\n";

    for (const auto& r : rows) {
        // Compute actual PT date+time from cycle_date + cycle_hour
        std::string pt_str = "?";
        auto dt = core::DateTime::parseDate(r.cycle_date);
        if (dt) {
            core::DateTime cycle_time = dt->addHours(r.cycle_hour);
            pt_str = core::PacificTime::format(cycle_time, true);
        }

        std::cout << std::left
                  << std::setw(12) << r.target_date
                  << std::setw(6)  << (std::to_string(r.cycle_hour) + "Z")
                  << std::setw(18) << pt_str;
        std::cout << std::right << std::fixed << std::setprecision(3)
                  << std::setw(10) << r.latitude
                  << std::setw(11) << r.longitude;
        std::cout << std::setprecision(1)
                  << std::setw(8) << r.temp_max_f
                  << std::setw(8) << r.temp_min_f
                  << std::setw(7) << r.hours_fetched << "\n";
    }
}

void printRemoteTable(const std::vector<api::NbmCycleInfo>& rows,
                      const std::set<std::pair<std::string, int>>& cached) {
    std::cout << std::left
              << std::setw(12) << "Date"
              << std::setw(7)  << "Cycle"
              << std::setw(10) << "Status"
              << "S3 prefix\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& r : rows) {
        bool hit = cached.count({r.date, r.cycle_hour}) > 0;
        std::cout << std::left
                  << std::setw(12) << r.date
                  << std::setw(7)  << (std::to_string(r.cycle_hour) + "Z")
                  << std::setw(10) << (hit ? "cached" : "missing")
                  << r.s3_prefix << "\n";
    }
}

void printInventoryTable(const std::vector<api::GribVariable>& rows) {
    std::cout << std::left
              << std::setw(14) << "shortName"
              << std::setw(22) << "typeOfLevel"
              << std::right << std::setw(7) << "level"
              << "  " << std::left << "name\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& r : rows) {
        std::cout << std::left
                  << std::setw(14) << r.short_name
                  << std::setw(22) << r.type_of_level
                  << std::right << std::setw(7) << r.level
                  << "  " << std::left << r.name << "\n";
    }
}

void printGridsTable(const std::vector<api::GridCycleInfo>& rows) {
    std::cout << std::left
              << std::setw(12) << "Date"
              << std::setw(7)  << "Cycle"
              << std::right
              << std::setw(8)  << "Files"
              << std::setw(10) << "FHR Min"
              << std::setw(10) << "FHR Max"
              << "\n";
    std::cout << std::string(47, '-') << "\n";

    for (const auto& r : rows) {
        std::cout << std::left
                  << std::setw(12) << r.cycle_date
                  << std::setw(7)  << (std::to_string(r.cycle_hour) + "Z")
                  << std::right
                  << std::setw(8)  << r.file_count
                  << std::setw(10) << r.fhr_min
                  << std::setw(10) << r.fhr_max
                  << "\n";
    }
}

std::string formatDouble(double v) {
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

}  // namespace

int runNbmDownload(const core::Config& config,
                   const std::string& start_date,
                   const std::string& end_date) {
    std::vector<const core::TrackedSeries*> nbm_series;
    for (const auto& tab : config.tabs) {
        for (const auto& sc : tab.series) {
            if (sc.latitude != 0 && sc.weather_source == core::WeatherSource::LocalNbm) {
                nbm_series.push_back(&sc);
            }
        }
    }

    if (nbm_series.empty()) {
        std::cerr << "No series configured with weather_source: local_nbm\n";
        return 1;
    }

    std::cerr << "Downloading NBM data for " << nbm_series.size() << " cities from "
              << start_date << " to " << end_date << "\n";

    api::LocalNbmClient nbm_client;
    int total = 0;
    int success = 0;

    std::vector<std::string> dates;
    std::string current = start_date;
    while (current <= end_date) {
        dates.push_back(current);
        current = core::addDaysToDate(current, 1);
        if (current.empty()) break;
    }

    std::cerr << "Date range: " << dates.size() << " days\n\n";

    for (const auto* sc : nbm_series) {
        std::cerr << sc->label << " (" << sc->series_ticker << ")\n";

        int city_success = 0;
        for (const auto& date : dates) {
            total++;
            std::string as_of = core::computeAsOfIso(
                date, sc->entry_day_offset, sc->effectiveEntryHour());

            auto result = nbm_client.getForecast(sc->latitude, sc->longitude, date,
                sc->utc_offset_hours, as_of);
            if (result.ok()) {
                success++;
                city_success++;
                std::cerr << ".";
            } else {
                std::cerr << "x";
            }
        }
        std::cerr << " " << city_success << "/" << dates.size() << "\n";
    }

    std::cerr << "\nTotal: " << success << "/" << total << " forecasts downloaded\n";
    if (success < total) {
        std::cerr << "Note: NOAA S3 only retains ~10 days of NBM data.\n";
    }
    return 0;
}

int runNbmList(const std::string& date,
               const std::string& lat,
               const std::string& lon,
               const std::string& format) {
    api::NbmService service;

    double lat_val = lat.empty() ? 0.0 : std::stod(lat);
    double lon_val = lon.empty() ? 0.0 : std::stod(lon);

    auto rows = service.listForecasts(date, lat_val, lon_val);

    if (parseFormat(format) == OutputFormat::Json) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& r : rows) {
            nlohmann::json row;
            row["date"] = r.target_date;
            row["cycle"] = r.cycle_hour;
            row["cycle_date"] = r.cycle_date;
            row["lat"] = r.latitude;
            row["lon"] = r.longitude;
            row["temp_max_f"] = r.temp_max_f;
            row["temp_min_f"] = r.temp_min_f;
            row["hours_fetched"] = r.hours_fetched;
            j.push_back(row);
        }
        std::cout << j.dump(2) << "\n";
    } else {
        if (rows.empty()) {
            std::cerr << "(no cached forecasts)\n";
        } else {
            printListTable(rows);
        }
    }
    return 0;
}

int runNbmRemote(const std::string& date,
                 int days,
                 const std::string& /*local_cache_dir*/,
                 const std::string& format) {
    api::NbmService service;

    int lookup_days = days > 0 ? days : 10;
    auto remote_result = service.listRemoteCycles(lookup_days);

    if (!remote_result.ok()) {
        std::cerr << "error: " << remote_result.error().message << "\n";
        return 1;
    }

    auto remote_rows = remote_result.value();

    // Filter by date if specified
    if (!date.empty()) {
        remote_rows.erase(
            std::remove_if(remote_rows.begin(), remote_rows.end(),
                           [&date](const api::NbmCycleInfo& r) { return r.date != date; }),
            remote_rows.end());
    }

    // Cross-reference with captured grids
    auto grid_rows = service.listCapturedGrids();
    std::set<std::pair<std::string, int>> cached;
    for (const auto& r : grid_rows) {
        if (r.file_count >= 10) {
            cached.insert({r.cycle_date, r.cycle_hour});
        }
    }

    if (parseFormat(format) == OutputFormat::Json) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& r : remote_rows) {
            nlohmann::json row;
            row["date"] = r.date;
            row["cycle"] = r.cycle_hour;
            row["s3_prefix"] = r.s3_prefix;
            row["cached"] = cached.count({r.date, r.cycle_hour}) > 0;
            j.push_back(row);
        }
        std::cout << j.dump(2) << "\n";
    } else {
        if (remote_rows.empty()) {
            std::cerr << "(no matching cycles found on S3)\n";
        } else {
            printRemoteTable(remote_rows, cached);
        }
    }
    return 0;
}

int runNbmFetch(double lat,
                double lon,
                const std::string& date,
                const std::string& as_of,
                bool force) {
    api::NbmService service;

    // Note: force flag is not implemented (would need to clear cache first)
    auto result = service.fetchDailyForecast(date, lat, lon, -5, as_of);

    if (!result.ok()) {
        std::cerr << "error: " << result.error().message << "\n";
        return 1;
    }

    const auto& f = result.value();
    nlohmann::json j;
    j["date"] = f.target_date;
    j["temp_max_f"] = f.temp_max_f;
    j["temp_min_f"] = f.temp_min_f;
    j["cycle"] = f.cycle_date + "T" + std::to_string(f.cycle_hour) + "Z";
    j["hours_fetched"] = f.hours_fetched;

    std::cout << j.dump(2) << "\n";
    return 0;
}

int runNbmInventory(const std::string& date,
                    int cycle,
                    int forecast_hour,
                    const std::string& format) {
    api::NbmService service;

    auto result = service.inventory(date, cycle, forecast_hour);

    if (!result.ok()) {
        std::cerr << "error: " << result.error().message << "\n";
        return 1;
    }

    const auto& rows = result.value();

    if (parseFormat(format) == OutputFormat::Json) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& r : rows) {
            nlohmann::json row;
            row["shortName"] = r.short_name;
            row["typeOfLevel"] = r.type_of_level;
            row["level"] = r.level;
            row["name"] = r.name;
            j.push_back(row);
        }
        std::cout << j.dump(2) << "\n";
    } else {
        if (rows.empty()) {
            std::cerr << "(no GRIB messages returned)\n";
        } else {
            printInventoryTable(rows);
        }
    }
    return 0;
}

int runNbmCapture(const std::string& date,
                  int cycle,
                  const std::string& forecast_hours_str,
                  const std::string& format) {
    api::NbmService service;

    // Parse forecast hours if specified
    std::vector<int> forecast_hours;
    if (!forecast_hours_str.empty()) {
        if (forecast_hours_str.find('-') != std::string::npos) {
            size_t pos = forecast_hours_str.find('-');
            int start = std::stoi(forecast_hours_str.substr(0, pos));
            int end = std::stoi(forecast_hours_str.substr(pos + 1));
            for (int h = start; h <= end; ++h) {
                forecast_hours.push_back(h);
            }
        } else {
            std::istringstream iss(forecast_hours_str);
            std::string token;
            while (std::getline(iss, token, ',')) {
                forecast_hours.push_back(std::stoi(token));
            }
        }
    }

    // Determine which cycles to capture
    std::vector<std::pair<std::string, int>> cycles_to_capture;
    constexpr int NBM_CYCLES[] = {1, 7, 13, 19};

    if (cycle > 0) {
        cycles_to_capture.push_back({date, cycle});
    } else {
        for (int ch : NBM_CYCLES) {
            cycles_to_capture.push_back({date, ch});
        }
    }

    bool is_json = parseFormat(format) == OutputFormat::Json;
    api::CaptureStats total = {0, 0, 0, ""};

    for (size_t ci = 0; ci < cycles_to_capture.size(); ++ci) {
        const std::string& cycle_date = cycles_to_capture[ci].first;
        int cycle_hour = cycles_to_capture[ci].second;

        if (!is_json) {
            std::cerr << "Capturing " << cycle_date << " " << cycle_hour << "Z...\n";
        }

        // Set up progress callback for table output
        if (!is_json) {
            service.setProgressCallback([cycle_date, cycle_hour](
                int current, int total_files,
                const std::string& /*d*/, int /*h*/,
                int fhr, const std::string& status) {
                int pct = (current * 100) / total_files;
                std::cerr << "\r" << cycle_date << " " << cycle_hour << "Z  f"
                          << std::setw(3) << std::setfill('0') << fhr
                          << " [" << std::setw(3) << std::setfill(' ') << pct << "%] "
                          << status << "    " << std::flush;
            });
        }

        auto result = service.captureCycle(cycle_date, cycle_hour, forecast_hours);

        if (!is_json) {
            std::cerr << "\n";
        }

        if (result.ok()) {
            total.success += result.value().success;
            total.failed += result.value().failed;
            total.skipped += result.value().skipped;
        } else if (!result.value().error.empty()) {
            std::cerr << result.value().error << "\n";
        }
    }

    if (is_json) {
        nlohmann::json j;
        j["success"] = total.success;
        j["failed"] = total.failed;
        j["skipped"] = total.skipped;
        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << "Success: " << total.success
                  << "  Failed: " << total.failed
                  << "  Skipped: " << total.skipped << "\n";
    }

    return total.failed > 0 ? 1 : 0;
}

int runNbmCaptureMissing(int days, const std::string& format) {
    api::NbmService service;

    bool is_json = parseFormat(format) == OutputFormat::Json;

    if (!is_json) {
        std::cerr << "Scanning S3 for last " << days << " days...\n";
    }

    // Get remote cycles for display
    auto remote_result = service.listRemoteCycles(days);
    if (!remote_result.ok()) {
        std::cerr << "error: " << remote_result.error().message << "\n";
        return 1;
    }

    auto captured = service.listCapturedGrids();
    std::set<std::pair<std::string, int>> captured_set;
    for (const auto& c : captured) {
        if (c.file_count >= 10) {
            captured_set.insert({c.cycle_date, c.cycle_hour});
        }
    }

    // Count missing
    std::vector<std::pair<std::string, int>> missing;
    for (const auto& r : remote_result.value()) {
        if (captured_set.count({r.date, r.cycle_hour}) == 0) {
            missing.push_back({r.date, r.cycle_hour});
        }
    }

    if (missing.empty()) {
        if (!is_json) {
            std::cerr << "All available cycles are captured.\n";
        }
        if (is_json) {
            nlohmann::json j;
            j["missing"] = 0;
            j["captured"] = 0;
            std::cout << j.dump(2) << "\n";
        }
        return 0;
    }

    if (!is_json) {
        std::cerr << "Found " << missing.size() << " missing cycles\n\n";
    }

    api::CaptureStats total = {0, 0, 0, ""};
    int num_cycles = static_cast<int>(missing.size());
    auto start_time = std::chrono::steady_clock::now();

    for (int idx = 0; idx < num_cycles; ++idx) {
        const std::string& cycle_date = missing[idx].first;
        int cycle_hour = missing[idx].second;

        if (!is_json) {
            // Set up progress callback
            service.setProgressCallback([idx, num_cycles, cycle_date, cycle_hour, &total](
                int current, int total_files,
                const std::string& /*d*/, int /*h*/,
                int fhr, const std::string& status) {
                int pct = (current * 100) / total_files;
                std::cerr << "\r[" << (idx + 1) << "/" << num_cycles << "] "
                          << cycle_date << " " << cycle_hour << "Z  f"
                          << std::setw(3) << std::setfill('0') << fhr
                          << " [" << std::setw(3) << std::setfill(' ') << pct << "%] "
                          << status << " | +"
                          << total.success << " skip:" << total.skipped
                          << " fail:" << total.failed << "   " << std::flush;
            });
        }

        auto result = service.captureCycle(cycle_date, cycle_hour, {});

        if (!is_json) {
            std::cerr << "\n";
        }

        if (result.ok()) {
            total.success += result.value().success;
            total.failed += result.value().failed;
            total.skipped += result.value().skipped;
        }

        // Show elapsed and estimated time
        if (!is_json && idx > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            double per_cycle = static_cast<double>(elapsed) / (idx + 1);
            auto remaining = static_cast<int>(per_cycle * (num_cycles - idx - 1));

            int elapsed_min = elapsed / 60;
            int elapsed_sec = elapsed % 60;
            int remaining_min = remaining / 60;
            int remaining_sec = remaining % 60;

            std::cerr << "  elapsed: " << elapsed_min << "m"
                      << std::setw(2) << std::setfill('0') << elapsed_sec << "s"
                      << "  remaining: ~" << remaining_min << "m"
                      << std::setw(2) << std::setfill('0') << remaining_sec << "s\n";
        }
    }

    if (is_json) {
        nlohmann::json j;
        j["success"] = total.success;
        j["failed"] = total.failed;
        j["skipped"] = total.skipped;
        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << "Success: " << total.success
                  << "  Failed: " << total.failed
                  << "  Skipped: " << total.skipped << "\n";
    }

    return total.failed > 0 ? 1 : 0;
}

int runNbmCleanup(int older_than_days, const std::string& format) {
    api::NbmService service;

    bool is_json = parseFormat(format) == OutputFormat::Json;

    if (!is_json) {
        std::cerr << "Cleaning up files older than " << older_than_days << " days...\n";
    }

    auto result = service.cleanup(older_than_days);

    if (!result.ok()) {
        std::cerr << "error: " << result.error().message << "\n";
        return 1;
    }

    const auto& stats = result.value();

    if (is_json) {
        nlohmann::json j;
        j["deleted_files"] = stats.deleted_files;
        j["deleted_cycles"] = stats.deleted_cycles;
        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << "Deleted " << stats.deleted_files << " files from "
                  << stats.deleted_cycles << " cycles\n";
    }

    return 0;
}

int runNbmGrids(const std::string& format) {
    api::NbmService service;

    auto rows = service.listCapturedGrids();

    if (parseFormat(format) == OutputFormat::Json) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& r : rows) {
            nlohmann::json row;
            row["cycle_date"] = r.cycle_date;
            row["cycle_hour"] = r.cycle_hour;
            row["file_count"] = r.file_count;
            row["fhr_min"] = r.fhr_min;
            row["fhr_max"] = r.fhr_max;
            j.push_back(row);
        }
        std::cout << j.dump(2) << "\n";
    } else {
        if (rows.empty()) {
            std::cerr << "(no captured grids)\n";
        } else {
            printGridsTable(rows);
        }
    }
    return 0;
}

int runNbmAbout() {
    // ANSI color codes
    const char* RESET   = "\033[0m";
    const char* BOLD    = "\033[1m";
    const char* DIM     = "\033[2m";
    const char* CYAN    = "\033[36m";
    const char* YELLOW  = "\033[33m";
    const char* GREEN   = "\033[32m";
    const char* BLUE    = "\033[34m";
    const char* MAGENTA = "\033[35m";
    const char* WHITE   = "\033[37m";

    std::cout << "\n";
    std::cout << BOLD << CYAN;
    std::cout << "  ███╗   ██╗██████╗ ███╗   ███╗\n";
    std::cout << "  ████╗  ██║██╔══██╗████╗ ████║\n";
    std::cout << "  ██╔██╗ ██║██████╔╝██╔████╔██║\n";
    std::cout << "  ██║╚██╗██║██╔══██╗██║╚██╔╝██║\n";
    std::cout << "  ██║ ╚████║██████╔╝██║ ╚═╝ ██║\n";
    std::cout << "  ╚═╝  ╚═══╝╚═════╝ ╚═╝     ╚═╝\n";
    std::cout << RESET << "\n";

    std::cout << BOLD << WHITE << "  National Blend of Models" << RESET << "\n";
    std::cout << DIM << "  NOAA/NWS Weather Prediction Center" << RESET << "\n\n";

    std::cout << YELLOW << "  What is it?" << RESET << "\n";
    std::cout << "  The NBM blends output from multiple numerical weather models\n";
    std::cout << "  (GFS, NAM, HRRR, ECMWF, etc.) into a single statistically\n";
    std::cout << "  post-processed forecast. It's what powers weather.gov forecasts.\n\n";

    std::cout << YELLOW << "  Why use it?" << RESET << "\n";
    std::cout << "  " << GREEN << "✓" << RESET << " More accurate than any single model\n";
    std::cout << "  " << GREEN << "✓" << RESET << " Bias-corrected using historical observations\n";
    std::cout << "  " << GREEN << "✓" << RESET << " Free, public domain data on AWS S3\n";
    std::cout << "  " << GREEN << "✓" << RESET << " Updated 4x daily at 01Z, 07Z, 13Z, 19Z (5pm, 11pm, 5am, 11am PT)\n\n";

    std::cout << YELLOW << "  Coverage" << RESET << "\n";
    std::cout << "  " << BLUE << "CONUS:" << RESET << "  2.5km grid, hourly out to 36h, 3-hourly to 264h\n";
    std::cout << "  " << BLUE << "Alaska:" << RESET << " 3km grid\n";
    std::cout << "  " << BLUE << "Hawaii:" << RESET << " 2.5km grid\n";
    std::cout << "  " << BLUE << "Puerto Rico:" << RESET << " 1.25km grid\n\n";

    std::cout << YELLOW << "  S3 Bucket" << RESET << "\n";
    std::cout << "  " << MAGENTA << "s3://noaa-nbm-grib2-pds" << RESET << "\n";
    std::cout << "  Public, no authentication required. ~10 days retained.\n\n";

    std::cout << YELLOW << "  Cycle Timeline (Pacific Time)" << RESET << "\n";

    // Use DateTime to get correct PST/PDT times
    auto now = core::DateTime::now();
    bool is_dst = core::PacificTime::isDst(now);
    std::string tz = is_dst ? "PDT" : "PST";

    auto formatCycleTime = [](int utc_hour) -> std::string {
        // Create a DateTime for today at this UTC hour
        auto today = core::DateTime::now();
        auto dt = core::DateTime::parseDate(today.toDateString());
        if (!dt) return "?";
        auto cycle_time = dt->addHours(utc_hour);
        return core::PacificTime::formatTime(cycle_time, true);
    };

    // Build table with fixed-width columns
    // Col widths (excluding │): Cycle=7, Issued=12, Available=14
    std::cout << DIM << "  ┌───────┬────────────┬──────────────┐\n" << RESET;
    std::cout << DIM << "  │" << RESET << " Cycle " << DIM << "│" << RESET
              << " Issued     " << DIM << "│" << RESET
              << " Available    " << DIM << "│\n" << RESET;
    std::cout << DIM << "  ├───────┼────────────┼──────────────┤\n" << RESET;

    constexpr int cycles[] = {1, 7, 13, 19};
    for (int c : cycles) {
        std::string issued = formatCycleTime(c) + " " + tz;
        std::string avail = "~" + formatCycleTime(c + 2) + " " + tz;

        // Pad to exact column widths (excluding leading space)
        while (issued.length() < 11) issued += " ";
        while (avail.length() < 13) avail += " ";

        char cycle_str[16];
        std::snprintf(cycle_str, sizeof(cycle_str), "  %02dZ  ", c);  // 7 chars total

        std::cout << DIM << "  │" << RESET << cycle_str
                  << DIM << "│" << RESET << " " << issued
                  << DIM << "│" << RESET << " " << avail
                  << DIM << "│\n" << RESET;
    }

    std::cout << DIM << "  └───────┴────────────┴──────────────┘" << RESET << "\n\n";

    std::cout << YELLOW << "  Learn More" << RESET << "\n";
    std::cout << "  https://vlab.noaa.gov/web/mdl/nbm\n";
    std::cout << "  https://registry.opendata.aws/noaa-nbm\n\n";

    return 0;
}

}  // namespace predibloom::cli
