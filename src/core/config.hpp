#pragma once

#include <string>
#include <vector>

namespace predibloom::core {

// Returns true if the series ticker is a low temperature market (KXLOWT*)
inline bool isLowTempSeries(const std::string& ticker) {
    return ticker.find("KXLOWT") == 0;
}

// Weather data source options
enum class WeatherSource {
    GribStream,  // Use GribStream API (requires token)
    LocalNbm     // Use local NBM from NOAA S3 (requires Python + dependencies)
};

struct TrackedSeries {
    std::string series_ticker;
    std::string label;

    // Weather params (for temperature markets)
    double latitude = 0;
    double longitude = 0;
    std::string nws_station;
    double offset = 2.0;  // Calibration offset added to forecast to align with NWS settlement
    int entry_hour = -1;   // UTC hour for backtest entry (-1 = use default: 4 UTC)
    int entry_day_offset = 0;  // 0 = same UTC day as settlement, -1 = day before
    WeatherSource weather_source = WeatherSource::GribStream;  // Which weather API to use

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

    // Auth (optional - needed for authenticated endpoints like fills)
    std::string api_key_id;
    std::string key_file;

    // GribStream API token (optional - needed for weather forecast endpoints)
    std::string gribstream_api_token;

    bool hasAuth() const { return !api_key_id.empty() && !key_file.empty(); }
    bool hasGribstream() const { return !gribstream_api_token.empty(); }

    // Find series by ticker (returns nullptr if not found)
    const TrackedSeries* findSeries(const std::string& series_ticker) const;

    static Config load();
    static Config loadFromFile(const std::string& path);
    void loadAuth(const std::string& path);
    static std::string default_path();
    static std::string auth_path();
};

} // namespace predibloom::core
