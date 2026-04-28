#include "local_nbm_client.hpp"
#include "../core/datetime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
    using core::DateTime;
    using core::NbmCycle;

    if (asOf_iso.empty()) {
        // Default: use previous day's 19Z cycle
        auto cycle = NbmCycle::forTargetDate(target_date);
        return {cycle.date(), cycle.hour()};
    }

    // Parse as-of time and find most recent available cycle
    auto as_of = DateTime::parseIso(asOf_iso);
    if (!as_of) {
        // Parse failed; fall back to default
        auto cycle = NbmCycle::forTargetDate(target_date);
        return {cycle.date(), cycle.hour()};
    }

    auto cycle = NbmCycle::availableAt(*as_of);
    return {cycle.date(), cycle.hour()};
}

std::vector<int> LocalNbmClient::computeForecastHours(const std::string& cycle_date,
                                                        int cycle_hour,
                                                        const std::string& target_date,
                                                        const std::string& timezone) {
    core::NbmCycle cycle(cycle_date, cycle_hour);
    return cycle.forecastHoursFor(target_date, timezone);
}

Result<WeatherResponse> LocalNbmClient::fetchFromGrid(double latitude,
                                                       double longitude,
                                                       const std::string& target_date,
                                                       const std::string& cycle_date,
                                                       int cycle_hour,
                                                       const std::string& timezone) {
    using core::DateTime;

    if (!grid_reader_) {
        return Error(ApiError::NetworkError, "Grid reader not available");
    }

    auto hours = computeForecastHours(cycle_date, cycle_hour, target_date, timezone);
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

    // Reject cycles that don't substantially cover the target's local day.
    // A complete local day has ~24 forecast hours; require most of them so we
    // don't treat a single hour at the day boundary as the "daily" min/max.
    constexpr size_t kMinCoverageHours = 20;
    if (temps_k.size() < kMinCoverageHours) {
        return Error(ApiError::HttpError,
                     "Insufficient coverage: only " + std::to_string(temps_k.size()) +
                     " hours of cycle " + cycle_date + " " +
                     std::to_string(cycle_hour) + "Z fall inside the local day for " +
                     target_date);
    }

    // Convert K to F and track max/min indices
    std::vector<double> temps_f;
    temps_f.reserve(temps_k.size());
    for (double k : temps_k) {
        temps_f.push_back((k - 273.15) * 9.0 / 5.0 + 32.0);
    }

    auto max_it = std::max_element(temps_f.begin(), temps_f.end());
    auto min_it = std::min_element(temps_f.begin(), temps_f.end());
    double temp_max = *max_it;
    double temp_min = *min_it;
    size_t max_idx = std::distance(temps_f.begin(), max_it);
    size_t min_idx = std::distance(temps_f.begin(), min_it);

    // Compute local times of max/min using DateTime
    auto cycle_dt = DateTime::parseDate(cycle_date);
    if (!cycle_dt) {
        return Error(ApiError::ParseError, "Invalid cycle date");
    }
    DateTime cycle_time = cycle_dt->addHours(cycle_hour);

    auto format_local_time = [&](size_t idx) -> std::string {
        int fhr = hours[idx];
        DateTime valid_utc = cycle_time.addHours(fhr);
        return core::formatLocalHourMinute(valid_utc, timezone);
    };

    std::string time_of_max = format_local_time(max_idx);
    std::string time_of_min = format_local_time(min_idx);

    // Round to 1 decimal place
    temp_max = std::round(temp_max * 10.0) / 10.0;
    temp_min = std::round(temp_min * 10.0) / 10.0;

    // Store to SQLite
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
        f.time_of_max = time_of_max;
        f.time_of_min = time_of_min;
        db_->putNbm(f);
    }

    WeatherResponse resp;
    resp.latitude = latitude;
    resp.longitude = longitude;
    resp.timezone = timezone;
    resp.daily.time = {target_date};
    resp.daily.temperature_2m_max = {temp_max};
    resp.daily.temperature_2m_min = {temp_min};
    resp.daily.time_of_max = {time_of_max};
    resp.daily.time_of_min = {time_of_min};
    return resp;
}

Result<WeatherResponse> LocalNbmClient::getForecast(double latitude,
                                                     double longitude,
                                                     const std::string& date,
                                                     const std::string& timezone,
                                                     const std::string& asOf_iso) {
    using core::DateTime;
    using core::NbmCycle;

    auto [start_cycle_date, start_cycle_hour] = findBestCycle(date, asOf_iso);

    // Walk back through cycles (6 hours apart) until one covers the target's
    // local day. Today's 13Z cannot predict today's daily min/max because
    // ~midnight–morning local already happened, so coverage is < 20 hours and
    // fetchFromGrid rejects. Fall back to 07Z, 01Z, yesterday's 19Z, etc.
    DateTime probe = NbmCycle(start_cycle_date, start_cycle_hour).nominalTime();
    std::string last_cycle_date = start_cycle_date;
    int last_cycle_hour = start_cycle_hour;

    constexpr int kMaxCycleFallbacks = 6;  // 6 cycles = ~1.5 days back
    for (int attempt = 0; attempt < kMaxCycleFallbacks; ++attempt) {
        NbmCycle c(probe);
        last_cycle_date = c.date();
        last_cycle_hour = c.hour();

        // Try SQLite database first
        if (db_ && db_->is_open()) {
            auto forecast = db_->getNbm(date, last_cycle_hour, latitude, longitude);
            if (forecast && !forecast->time_of_max.empty() &&
                !forecast->time_of_min.empty()) {
                WeatherResponse resp;
                resp.latitude = forecast->latitude;
                resp.longitude = forecast->longitude;
                resp.timezone = timezone;
                resp.daily.time = {date};
                resp.daily.temperature_2m_max = {forecast->temp_max_f};
                resp.daily.temperature_2m_min = {forecast->temp_min_f};
                resp.daily.time_of_max = {forecast->time_of_max};
                resp.daily.time_of_min = {forecast->time_of_min};
                return resp;
            }
        }

        // Fall back to grid files
        if (grid_reader_) {
            auto result = fetchFromGrid(latitude, longitude, date,
                                          last_cycle_date, last_cycle_hour, timezone);
            if (result.ok()) {
                return result;
            }
        }

        // Step back 6 hours to the previous NBM cycle and try again.
        probe = probe.addHours(-6);
    }

    // No cycle in the search window had sufficient coverage. Most often this
    // means we don't have grid files for cycles old enough to cover the
    // target date — usually fixed by `weather nbm update` or `nbm capture`.
    std::ostringstream msg;
    msg << "No forecast available for " << date
        << " (tried " << kMaxCycleFallbacks << " cycles back from "
        << start_cycle_date << " " << start_cycle_hour << "Z) "
        << "at (" << latitude << ", " << longitude << "). "
        << "Run `weather nbm update` to refresh incomplete cycles from S3, "
        << "or `weather nbm capture --date " << last_cycle_date
        << " --cycle " << last_cycle_hour << "` to force-fetch this specific cycle.";
    return Error(ApiError::HttpError, msg.str());
}

Result<WeatherResponse> LocalNbmClient::getActuals(double latitude,
                                                    double longitude,
                                                    const std::string& date,
                                                    const std::string& timezone) {
    std::string asOf = date + "T23:59:00Z";
    return getForecast(latitude, longitude, date, timezone, asOf);
}

}  // namespace predibloom::api
