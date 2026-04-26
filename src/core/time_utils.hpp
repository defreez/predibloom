#pragma once

#include <string>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace predibloom::core {

// Parse "YYYY-MM-DD" into a std::tm (midnight UTC).
// Returns false on parse failure.
inline bool parseDateString(const std::string& date_str, std::tm& out) {
    if (date_str.size() != 10 || date_str[4] != '-' || date_str[7] != '-')
        return false;
    out = {};
    try {
        out.tm_year = std::stoi(date_str.substr(0, 4)) - 1900;
        out.tm_mon = std::stoi(date_str.substr(5, 2)) - 1;
        out.tm_mday = std::stoi(date_str.substr(8, 2));
    } catch (...) {
        return false;
    }
    out.tm_isdst = 0;
    return true;
}

// Format std::tm as "YYYY-MM-DD".
inline std::string formatDate(const std::tm& tm) {
    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

// Add `days` (positive or negative) to a "YYYY-MM-DD" date string.
// Handles month boundaries, year boundaries, leap years via timegm.
// Returns empty string on invalid input.
inline std::string addDaysToDate(const std::string& date_str, int days) {
    std::tm tm = {};
    if (!parseDateString(date_str, tm)) return "";
    tm.tm_mday += days;
    time_t t = timegm(&tm);
    std::tm result = {};
    gmtime_r(&t, &result);
    return formatDate(result);
}

// Compute entry datetime "YYYY-MM-DDTHH" from settlement date, day offset, and hour.
inline std::string computeEntryDatetime(const std::string& settlement_date,
                                        int entry_day_offset,
                                        int entry_hour) {
    std::string entry_date = (entry_day_offset == 0)
        ? settlement_date
        : addDaysToDate(settlement_date, entry_day_offset);
    if (entry_date.empty()) return "";

    char buf[14];
    std::snprintf(buf, sizeof(buf), "%sT%02d", entry_date.c_str(), entry_hour);
    return buf;
}

// Compute entry datetime with hour jitter. Handles day-boundary crossings.
// E.g., hour=1, delta=-3 -> hour=22, day shifted back by 1.
inline std::string computeEntryDatetimeWithJitter(
    const std::string& settlement_date,
    int entry_day_offset,
    int entry_hour,
    int hour_delta) {

    int adjusted_hour = entry_hour + hour_delta;
    int extra_days = 0;

    while (adjusted_hour < 0) {
        adjusted_hour += 24;
        extra_days--;
    }
    while (adjusted_hour >= 24) {
        adjusted_hour -= 24;
        extra_days++;
    }

    return computeEntryDatetime(settlement_date, entry_day_offset + extra_days, adjusted_hour);
}

// Convert a local America/New_York wall-clock "YYYY-MM-DD HH:MM:SS" to an
// ISO-8601 UTC timestamp. Uses libc with TZ temporarily set to handle DST.
// Returns empty string on parse failure.
// NOTE: not thread-safe (modifies TZ env var via setenv/tzset).
inline std::string nyLocalToUtcIso(int year, int month, int day,
                                    int hour, int minute, int second) {
    std::tm local = {};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = second;
    local.tm_isdst = -1;  // let mktime decide

    char* orig_tz = std::getenv("TZ");
    std::string saved_tz = orig_tz ? orig_tz : "";
    bool had_tz = orig_tz != nullptr;

    setenv("TZ", "America/New_York", 1);
    tzset();
    time_t t = mktime(&local);

    if (had_tz) setenv("TZ", saved_tz.c_str(), 1);
    else unsetenv("TZ");
    tzset();

    if (t == (time_t)-1) return "";

    std::tm utc = {};
    gmtime_r(&t, &utc);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec);
    return buf;
}

// ISO-8601 UTC timestamp for local midnight in America/New_York on the given date.
// Returns empty string on parse failure.
inline std::string nyMidnightToUtcIso(const std::string& date) {
    std::tm tm = {};
    if (!parseDateString(date, tm)) return "";
    return nyLocalToUtcIso(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, 0, 0, 0);
}

// Compute ISO-8601 UTC timestamp ("YYYY-MM-DDTHH:MM:SSZ") for the entry moment
// used as GribStream's asOf parameter: (settlement_date + entry_day_offset) at entry_hour UTC.
// Returns empty string on parse failure.
inline std::string computeAsOfIso(const std::string& settlement_date,
                                  int entry_day_offset,
                                  int entry_hour) {
    std::tm tm = {};
    if (!parseDateString(settlement_date, tm)) return "";

    std::string entry_date = (entry_day_offset == 0)
        ? settlement_date
        : addDaysToDate(settlement_date, entry_day_offset);
    if (entry_date.empty()) return "";

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%sT%02d:00:00Z", entry_date.c_str(), entry_hour);
    return buf;
}

