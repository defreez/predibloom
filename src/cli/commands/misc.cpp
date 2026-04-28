#include "misc.hpp"
#include "../../api/local_nbm_client.hpp"
#include "../../core/datetime.hpp"

#include <iostream>
#include <iomanip>
#include <cstdio>
#include <ctime>

namespace predibloom::cli {

int runSeries(const core::Config& config) {
    std::cout << std::left << std::setw(16) << "Ticker"
              << std::setw(20) << "Label"
              << std::right << std::setw(8) << "Offset"
              << "  " << std::left << std::setw(14) << "Entry (PT)"
              << std::setw(6) << "NWS"
              << "  " << std::left << std::setw(10) << "Source"
              << "\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& tab : config.tabs) {
        for (const auto& s : tab.series) {
            if (s.latitude == 0) continue;

            int utc = s.effectiveEntryHour();
            int pt = (utc - 7 + 24) % 24;
            std::string pt_ampm = (pt >= 12) ? "pm" : "am";
            int pt12 = (pt % 12 == 0) ? 12 : (pt % 12);
            std::string day_note = (utc < 7) ? " prev" : "";
            if (s.entry_day_offset != 0) {
                char d_buf[8];
                snprintf(d_buf, sizeof(d_buf), " D%+d", s.entry_day_offset);
                day_note += d_buf;
            }

            char offset_buf[16];
            snprintf(offset_buf, sizeof(offset_buf), "%+.1f F", s.offset);

            char entry_buf[32];
            snprintf(entry_buf, sizeof(entry_buf), "%d%s%s", pt12, pt_ampm.c_str(), day_note.c_str());

            std::string source = (s.weather_source == core::WeatherSource::LocalNbm)
                ? "local_nbm" : "gribstream";

            std::cout << std::left << std::setw(16) << s.series_ticker
                      << std::setw(20) << s.label
                      << std::right << std::setw(8) << offset_buf
                      << "  " << std::left << std::setw(14) << entry_buf
                      << std::setw(6) << s.nws_station
                      << "  " << std::left << std::setw(10) << source
                      << "\n";
        }
    }
    return 0;
}

int runMfr(const std::string& date) {
    // MFR airport: Medford, Oregon
    constexpr double MFR_LAT = 42.3742;
    constexpr double MFR_LON = -122.8735;

    // Default to today if no date specified
    std::string target_date = date;
    if (target_date.empty()) {
        target_date = core::todayUtc();
    }

    api::LocalNbmClient nbm;
    if (!nbm.is_open()) {
        std::cerr << "Error: NBM database not available\n";
        return 1;
    }

    // Get forecast using most recent available cycle
    // Medford is in Pacific Time
    constexpr const char* MFR_TZ = "America/Los_Angeles";
    std::string now_iso = core::currentUtcDatetimeHour() + ":00:00Z";
    auto result = nbm.getForecast(MFR_LAT, MFR_LON, target_date, MFR_TZ, now_iso);
    if (!result.ok()) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
    }

    const auto& resp = result.value();
    if (resp.daily.temperature_2m_max.empty() || resp.daily.temperature_2m_min.empty()) {
        std::cerr << "No forecast data for " << target_date << "\n";
        return 1;
    }

    double high = resp.daily.temperature_2m_max[0];
    double low = resp.daily.temperature_2m_min[0];

    // Convert station-local "HH:MM" to "3pm PDT" using the IANA tz abbreviation.
    auto format_local = [&](const std::string& hhmm) -> std::string {
        if (hhmm.empty()) return "";
        auto utc = core::parseLocalDatetime(target_date, hhmm, MFR_TZ);
        if (!utc) return "";
        return core::formatLocalAmPm(*utc, MFR_TZ);
    };

    std::string time_high = resp.daily.time_of_max.empty() ? "" : format_local(resp.daily.time_of_max[0]);
    std::string time_low = resp.daily.time_of_min.empty() ? "" : format_local(resp.daily.time_of_min[0]);

    std::cout << "MFR (Medford, OR) - " << target_date << "\n";
    std::cout << "  High: " << std::fixed << std::setprecision(0) << high << "°F";
    if (!time_high.empty()) std::cout << " @ " << time_high;
    std::cout << "\n";
    std::cout << "  Low:  " << std::fixed << std::setprecision(0) << low << "°F";
    if (!time_low.empty()) std::cout << " @ " << time_low;
    std::cout << "\n";

    return 0;
}

}  // namespace predibloom::cli
