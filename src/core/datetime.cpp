#include "datetime.hpp"

#include <date/tz.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace predibloom::core {

namespace {

const char* MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Thread-safe timezone conversion helper
// Sets TZ temporarily to get correct DST handling
struct TzGuard {
    TzGuard(const char* tz) {
        char* orig = std::getenv("TZ");
        if (orig) {
            saved_tz_ = orig;
            had_tz_ = true;
        }
        setenv("TZ", tz, 1);
        tzset();
    }

    ~TzGuard() {
        if (had_tz_) {
            setenv("TZ", saved_tz_.c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }

private:
    std::string saved_tz_;
    bool had_tz_ = false;
};

}  // namespace

// =============================================================================
// DateTime implementation
// =============================================================================

DateTime DateTime::now() {
    auto tp = std::chrono::system_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
        tp.time_since_epoch()).count();
    return DateTime(epoch);
}

std::optional<DateTime> DateTime::parseDate(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return std::nullopt;
    }

    std::tm tm = {};
    try {
        tm.tm_year = std::stoi(date.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(date.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(date.substr(8, 2));
    } catch (...) {
        return std::nullopt;
    }

    tm.tm_isdst = 0;
    time_t t = timegm(&tm);
    if (t == -1) return std::nullopt;

    return DateTime(t);
}

std::optional<DateTime> DateTime::parseIso(const std::string& iso) {
    if (iso.empty()) return std::nullopt;

    std::string s = iso;
    if (s.back() == 'Z') s.pop_back();

    std::tm tm = {};

    // Try YYYY-MM-DDTHH:MM:SS
    if (s.size() >= 19) {
        std::istringstream ss(s);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (!ss.fail()) {
            return DateTime(timegm(&tm));
        }
    }

    // Try YYYY-MM-DDTHH:MM
    if (s.size() >= 16) {
        std::istringstream ss(s);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M");
        if (!ss.fail()) {
            return DateTime(timegm(&tm));
        }
    }

    // Try YYYY-MM-DDTHH
    if (s.size() >= 13) {
        std::istringstream ss(s);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H");
        if (!ss.fail()) {
            return DateTime(timegm(&tm));
        }
    }

    return std::nullopt;
}

std::optional<DateTime> DateTime::parseDateHour(const std::string& dt) {
    if (dt.size() < 13 || dt[10] != 'T') return std::nullopt;

    std::tm tm = {};
    try {
        tm.tm_year = std::stoi(dt.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(dt.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(dt.substr(8, 2));
        tm.tm_hour = std::stoi(dt.substr(11, 2));
    } catch (...) {
        return std::nullopt;
    }

    tm.tm_isdst = 0;
    return DateTime(timegm(&tm));
}

std::tm DateTime::toUtcTm() const {
    std::tm tm = {};
    time_t t = static_cast<time_t>(epoch_);
    gmtime_r(&t, &tm);
    return tm;
}

std::string DateTime::toDateString() const {
    std::tm tm = toUtcTm();
    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

std::string DateTime::toIsoString() const {
    std::tm tm = toUtcTm();
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::string DateTime::toDateHour() const {
    std::tm tm = toUtcTm();
    char buf[14];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour);
    return buf;
}

int DateTime::year() const { return toUtcTm().tm_year + 1900; }
int DateTime::month() const { return toUtcTm().tm_mon + 1; }
int DateTime::day() const { return toUtcTm().tm_mday; }
int DateTime::hour() const { return toUtcTm().tm_hour; }
int DateTime::minute() const { return toUtcTm().tm_min; }
int DateTime::second() const { return toUtcTm().tm_sec; }

DateTime DateTime::addDays(int days) const {
    return DateTime(epoch_ + days * 24 * 3600);
}

DateTime DateTime::addHours(int hours) const {
    return DateTime(epoch_ + hours * 3600);
}

DateTime DateTime::addSeconds(int64_t seconds) const {
    return DateTime(epoch_ + seconds);
}

double DateTime::hoursUntil(const DateTime& other) const {
    return static_cast<double>(other.epoch_ - epoch_) / 3600.0;
}

// =============================================================================
// PacificTime implementation
// =============================================================================

void PacificTime::toLocal(const DateTime& utc, int& year, int& month, int& day,
                           int& hour, int& minute, int& second, bool& is_dst) {
    TzGuard guard("America/Los_Angeles");

    time_t t = static_cast<time_t>(utc.epoch());
    std::tm local = {};
    localtime_r(&t, &local);

    year = local.tm_year + 1900;
    month = local.tm_mon + 1;
    day = local.tm_mday;
    hour = local.tm_hour;
    minute = local.tm_min;
    second = local.tm_sec;
    is_dst = local.tm_isdst > 0;
}

std::string PacificTime::format(const DateTime& utc, bool show_dst_suffix) {
    int year, month, day, hour, minute, second;
    bool is_dst;
    toLocal(utc, year, month, day, hour, minute, second, is_dst);

    int hour12 = hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = hour >= 12 ? "pm" : "am";
    const char* tz = show_dst_suffix ? (is_dst ? " PDT" : " PST") : " PT";

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s %d %d:%02d%s%s",
                  MONTH_NAMES[month - 1], day, hour12, minute, ampm, tz);
    return buf;
}

std::string PacificTime::formatWithAge(const DateTime& utc) {
    std::string base = format(utc, false);

    DateTime now_dt = DateTime::now();
    int hours_ago = static_cast<int>(utc.hoursUntil(now_dt));

    char buf[64];
    if (hours_ago == 1) {
        std::snprintf(buf, sizeof(buf), "%s (1 hour ago)", base.c_str());
    } else if (hours_ago >= 0) {
        std::snprintf(buf, sizeof(buf), "%s (%d hours ago)", base.c_str(), hours_ago);
    } else if (hours_ago == -1) {
        std::snprintf(buf, sizeof(buf), "%s (in 1 hour)", base.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%s (in %d hours)", base.c_str(), -hours_ago);
    }
    return buf;
}

std::string PacificTime::formatTime(const DateTime& utc, bool include_minutes) {
    int year, month, day, hour, minute, second;
    bool is_dst;
    toLocal(utc, year, month, day, hour, minute, second, is_dst);

    int hour12 = hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = hour >= 12 ? "pm" : "am";

    char buf[16];
    if (include_minutes && minute != 0) {
        std::snprintf(buf, sizeof(buf), "%d:%02d%s", hour12, minute, ampm);
    } else {
        std::snprintf(buf, sizeof(buf), "%d%s", hour12, ampm);
    }
    return buf;
}

int PacificTime::utcOffset(const DateTime& utc) {
    return isDst(utc) ? -7 : -8;
}

bool PacificTime::isDst(const DateTime& utc) {
    int year, month, day, hour, minute, second;
    bool is_dst;
    toLocal(utc, year, month, day, hour, minute, second, is_dst);
    return is_dst;
}

// =============================================================================
// NbmCycle implementation
// =============================================================================

NbmCycle::NbmCycle(const std::string& date, int hour)
    : date_(date), hour_(hour) {}

NbmCycle::NbmCycle(const DateTime& cycle_time)
    : date_(cycle_time.toDateString()), hour_(cycle_time.hour()) {}

NbmCycle NbmCycle::availableAt(const DateTime& utc) {
    // Account for availability delay
    DateTime effective = utc.addHours(-NBM_AVAILABILITY_DELAY_HOURS);

    int eff_hour = effective.hour();
    std::string eff_date = effective.toDateString();

    int cycle_hour;
    std::string cycle_date = eff_date;

    if (eff_hour < 1) {
        // Before 01Z: use previous day's 19Z
        auto prev = effective.addDays(-1);
        cycle_date = prev.toDateString();
        cycle_hour = 19;
    } else if (eff_hour < 7) {
        cycle_hour = 1;
    } else if (eff_hour < 13) {
        cycle_hour = 7;
    } else if (eff_hour < 19) {
        cycle_hour = 13;
    } else {
        cycle_hour = 19;
    }

    return NbmCycle(cycle_date, cycle_hour);
}

NbmCycle NbmCycle::forTargetDate(const std::string& target_date) {
    // Default: use previous day's 19Z cycle
    auto target = DateTime::parseDate(target_date);
    if (!target) {
        return NbmCycle(target_date, 19);  // Fallback
    }

    auto prev_day = target->addDays(-1);
    return NbmCycle(prev_day.toDateString(), 19);
}

std::optional<NbmCycle> NbmCycle::forTargetDateAsOf(const std::string& target_date,
                                                     const DateTime& as_of) {
    return NbmCycle::availableAt(as_of);
}

DateTime NbmCycle::availableTime() const {
    return nominalTime().addHours(NBM_AVAILABILITY_DELAY_HOURS);
}

DateTime NbmCycle::nominalTime() const {
    auto dt = DateTime::parseDate(date_);
    if (!dt) {
        return DateTime(0);
    }
    return dt->addHours(hour_);
}

std::string NbmCycle::toString(bool include_date) const {
    char buf[24];
    if (include_date) {
        std::snprintf(buf, sizeof(buf), "%s %02dZ", date_.c_str(), hour_);
    } else {
        std::snprintf(buf, sizeof(buf), "%02dZ", hour_);
    }
    return buf;
}

std::string NbmCycle::toPacificString(bool show_dst) const {
    DateTime nominal = nominalTime();
    return PacificTime::formatTime(nominal, false) +
           (show_dst ? (PacificTime::isDst(nominal) ? " PDT" : " PST") : " PT");
}

std::vector<int> NbmCycle::forecastHoursFor(const std::string& target_date,
                                              const std::string& timezone) const {
    auto window = localDayUtcWindow(target_date, timezone);
    auto cycle_dt = DateTime::parseDate(date_);

    if (!window || !cycle_dt) {
        return {};
    }

    DateTime cycle_time = cycle_dt->addHours(hour_);

    std::vector<int> hours;
    for (int h = 1; h <= 264; ++h) {
        DateTime valid_time = cycle_time.addHours(h);
        if (valid_time >= window->start && valid_time < window->end) {
            hours.push_back(h);
        }
    }
    return hours;
}

// =============================================================================
// IANA timezone helpers
// =============================================================================

namespace {

const date::time_zone* tryLocateZone(const std::string& tz_name) {
    try {
        return date::locate_zone(tz_name);
    } catch (const std::exception&) {
        return nullptr;
    }
}

}  // namespace

std::optional<UtcWindow> localDayUtcWindow(const std::string& date,
                                            const std::string& timezone) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return std::nullopt;
    }

    int year_n;
    unsigned month_n;
    unsigned day_n;
    try {
        year_n = std::stoi(date.substr(0, 4));
        month_n = static_cast<unsigned>(std::stoi(date.substr(5, 2)));
        day_n = static_cast<unsigned>(std::stoi(date.substr(8, 2)));
    } catch (...) {
        return std::nullopt;
    }

    const auto* tz = tryLocateZone(timezone);
    if (!tz) return std::nullopt;

    using namespace date;
    using namespace std::chrono;

    local_days local_start{year{year_n} / month{month_n} / day{day_n}};
    local_days local_end = local_start + days{1};

    sys_seconds sys_start;
    sys_seconds sys_end;
    try {
        // Midnight is never inside a US DST gap (transitions happen at 02:00).
        // Use choose::earliest as a safe default for any other zones with
        // midnight transitions.
        sys_start = tz->to_sys(local_start, choose::earliest);
        sys_end = tz->to_sys(local_end, choose::earliest);
    } catch (const std::exception&) {
        return std::nullopt;
    }

    UtcWindow w{
        DateTime(sys_start.time_since_epoch().count()),
        DateTime(sys_end.time_since_epoch().count()),
    };
    return w;
}

std::string formatLocalHourMinute(const DateTime& utc, const std::string& timezone) {
    const auto* tz = tryLocateZone(timezone);
    if (!tz) return "";

    using namespace date;
    using namespace std::chrono;

    sys_seconds sys{seconds{utc.epoch()}};
    local_seconds local = tz->to_local(sys);

    auto local_day = floor<days>(local);
    auto tod = make_time(local - local_day);

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d",
                  static_cast<int>(tod.hours().count()),
                  static_cast<int>(tod.minutes().count()));
    return buf;
}

std::optional<DateTime> parseLocalDatetime(const std::string& date,
                                            const std::string& hhmm,
                                            const std::string& timezone) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return std::nullopt;
    if (hhmm.size() < 5 || hhmm[2] != ':') return std::nullopt;

    int year_n;
    unsigned month_n, day_n;
    int hour_n, min_n;
    try {
        year_n = std::stoi(date.substr(0, 4));
        month_n = static_cast<unsigned>(std::stoi(date.substr(5, 2)));
        day_n = static_cast<unsigned>(std::stoi(date.substr(8, 2)));
        hour_n = std::stoi(hhmm.substr(0, 2));
        min_n = std::stoi(hhmm.substr(3, 2));
    } catch (...) {
        return std::nullopt;
    }

    const auto* tz = tryLocateZone(timezone);
    if (!tz) return std::nullopt;

    using namespace date;
    using namespace std::chrono;

    local_seconds local =
        local_days{year{year_n} / month{month_n} / day{day_n}}
        + hours{hour_n} + minutes{min_n};

    sys_seconds sys;
    try {
        sys = tz->to_sys(local, choose::earliest);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return DateTime(sys.time_since_epoch().count());
}

std::string formatLocalAmPm(const DateTime& utc, const std::string& timezone) {
    const auto* tz = tryLocateZone(timezone);
    if (!tz) return "";

    using namespace date;
    using namespace std::chrono;

    sys_seconds sys{seconds{utc.epoch()}};
    auto info = tz->get_info(sys);
    local_seconds local = tz->to_local(sys);

    auto local_day = floor<days>(local);
    auto tod = make_time(local - local_day);
    int hour = static_cast<int>(tod.hours().count());
    int minute = static_cast<int>(tod.minutes().count());

    int hour12 = hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = hour >= 12 ? "pm" : "am";

    char buf[32];
    if (minute == 0) {
        std::snprintf(buf, sizeof(buf), "%d%s %s", hour12, ampm, info.abbrev.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%d:%02d%s %s",
                      hour12, minute, ampm, info.abbrev.c_str());
    }
    return buf;
}

// =============================================================================
// Free functions
// =============================================================================

std::string utcHourToPacific(int utc_hour, bool is_dst) {
    int offset = is_dst ? -7 : -8;
    int pt_hour = (utc_hour + offset + 24) % 24;

    int hour12 = pt_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = pt_hour >= 12 ? "pm" : "am";
    const char* tz = is_dst ? "PDT" : "PST";

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d%s %s", hour12, ampm, tz);
    return buf;
}

const std::array<CycleTimeInfo, 4>& nbmCycleTable() {
    static const std::array<CycleTimeInfo, 4> table = {{
        {1,  "01Z", "5pm PST",  "6pm PDT",  "7pm PST",  "8pm PDT"},
        {7,  "07Z", "11pm PST", "12am PDT", "1am PST",  "2am PDT"},
        {13, "13Z", "5am PST",  "6am PDT",  "7am PST",  "8am PDT"},
        {19, "19Z", "11am PST", "12pm PDT", "1pm PST",  "2pm PDT"},
    }};
    return table;
}

std::string computeAsOfIso(const std::string& settlement_date, int entry_day_offset, int entry_hour) {
    auto dt = DateTime::parseDate(settlement_date);
    if (!dt) return "";

    DateTime entry_time = dt->addDays(entry_day_offset).addHours(entry_hour);

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%sT%02d:00:00Z",
                  entry_time.toDateString().c_str(), entry_time.hour());
    return buf;
}

std::string nyMidnightToUtcIso(const std::string& date) {
    auto dt = DateTime::parseDate(date);
    if (!dt) return "";

    // NY midnight = 05:00 UTC (standard time) or 04:00 UTC (daylight time)
    // Use America/New_York timezone to get correct offset
    TzGuard guard("America/New_York");

    std::tm local = {};
    local.tm_year = dt->year() - 1900;
    local.tm_mon = dt->month() - 1;
    local.tm_mday = dt->day();
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;

    time_t t = mktime(&local);
    if (t == -1) return "";

    std::tm utc = {};
    gmtime_r(&t, &utc);

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec);
    return buf;
}

}  // namespace predibloom::core
