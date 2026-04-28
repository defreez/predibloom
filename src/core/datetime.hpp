#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>

namespace predibloom::core {

// NBM cycle hours (UTC)
constexpr int NBM_CYCLES[] = {1, 7, 13, 19};
constexpr int NBM_CYCLE_COUNT = 4;

// NBM cycle availability delay (hours after nominal time)
constexpr int NBM_AVAILABILITY_DELAY_HOURS = 2;

// Represents a point in time as UTC seconds since epoch.
// Provides conversions to/from various string formats and timezones.
class DateTime {
public:
    // Construct from UTC epoch seconds
    explicit DateTime(int64_t utc_epoch) : epoch_(utc_epoch) {}

    // Construct from current time
    static DateTime now();

    // Parse various formats (all interpreted as UTC unless noted)
    static std::optional<DateTime> parseDate(const std::string& date);           // YYYY-MM-DD (midnight UTC)
    static std::optional<DateTime> parseIso(const std::string& iso);             // YYYY-MM-DDTHH:MM:SSZ
    static std::optional<DateTime> parseDateHour(const std::string& dt);         // YYYY-MM-DDTHH

    // Accessors
    int64_t epoch() const { return epoch_; }

    // Format as various strings (UTC)
    std::string toDateString() const;      // YYYY-MM-DD
    std::string toIsoString() const;       // YYYY-MM-DDTHH:MM:SSZ
    std::string toDateHour() const;        // YYYY-MM-DDTHH

    // UTC components
    int year() const;
    int month() const;    // 1-12
    int day() const;      // 1-31
    int hour() const;     // 0-23
    int minute() const;   // 0-59
    int second() const;   // 0-59

    // Arithmetic
    DateTime addDays(int days) const;
    DateTime addHours(int hours) const;
    DateTime addSeconds(int64_t seconds) const;

    // Comparisons
    bool operator<(const DateTime& other) const { return epoch_ < other.epoch_; }
    bool operator<=(const DateTime& other) const { return epoch_ <= other.epoch_; }
    bool operator>(const DateTime& other) const { return epoch_ > other.epoch_; }
    bool operator>=(const DateTime& other) const { return epoch_ >= other.epoch_; }
    bool operator==(const DateTime& other) const { return epoch_ == other.epoch_; }
    bool operator!=(const DateTime& other) const { return epoch_ != other.epoch_; }

    // Difference in hours (positive if other is earlier)
    double hoursUntil(const DateTime& other) const;

private:
    int64_t epoch_;  // UTC seconds since 1970-01-01 00:00:00

    std::tm toUtcTm() const;
};

// Pacific Time utilities.
// Handles PST (UTC-8) and PDT (UTC-7) transitions automatically.
class PacificTime {
public:
    // Convert UTC DateTime to Pacific Time components
    static void toLocal(const DateTime& utc, int& year, int& month, int& day,
                        int& hour, int& minute, int& second, bool& is_dst);

    // Format UTC time as Pacific Time string
    // E.g., "Apr 23 11:00am PT" or "Apr 23 11:00am PDT"
    static std::string format(const DateTime& utc, bool show_dst_suffix = false);

    // Format with age: "Apr 23 11:00am PT (6 hours ago)"
    static std::string formatWithAge(const DateTime& utc);

    // Format just the time: "11:00am" or "3pm"
    static std::string formatTime(const DateTime& utc, bool include_minutes = true);

    // Get UTC offset in hours for a given UTC time (-8 for PST, -7 for PDT)
    static int utcOffset(const DateTime& utc);

    // Check if a UTC time falls in PDT (daylight saving)
    static bool isDst(const DateTime& utc);
};

// NBM (National Blend of Models) cycle utilities
class NbmCycle {
public:
    // Construct from cycle date and hour
    NbmCycle(const std::string& date, int hour);
    NbmCycle(const DateTime& cycle_time);

    // Get the cycle that would be available at a given UTC time
    // (accounts for ~2 hour availability delay)
    static NbmCycle availableAt(const DateTime& utc);

    // Get the most recent cycle for a target forecast date
    // Default: previous day's 19Z cycle
    static NbmCycle forTargetDate(const std::string& target_date);

    // Get cycle with as-of constraint (what cycle was available at that time?)
    static std::optional<NbmCycle> forTargetDateAsOf(const std::string& target_date,
                                                      const DateTime& as_of);

    // Accessors
    std::string date() const { return date_; }
    int hour() const { return hour_; }

    // When this cycle becomes available (nominal + delay)
    DateTime availableTime() const;

    // Nominal cycle time
    DateTime nominalTime() const;

    // Format as string: "2026-04-25 19Z" or "19Z"
    std::string toString(bool include_date = true) const;

