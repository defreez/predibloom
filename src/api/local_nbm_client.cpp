#include "local_nbm_client.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

namespace predibloom::api {

namespace {

std::string default_nbm_base_path() {
    const char* home = std::getenv("HOME");
    if (!home) {
        return "";
    }
    return std::string(home) + "/.cache/predibloom/nbm";
}

// Parse YYYY-MM-DD to tm struct.
std::tm parse_date(const std::string& date) {
    std::tm tm = {};
    std::istringstream ss(date);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    return tm;
}

// Parse ISO-8601 timestamp to time_t (UTC).
std::time_t parse_iso_time(const std::string& iso) {
    std::tm tm = {};
    std::string s = iso;
    // Remove trailing 'Z' if present.
    if (!s.empty() && s.back() == 'Z') {
        s.pop_back();
    }

    // Try various formats.
    for (const char* fmt : {"%Y-%m-%dT%H:%M:%S", "%Y-%m-%dT%H:%M", "%Y-%m-%dT%H"}) {
        std::istringstream ss(s);
        ss >> std::get_time(&tm, fmt);
        if (!ss.fail()) {
            return timegm(&tm);
        }
    }
    return 0;
}

// Format tm as YYYY-MM-DD.
std::string format_date(const std::tm& tm) {
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d");
    return ss.str();
}

}  // namespace

LocalNbmClient::LocalNbmClient() {
    try {
        db_ = std::make_unique<ForecastDb>();
    } catch (const std::exception&) {
        // Database open failed; leave db_ null.
    }

    // Initialize grid reader if base path exists
    std::string base_path = default_nbm_base_path();
    if (!base_path.empty()) {
        grid_reader_ = std::make_unique<NbmGridReader>(base_path);
    }
}

LocalNbmClient::LocalNbmClient(const std::string& db_path) {
    try {
        db_ = std::make_unique<ForecastDb>(db_path);
    } catch (const std::exception&) {
        // Database open failed; leave db_ null.
    }

    // Initialize grid reader if base path exists
    std::string base_path = default_nbm_base_path();
    if (!base_path.empty()) {
        grid_reader_ = std::make_unique<NbmGridReader>(base_path);
    }
}

LocalNbmClient::~LocalNbmClient() = default;

void LocalNbmClient::setDbPath(const std::string& path) {
    try {
        db_ = std::make_unique<ForecastDb>(path);
    } catch (const std::exception&) {
        db_.reset();
    }
}

void LocalNbmClient::setGridPath(const std::string& path) {
    if (path.empty()) {
        grid_reader_.reset();
    } else {
        grid_reader_ = std::make_unique<NbmGridReader>(path);
    }
}

bool LocalNbmClient::is_open() const {
    return db_ && db_->is_open();
}

std::pair<std::string, int> LocalNbmClient::findBestCycle(const std::string& target_date,
                                                           const std::string& asOf_iso) {
    // NBM cycles: 01, 07, 13, 19 UTC.
    // Default: use previous day's 19Z cycle.
    std::tm target = parse_date(target_date);
    std::time_t target_t = timegm(&target);

    if (asOf_iso.empty()) {
        // Use previous day's 19Z cycle.
        std::time_t prev_day = target_t - 86400;
        std::tm* prev_tm = gmtime(&prev_day);
        return {format_date(*prev_tm), 19};
    }

    // Parse as-of time and find most recent available cycle.
    std::time_t as_of_t = parse_iso_time(asOf_iso);
    if (as_of_t == 0) {
        // Parse failed; fall back to default.
        std::time_t prev_day = target_t - 86400;
        std::tm* prev_tm = gmtime(&prev_day);
        return {format_date(*prev_tm), 19};
    }

    // Account for ~2 hour delay in cycle availability.
    std::time_t effective = as_of_t - 2 * 3600;
    std::tm* eff_tm = gmtime(&effective);

    int hour = eff_tm->tm_hour;
    int cycle_hour;
    std::time_t cycle_date_t = effective;

    if (hour < 1) {
        // Before 01Z: use previous day's 19Z.
        cycle_date_t = effective - 86400;
        cycle_hour = 19;
    } else if (hour < 7) {
        cycle_hour = 1;
    } else if (hour < 13) {
        cycle_hour = 7;
    } else if (hour < 19) {
        cycle_hour = 13;
    } else {
        cycle_hour = 19;
    }

    std::tm* cycle_tm = gmtime(&cycle_date_t);
    return {format_date(*cycle_tm), cycle_hour};
}

std::vector<int> LocalNbmClient::computeForecastHours(const std::string& cycle_date,
                                                        int cycle_hour,
                                                        const std::string& target_date) {
    // Parse cycle datetime
    std::tm cycle_tm = parse_date(cycle_date);
    cycle_tm.tm_hour = cycle_hour;
    std::time_t cycle_t = timegm(&cycle_tm);

    // Parse target date and compute NYC midnight (05:00 UTC for EST)
    std::tm target_tm = parse_date(target_date);
    target_tm.tm_hour = 5;  // NYC midnight ~= 05:00 UTC
    std::time_t target_start = timegm(&target_tm);
    std::time_t target_end = target_start + 24 * 3600;

    std::vector<int> hours;
    for (int h = 1; h <= 264; ++h) {
        std::time_t valid_time = cycle_t + h * 3600;
        if (valid_time >= target_start && valid_time < target_end) {
            hours.push_back(h);
        }
    }
    return hours;
}

Result<WeatherResponse> LocalNbmClient::fetchFromGrid(double latitude,
                                                       double longitude,
                                                       const std::string& target_date,
                                                       const std::string& cycle_date,
                                                       int cycle_hour) {
    if (!grid_reader_) {
        return Error(ApiError::NetworkError, "Grid reader not available");
    }

    auto hours = computeForecastHours(cycle_date, cycle_hour, target_date);
    if (hours.empty()) {
        return Error(ApiError::HttpError, "No forecast hours found for date");
    }

    std::vector<double> temps_k;
    for (int fhr : hours) {
        auto temp = grid_reader_->getTemp(cycle_date, cycle_hour, fhr, latitude, longitude);
        if (temp) {
            temps_k.push_back(*temp);
        }
    }

    if (temps_k.empty()) {
        return Error(ApiError::HttpError, "No temperature data in grid files");
    }

    // Convert K to F
    std::vector<double> temps_f;
    temps_f.reserve(temps_k.size());
    for (double k : temps_k) {
        temps_f.push_back((k - 273.15) * 9.0 / 5.0 + 32.0);
    }

    double temp_max = *std::max_element(temps_f.begin(), temps_f.end());
    double temp_min = *std::min_element(temps_f.begin(), temps_f.end());

    // Round to 1 decimal place
    temp_max = std::round(temp_max * 10.0) / 10.0;
    temp_min = std::round(temp_min * 10.0) / 10.0;

    // Cache to SQLite
    if (db_ && db_->is_open()) {
        DailyForecast f;
        f.source = "nbm";
        f.target_date = target_date;
        f.latitude = latitude;
        f.longitude = longitude;
        f.temp_max_f = temp_max;
        f.temp_min_f = temp_min;
        f.cycle_hour = cycle_hour;
        f.cycle_date = cycle_date;
        f.hours_fetched = static_cast<int>(temps_k.size());
        db_->putNbm(f);
    }

    WeatherResponse resp;
    resp.latitude = latitude;
    resp.longitude = longitude;
    resp.timezone = "America/New_York";
    resp.daily.time = {target_date};
    resp.daily.temperature_2m_max = {temp_max};
    resp.daily.temperature_2m_min = {temp_min};
    return resp;
}

Result<WeatherResponse> LocalNbmClient::getForecast(double latitude,
                                                     double longitude,
                                                     const std::string& date,
                                                     const std::string& asOf_iso) {
    auto [cycle_date, cycle_hour] = findBestCycle(date, asOf_iso);

    // Try SQLite cache first
    if (db_ && db_->is_open()) {
        auto forecast = db_->getNbm(date, cycle_hour, latitude, longitude);
        if (forecast) {
            WeatherResponse resp;
            resp.latitude = forecast->latitude;
            resp.longitude = forecast->longitude;
            resp.timezone = "America/New_York";
            resp.daily.time = {date};
            resp.daily.temperature_2m_max = {forecast->temp_max_f};
            resp.daily.temperature_2m_min = {forecast->temp_min_f};
            return resp;
        }
    }

    // Fall back to grid files
    if (grid_reader_) {
        auto result = fetchFromGrid(latitude, longitude, date, cycle_date, cycle_hour);
        if (result.ok()) {
            return result;
        }
    }

    // Neither cache nor grid available
    std::ostringstream msg;
    msg << "No forecast available for " << date << " cycle " << cycle_hour
        << " at (" << latitude << ", " << longitude << "). "
        << "Run `nbm capture` to download grids or `nbm download` to fetch points.";
    return Error(ApiError::HttpError, msg.str());
}

Result<WeatherResponse> LocalNbmClient::getActuals(double latitude,
                                                    double longitude,
                                                    const std::string& date) {
    std::string asOf = date + "T23:59:00Z";
    return getForecast(latitude, longitude, date, asOf);
}

}  // namespace predibloom::api
