#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace predibloom::api {

struct DailyWeatherData {
    std::vector<std::string> time;
    std::vector<double> temperature_2m_max;  // Daily high (Fahrenheit)
};

inline void from_json(const nlohmann::json& j, DailyWeatherData& d) {
    if (j.contains("time")) {
        j.at("time").get_to(d.time);
    }
    if (j.contains("temperature_2m_max")) {
        const auto& temps = j.at("temperature_2m_max");
        d.temperature_2m_max.reserve(temps.size());
        for (const auto& t : temps) {
            if (t.is_null()) {
                d.temperature_2m_max.push_back(std::numeric_limits<double>::quiet_NaN());
            } else {
                d.temperature_2m_max.push_back(t.get<double>());
            }
        }
    }
}

struct WeatherResponse {
    double latitude;
    double longitude;
    std::string timezone;
    DailyWeatherData daily;
};

inline void from_json(const nlohmann::json& j, WeatherResponse& r) {
    if (j.contains("latitude")) j.at("latitude").get_to(r.latitude);
    if (j.contains("longitude")) j.at("longitude").get_to(r.longitude);
    if (j.contains("timezone")) j.at("timezone").get_to(r.timezone);
    if (j.contains("daily")) j.at("daily").get_to(r.daily);
}

// Helper to get temperature for a specific date
inline std::optional<double> getTemperatureForDate(const WeatherResponse& response,
                                                    const std::string& date) {
    const auto& times = response.daily.time;
    const auto& temps = response.daily.temperature_2m_max;

    for (size_t i = 0; i < times.size() && i < temps.size(); ++i) {
        if (times[i] == date) {
            double temp = temps[i];
            if (!std::isnan(temp)) {
                return temp;
            }
        }
    }
    return std::nullopt;
}

} // namespace predibloom::api
