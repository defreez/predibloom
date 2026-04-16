#pragma once

#include <string>
#include <vector>

namespace predibloom::core {

struct TrackedSeries {
    std::string series_ticker;
    std::string label;
};

struct Config {
    std::vector<TrackedSeries> tracked;

    static Config load();
    static std::string default_path();
};

} // namespace predibloom::core