// Get current UTC time as "YYYY-MM-DDTHH".
inline std::string currentUtcDatetimeHour() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm utc = {};
    gmtime_r(&t, &utc);
    char buf[14];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour);
    return buf;
}

// Get current UTC date as "YYYY-MM-DD".
inline std::string todayUtc() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm utc = {};
    gmtime_r(&t, &utc);
    return formatDate(utc);
}

// Parse "YYYY-MM-DDTHH" to time_t (seconds since epoch, UTC).
// Returns -1 on failure.
inline time_t parseHourDatetime(const std::string& dt) {
    if (dt.size() < 13 || dt[10] != 'T') return -1;
    std::tm tm = {};
    try {
        tm.tm_year = std::stoi(dt.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(dt.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(dt.substr(8, 2));
        tm.tm_hour = std::stoi(dt.substr(11, 2));
    } catch (...) {
        return -1;
    }
    tm.tm_isdst = 0;
    return timegm(&tm);
}

// Check if current time is within +/- window_hours of entry_datetime.
// entry_datetime: "YYYY-MM-DDTHH" format
// current_datetime: "YYYY-MM-DDTHH" format (empty = use actual current time)
// Returns true if within the window.
inline bool isWithinEntryWindow(const std::string& entry_datetime,
                                int window_hours,
                                const std::string& current_datetime = "") {
    std::string now_str = current_datetime.empty() ? currentUtcDatetimeHour() : current_datetime;

    time_t entry_t = parseHourDatetime(entry_datetime);
    time_t now_t = parseHourDatetime(now_str);

    if (entry_t == -1 || now_t == -1) return false;

    double diff_hours = std::difftime(now_t, entry_t) / 3600.0;
    return diff_hours >= -window_hours && diff_hours <= window_hours;
}

// Convert UTC ISO timestamp (YYYY-MM-DDTHH:MM:SSZ) to Pacific Time formatted string
// and compute hours ago. Returns formatted string like "Apr 23 11:00am PT (6 hours ago)"
// Returns empty string on parse failure.
inline std::string formatUtcAsPtWithAge(const std::string& utc_iso) {
    if (utc_iso.size() < 19) return "";

    std::tm utc_tm = {};
    try {
        utc_tm.tm_year = std::stoi(utc_iso.substr(0, 4)) - 1900;
        utc_tm.tm_mon = std::stoi(utc_iso.substr(5, 2)) - 1;
        utc_tm.tm_mday = std::stoi(utc_iso.substr(8, 2));
        utc_tm.tm_hour = std::stoi(utc_iso.substr(11, 2));
        utc_tm.tm_min = std::stoi(utc_iso.substr(14, 2));
        utc_tm.tm_sec = std::stoi(utc_iso.substr(17, 2));
    } catch (...) {
        return "";
    }
    utc_tm.tm_isdst = 0;
    time_t utc_t = timegm(&utc_tm);
    if (utc_t == (time_t)-1) return "";

    // Convert to PT (use America/Los_Angeles)
    char* orig_tz = std::getenv("TZ");
    std::string saved_tz = orig_tz ? orig_tz : "";
    bool had_tz = orig_tz != nullptr;

    setenv("TZ", "America/Los_Angeles", 1);
    tzset();
    std::tm pt_tm = {};
    localtime_r(&utc_t, &pt_tm);

    if (had_tz) setenv("TZ", saved_tz.c_str(), 1);
    else unsetenv("TZ");
    tzset();

    // Format month name
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    // Format hour in 12-hour format
    int hour12 = pt_tm.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = pt_tm.tm_hour >= 12 ? "pm" : "am";

    // Calculate hours ago
    auto now = std::chrono::system_clock::now();
    time_t now_t = std::chrono::system_clock::to_time_t(now);
    int hours_ago = static_cast<int>(std::difftime(now_t, utc_t) / 3600.0);

    char buf[64];
    if (hours_ago == 1) {
        std::snprintf(buf, sizeof(buf), "%s %d %d:%02d%s PT (1 hour ago)",
                      months[pt_tm.tm_mon], pt_tm.tm_mday,
                      hour12, pt_tm.tm_min, ampm);
    } else {
        std::snprintf(buf, sizeof(buf), "%s %d %d:%02d%s PT (%d hours ago)",
                      months[pt_tm.tm_mon], pt_tm.tm_mday,
                      hour12, pt_tm.tm_min, ampm, hours_ago);
    }
    return buf;
}

} // namespace predibloom::core
