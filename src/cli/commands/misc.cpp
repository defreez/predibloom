#include "misc.hpp"

#include <iostream>
#include <iomanip>
#include <cstdio>

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

}  // namespace predibloom::cli
