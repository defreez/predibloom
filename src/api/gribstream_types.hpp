#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace predibloom::api {

// Daily aggregated weather keyed by America/New_York local date (YYYY-MM-DD).
// Temperatures are in Fahrenheit. Preserves the public shape previously used
// by the previous weather client so callers need no changes beyond swapping the client.
struct DailyWeatherData {
    std::vector<std::string> time;           // YYYY-MM-DD
    std::vector<double> temperature_2m_max;  // daily high (Fahrenheit)
    std::vector<double> temperature_2m_min;  // daily low (Fahrenheit)
    std::vector<std::string> time_of_max;    // local time of high (HH:MM)
    std::vector<std::string> time_of_min;    // local time of low (HH:MM)
};

struct WeatherResponse {
    double latitude = 0;
    double longitude = 0;
    std::string timezone = "America/New_York";
    std::string forecasted_at;  // ISO timestamp of when forecast was generated
    DailyWeatherData daily;
};

inline std::optional<double> getTemperatureForDate(const WeatherResponse& response,
                                                    const std::string& date) {
    const auto& times = response.daily.time;
    const auto& temps = response.daily.temperature_2m_max;
    for (size_t i = 0; i < times.size() && i < temps.size(); ++i) {
        if (times[i] == date) {
            double t = temps[i];
            if (!std::isnan(t)) return t;
        }
    }
    return std::nullopt;
}

inline std::optional<double> getMinTemperatureForDate(const WeatherResponse& response,
                                                       const std::string& date) {
    const auto& times = response.daily.time;
    const auto& temps = response.daily.temperature_2m_min;
    for (size_t i = 0; i < times.size() && i < temps.size(); ++i) {
        if (times[i] == date) {
            double t = temps[i];
            if (!std::isnan(t)) return t;
        }
    }
    return std::nullopt;
}

// Kelvin to Fahrenheit.
inline double kelvinToFahrenheit(double kelvin) {
    return (kelvin - 273.15) * 9.0 / 5.0 + 32.0;
}

// Parse a GribStream CSV body (header + rows) and extract the temperature column
// (last column) as Kelvin values. Ignores the header row. Returns true on success.
// Rows that cannot be parsed are skipped.
// If out_forecasted_at is provided, it will be populated with the most recent
// (lexicographically greatest) forecasted_at timestamp from column 0.
bool parseGribstreamCsvTemps(const std::string& csv_body,
                              std::vector<double>& out_kelvin,
                              std::string* out_forecasted_at = nullptr);

} // namespace predibloom::api
