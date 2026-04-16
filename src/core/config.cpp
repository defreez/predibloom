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

            if (j.contains("tabs")) {
                for (const auto& tab_json : j["tabs"]) {
                    Tab tab;
                    tab.name = tab_json["name"];
                    for (const auto& series_json : tab_json["series"]) {
                        TrackedSeries ts;
                        ts.series_ticker = series_json["series_ticker"];
                        ts.label = series_json["label"];
                        tab.series.push_back(ts);
                    }
                    config.tabs.push_back(tab);
                }
            }
        } catch (...) {
            // Invalid config, return empty
        }
    }
    return config;
}

} // namespace predibloom::core
