#pragma once

#include <string>
#include <vector>

namespace predibloom::core {

struct TrackedSeries {
    std::string series_ticker;
    std::string label;

    // Weather params (for temperature markets)
    double latitude = 0;
    double longitude = 0;
    std::string nws_station;
    double offset = 2.0;  // Calibration offset for Open-Meteo -> NWS
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