    // Format cycle hour in Pacific Time: "12pm PT" or "11am PST"
    std::string toPacificString(bool show_dst = false) const;

    // Compute forecast hours needed to cover a target local date in the given IANA timezone.
    // timezone: IANA name (e.g., "America/New_York", "America/Los_Angeles", "America/Phoenix").
    // Handles DST automatically. Returns empty on parse failure or unknown timezone.
    std::vector<int> forecastHoursFor(const std::string& target_date,
                                       const std::string& timezone) const;

private:
    std::string date_;  // YYYY-MM-DD
    int hour_;          // 1, 7, 13, or 19
};

// =============================================================================
// IANA timezone helpers (powered by Howard Hinnant's date/tz library)
// =============================================================================

// UTC window covering a local calendar day in a given IANA timezone.
// `start` is inclusive; `end` is exclusive. Spring-forward / fall-back days are
// 23h or 25h long.
struct UtcWindow {
    DateTime start;
    DateTime end;
};

// Compute the UTC window covering local calendar day `date` in `timezone`.
// `date` is "YYYY-MM-DD"; `timezone` is an IANA name.
// Returns nullopt on parse failure or unknown timezone.
std::optional<UtcWindow> localDayUtcWindow(const std::string& date,
                                            const std::string& timezone);

// Format a UTC instant as local 24-hour "HH:MM" in the given IANA timezone.
// Returns empty string on unknown timezone.
std::string formatLocalHourMinute(const DateTime& utc, const std::string& timezone);

// Parse a local "HH:MM" on `date` interpreted in `timezone`, returning the
// corresponding UTC instant. Returns nullopt on parse failure or unknown tz.
std::optional<DateTime> parseLocalDatetime(const std::string& date,
                                            const std::string& hhmm,
                                            const std::string& timezone);

// Format a UTC instant as 12-hour "3pm EDT" / "11am PST" with the IANA zone's
// own abbreviation. Returns empty string on unknown timezone.
std::string formatLocalAmPm(const DateTime& utc, const std::string& timezone);

// Convenience: format a UTC hour as Pacific Time
// E.g., utcHourToPacific(19) -> "12pm PDT" or "11am PST" depending on current DST
std::string utcHourToPacific(int utc_hour, bool is_dst);

// Table of NBM cycles with both UTC and PT times
struct CycleTimeInfo {
    int utc_hour;
    std::string utc_str;      // "19Z"
    std::string pst_str;      // "11am PST"
    std::string pdt_str;      // "12pm PDT"
    std::string available_pst; // "1pm PST" (with delay)
    std::string available_pdt; // "2pm PDT" (with delay)
};

// Get info for all NBM cycles
const std::array<CycleTimeInfo, 4>& nbmCycleTable();

// =============================================================================
// Low-level helper functions
// =============================================================================

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

// =============================================================================
// Convenience functions
// =============================================================================

// Get today's date in UTC as "YYYY-MM-DD"
inline std::string todayUtc() { return DateTime::now().toDateString(); }

// Get current UTC datetime as "YYYY-MM-DDTHH"
inline std::string currentUtcDatetimeHour() { return DateTime::now().toDateHour(); }

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

// Add days to a date string. Returns empty on invalid input.
inline std::string addDaysToDate(const std::string& date, int days) {
    auto dt = DateTime::parseDate(date);
    if (!dt) return "";
    return dt->addDays(days).toDateString();
}

// Compute entry datetime "YYYY-MM-DDTHH" from settlement date, day offset, and hour.
inline std::string computeEntryDatetime(const std::string& settlement_date,
                                        int entry_day_offset, int entry_hour) {
    std::string entry_date = (entry_day_offset == 0)
        ? settlement_date
        : addDaysToDate(settlement_date, entry_day_offset);
    if (entry_date.empty()) return "";

    char buf[14];
    std::snprintf(buf, sizeof(buf), "%sT%02d", entry_date.c_str(), entry_hour);
    return buf;
}

// Compute as-of ISO timestamp for entry moment: (settlement_date + offset) at hour UTC
// Returns "YYYY-MM-DDTHH:00:00Z"
std::string computeAsOfIso(const std::string& settlement_date, int entry_day_offset, int entry_hour);

// Convert NY local midnight to UTC ISO timestamp
std::string nyMidnightToUtcIso(const std::string& date);

// Format UTC ISO timestamp as PT with age: "Apr 23 11:00am PT (6 hours ago)"
inline std::string formatUtcAsPtWithAge(const std::string& utc_iso) {
    auto dt = DateTime::parseIso(utc_iso);
    if (!dt) return "";
    return PacificTime::formatWithAge(*dt);
}

}  // namespace predibloom::core
