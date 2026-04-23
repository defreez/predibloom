#include "local_nbm_client.hpp"
#include "../core/time_utils.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>

namespace predibloom::api {

LocalNbmClient::LocalNbmClient() = default;
LocalNbmClient::~LocalNbmClient() = default;

Result<WeatherResponse> LocalNbmClient::getForecast(double latitude,
                                                      double longitude,
                                                      const std::string& date,
                                                      const std::string& asOf_iso) {
    return runScript(latitude, longitude, date, asOf_iso);
}

Result<WeatherResponse> LocalNbmClient::getActuals(double latitude,
                                                     double longitude,
                                                     const std::string& date) {
    // For actuals, use an as-of time at end of the target date to get the
    // shortest-lead forecast (closest to actual observations)
    std::string asOf = date + "T23:59:00Z";
    return runScript(latitude, longitude, date, asOf);
}

Result<WeatherResponse> LocalNbmClient::runScript(double latitude,
                                                    double longitude,
                                                    const std::string& date,
                                                    const std::string& asOf_iso) {
    // Build command
    std::ostringstream cmd;
    cmd << "python3 " << script_path_;
    cmd << " --lat " << latitude;
    cmd << " --lon " << longitude;
    cmd << " --date " << date;

    if (!asOf_iso.empty()) {
        cmd << " --as-of " << asOf_iso;
    }

    if (!cache_dir_.empty()) {
        cmd << " --cache-dir " << cache_dir_;
    }

    cmd << " 2>/dev/null";  // Suppress stderr warnings

    // Execute and capture output
    std::array<char, 4096> buffer;
    std::string output;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen(cmd.str().c_str(), "r"), pclose);

    if (!pipe) {
        return Error(ApiError::NetworkError, "Failed to execute nbm_fetch.py");
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        output += buffer.data();
    }

    int status = pclose(pipe.release());

    // Parse JSON output
    if (output.empty()) {
        return Error(ApiError::ParseError, "nbm_fetch.py returned no output");
    }

    try {
        nlohmann::json j = nlohmann::json::parse(output);

        // Check for error
        if (j.contains("error")) {
            return Error(ApiError::HttpError, j["error"].get<std::string>());
        }

        // Extract temperatures
        if (!j.contains("temp_max_f") || !j.contains("temp_min_f")) {
            return Error(ApiError::ParseError, "Missing temperature fields in response");
        }

        WeatherResponse resp;
        resp.latitude = latitude;
        resp.longitude = longitude;
        resp.timezone = "America/New_York";
        resp.daily.time = {date};
        resp.daily.temperature_2m_max = {j["temp_max_f"].get<double>()};
        resp.daily.temperature_2m_min = {j["temp_min_f"].get<double>()};

        return resp;

    } catch (const nlohmann::json::parse_error& e) {
        return Error(ApiError::ParseError,
            std::string("Failed to parse JSON: ") + e.what() + " (output: " + output + ")");
    }
}

} // namespace predibloom::api
