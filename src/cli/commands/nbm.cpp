#include "nbm.hpp"
#include "../../api/local_nbm_client.hpp"
#include "../../core/time_utils.hpp"
#include "../formatters.hpp"

#include <nlohmann/json.hpp>

#include <unistd.h>
#include <array>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace predibloom::cli {

namespace {

// Get path to scripts directory relative to current working directory
std::string getScriptsDir() {
    const char* paths[] = {
        "scripts",           // Running from project root
        "../scripts",        // Running from build/
    };
    for (const char* p : paths) {
        std::string pypath = std::string(p) + "/nbm_fetch.py";
        if (access(pypath.c_str(), F_OK) == 0) {
            return p;
        }
    }
    return "scripts";
}

// Run nbm_fetch.py script with given arguments and parse JSON output.
int runScriptJson(const std::vector<std::string>& args, nlohmann::json& parsed) {
    std::ostringstream cmd;
    cmd << "cd " << getScriptsDir() << " && uv run python nbm_fetch.py";
    for (const auto& a : args) {
        cmd << " " << a;
    }
    cmd << " --format json 2>/dev/null";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        std::cerr << "error: failed to execute nbm_fetch.py\n";
        return 1;
    }

    std::string out;
    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        out += buffer.data();
    }
    int status = pclose(pipe);

    if (out.empty()) {
        std::cerr << "error: nbm_fetch.py returned no output (exit " << status << ")\n";
        return 1;
    }
    try {
        parsed = nlohmann::json::parse(out);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "error: failed to parse nbm_fetch.py output: " << e.what() << "\n";
        std::cerr << "output: " << out << "\n";
        return 1;
    }
    if (parsed.is_object() && parsed.contains("error")) {
        std::cerr << "error: " << parsed["error"].get<std::string>() << "\n";
        return 1;
    }
    return 0;
}

void printListTable(const nlohmann::json& rows) {
    std::cout << std::left
              << std::setw(12) << "Date"
              << std::setw(7)  << "Cycle"
              << std::right
              << std::setw(10) << "Lat"
              << std::setw(11) << "Lon"
              << std::setw(8)  << "Max"
              << std::setw(8)  << "Min"
              << std::setw(7)  << "Hrs"
              << "\n";
    std::cout << std::string(63, '-') << "\n";
    for (const auto& r : rows) {
        std::cout << std::left
                  << std::setw(12) << r.value("date", std::string{"?"})
                  << std::setw(7)  << (std::to_string(r.value("cycle", 0)) + "Z");
        std::cout << std::right << std::fixed << std::setprecision(3)
                  << std::setw(10) << r.value("lat", 0.0)
                  << std::setw(11) << r.value("lon", 0.0);
        std::cout << std::setprecision(1)
                  << std::setw(8) << r.value("temp_max_f", 0.0)
                  << std::setw(8) << r.value("temp_min_f", 0.0);
        int hrs = r.value("hours_fetched", 0);
        std::cout << std::setw(7) << hrs << "\n";
    }
}

void printRemoteTable(const nlohmann::json& rows,
                      const std::set<std::pair<std::string, int>>& cached) {
    std::cout << std::left
              << std::setw(12) << "Date"
              << std::setw(7)  << "Cycle"
              << std::setw(10) << "Status"
              << "S3 prefix\n";
    std::cout << std::string(80, '-') << "\n";
    for (const auto& r : rows) {
        const std::string date  = r.value("date", std::string{"?"});
        const int cycle         = r.value("cycle", 0);
        const std::string s3    = r.value("s3_prefix", std::string{""});
        const bool hit = cached.count({date, cycle}) > 0;
        std::cout << std::left
                  << std::setw(12) << date
                  << std::setw(7)  << (std::to_string(cycle) + "Z")
                  << std::setw(10) << (hit ? "cached" : "missing")
                  << s3 << "\n";
    }
}

