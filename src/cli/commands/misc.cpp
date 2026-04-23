#include "misc.hpp"
#include "../../api/local_nbm_client.hpp"
#include "../../core/time_utils.hpp"

#include <iostream>
#include <iomanip>
#include <cstdio>
#include <vector>

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

}  // namespace predibloom::cli
