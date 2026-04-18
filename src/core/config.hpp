#pragma once

#include <string>
#include <vector>

namespace predibloom::core {

// Returns true if the series ticker is a low temperature market (KXLOWT*)
inline bool isLowTempSeries(const std::string& ticker) {
    return ticker.find("KXLOWT") == 0;
}

struct TrackedSeries {
    std::string series_ticker;
    std::string label;

    // Weather params (for temperature markets)
    double latitude = 0;
    double longitude = 0;
    std::string nws_station;
    double offset = 2.0;  // Calibration offset for Open-Meteo -> NWS
    int entry_hour = -1;   // UTC hour for backtest entry (-1 = use default: 5 high, 17 low)

    bool isLowTemp() const { return isLowTempSeries(series_ticker); }

    // Effective entry hour (resolves -1 to default: 4 UTC = 9pm PT)
    int effectiveEntryHour() const {
        if (entry_hour >= 0) return entry_hour;
        return 4;  // 9pm PT (prev day)
    }
};

struct Tab {
    std::string name;
    std::vector<TrackedSeries> series;
};

struct Config {
    std::vector<Tab> tabs;

    // Find series by ticker (returns nullptr if not found)
    const TrackedSeries* findSeries(const std::string& series_ticker) const;

    static Config load();
    static std::string default_path();
};

} // namespace predibloom::core
