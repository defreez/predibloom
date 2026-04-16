#pragma once

#include <string>
#include <vector>

namespace predibloom::core {

struct TrackedSeries {
    std::string series_ticker;
    std::string label;
};

struct Tab {
    std::string name;
    std::vector<TrackedSeries> series;
};

struct Config {
    std::vector<Tab> tabs;

    static Config load();
    static std::string default_path();
};

} // namespace predibloom::core