void printInventoryTable(const nlohmann::json& rows) {
    std::cout << std::left
              << std::setw(14) << "shortName"
              << std::setw(22) << "typeOfLevel"
              << std::right << std::setw(7) << "level"
              << "  " << std::left << "name\n";
    std::cout << std::string(80, '-') << "\n";
    for (const auto& r : rows) {
        std::cout << std::left
                  << std::setw(14) << r.value("shortName", std::string{"?"})
                  << std::setw(22) << r.value("typeOfLevel", std::string{"?"})
                  << std::right << std::setw(7) << r.value("level", 0)
                  << "  " << std::left << r.value("name", std::string{""}) << "\n";
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

            auto result = nbm_client.getForecast(sc->latitude, sc->longitude, date, as_of);
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
    std::vector<std::string> args = {"list-cache"};
    if (!date.empty()) { args.push_back("--date"); args.push_back(date); }
    if (!lat.empty())  { args.push_back("--lat");  args.push_back(lat); }
    if (!lon.empty())  { args.push_back("--lon");  args.push_back(lon); }

    nlohmann::json rows;
    int rc = runScriptJson(args, rows);
    if (rc != 0) return rc;

    if (!rows.is_array()) {
        std::cerr << "error: expected JSON array from list-cache\n";
        return 1;
    }

    if (parseFormat(format) == OutputFormat::Json) {
        std::cout << rows.dump(2) << "\n";
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
    std::vector<std::string> args = {"list-remote"};
    if (!date.empty()) {
        args.push_back("--date");
        args.push_back(date);
    } else if (days > 0) {
        args.push_back("--days");
        args.push_back(std::to_string(days));
    }

    nlohmann::json remote_rows;
    int rc = runScriptJson(args, remote_rows);
    if (rc != 0) return rc;
    if (!remote_rows.is_array()) {
        std::cerr << "error: expected JSON array from list-remote\n";
        return 1;
    }

    // Cross-reference with captured grids (not point forecast cache).
    // A cycle is "cached" only if we have grid files for it.
    nlohmann::json grid_rows;
    std::set<std::pair<std::string, int>> cached;
    if (runScriptJson({"grids"}, grid_rows) == 0 && grid_rows.is_array()) {
        for (const auto& r : grid_rows) {
            // Only consider it cached if we have a reasonable number of forecast hours
            int file_count = r.value("file_count", 0);
            if (file_count >= 10) {  // At least 10 forecast hours
                cached.insert({r.value("cycle_date", std::string{}), r.value("cycle_hour", 0)});
            }
        }
    }

    if (parseFormat(format) == OutputFormat::Json) {
        for (auto& r : remote_rows) {
            const std::string d = r.value("date", std::string{});
            const int c         = r.value("cycle", 0);
            r["cached"] = cached.count({d, c}) > 0;
        }
        std::cout << remote_rows.dump(2) << "\n";
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
    std::vector<std::string> args = {
        "fetch",
        "--lat", formatDouble(lat),
        "--lon", formatDouble(lon),
        "--date", date,
    };
    if (!as_of.empty()) {
        args.push_back("--as-of");
        args.push_back(as_of);
    }
    if (force) {
        args.push_back("--no-cache");
    }

    nlohmann::json result;
    int rc = runScriptJson(args, result);
    if (rc != 0) return rc;
    std::cout << result.dump(2) << "\n";
    return 0;
}

int runNbmInventory(const std::string& date,
                    int cycle,
                    int forecast_hour,
                    const std::string& format) {
    std::vector<std::string> args = {
        "inventory",
        "--date", date,
        "--cycle", std::to_string(cycle),
        "--forecast-hour", std::to_string(forecast_hour),
    };

    nlohmann::json rows;
    int rc = runScriptJson(args, rows);
    if (rc != 0) return rc;
    if (!rows.is_array()) {
        std::cerr << "error: expected JSON array from inventory\n";
        return 1;
    }

    if (parseFormat(format) == OutputFormat::Json) {
        std::cout << rows.dump(2) << "\n";
    } else {
        if (rows.empty()) {
            std::cerr << "(no GRIB messages returned)\n";
        } else {
            printInventoryTable(rows);
        }
    }
    return 0;
}

namespace {

// Run Python script with streaming output (not JSON).
int runScriptStreaming(const std::vector<std::string>& args) {
    std::ostringstream cmd;
    cmd << "cd " << getScriptsDir() << " && uv run python nbm_fetch.py";
    for (const auto& a : args) {
        cmd << " " << a;
    }

    int rc = std::system(cmd.str().c_str());
    return WEXITSTATUS(rc);
}

void printGridsTable(const nlohmann::json& rows) {
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
                  << std::setw(12) << r.value("cycle_date", std::string{"?"})
                  << std::setw(7)  << (std::to_string(r.value("cycle_hour", 0)) + "Z")
                  << std::right
                  << std::setw(8)  << r.value("file_count", 0)
                  << std::setw(10) << r.value("fhr_min", 0)
                  << std::setw(10) << r.value("fhr_max", 0)
                  << "\n";
    }
}

}  // namespace

int runNbmCapture(const std::string& date,
                  int cycle,
                  const std::string& forecast_hours,
                  const std::string& format) {
    std::vector<std::string> args = {"capture", "--date", date};

    if (cycle > 0) {
        args.push_back("--cycle");
        args.push_back(std::to_string(cycle));
    }

    if (!forecast_hours.empty()) {
        args.push_back("--forecast-hours");
        args.push_back(forecast_hours);
    }

    // Use streaming for table output (progress indicators)
    if (parseFormat(format) == OutputFormat::Json) {
        nlohmann::json result;
        int rc = runScriptJson(args, result);
        if (rc != 0) return rc;
        std::cout << result.dump(2) << "\n";
        return 0;
    } else {
        return runScriptStreaming(args);
    }
}

int runNbmCaptureMissing(int days, const std::string& format) {
    std::vector<std::string> args = {
        "capture-missing",
        "--days", std::to_string(days),
    };

    // Use streaming for table output (progress indicators)
    if (parseFormat(format) == OutputFormat::Json) {
        nlohmann::json result;
        int rc = runScriptJson(args, result);
        if (rc != 0) return rc;
        std::cout << result.dump(2) << "\n";
        return 0;
    } else {
        return runScriptStreaming(args);
    }
}

int runNbmCleanup(int older_than_days, const std::string& format) {
    std::vector<std::string> args = {
        "cleanup",
        "--older-than", std::to_string(older_than_days),
    };

    nlohmann::json result;
    int rc = runScriptJson(args, result);
    if (rc != 0) return rc;

    if (parseFormat(format) == OutputFormat::Json) {
        std::cout << result.dump(2) << "\n";
    } else {
        int files = result.value("deleted_files", 0);
        int cycles = result.value("deleted_cycles", 0);
        std::cout << "Deleted " << files << " files from " << cycles << " cycles\n";
    }
    return 0;
}

int runNbmGrids(const std::string& format) {
    std::vector<std::string> args = {"grids"};

    nlohmann::json rows;
    int rc = runScriptJson(args, rows);
    if (rc != 0) return rc;
    if (!rows.is_array()) {
        std::cerr << "error: expected JSON array from grids\n";
        return 1;
    }

    if (parseFormat(format) == OutputFormat::Json) {
        std::cout << rows.dump(2) << "\n";
    } else {
        if (rows.empty()) {
            std::cerr << "(no captured grids)\n";
        } else {
            printGridsTable(rows);
        }
    }
    return 0;
}

}  // namespace predibloom::cli
