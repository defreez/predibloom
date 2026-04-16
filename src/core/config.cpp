#include "config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>

namespace predibloom::core {

std::string Config::default_path() {
    const char* home = std::getenv("HOME");
    if (!home) {
        home = "/tmp";
    }
    return std::string(home) + "/.config/predibloom/config.json";
}

Config Config::load() {
    std::string path = default_path();
    std::ifstream file(path);

    Config config;
    if (file.good()) {
        try {
            nlohmann::json j;
            file >> j;
            for (const auto& item : j["tracked"]) {
                TrackedSeries ts;
                ts.series_ticker = item["series_ticker"];
                ts.label = item["label"];
                config.tracked.push_back(ts);
            }
        } catch (...) {
            // Invalid config, return empty
        }
    }
    return config;
}

} // namespace predibloom::core
