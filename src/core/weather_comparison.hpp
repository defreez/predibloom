#pragma once

#include "../api/result.hpp"
#include "../api/types.hpp"
#include "../api/kalshi_client.hpp"
#include "../api/openmeteo_client.hpp"
#include <string>
#include <vector>
#include <optional>
#include <regex>

namespace predibloom::core {

// NYC Central Park coordinates
constexpr double NYC_LATITUDE = 40.7128;
constexpr double NYC_LONGITUDE = -74.0060;

// LA Airport (LAX) coordinates
constexpr double LA_LATITUDE = 33.9425;
constexpr double LA_LONGITUDE = -118.4081;

struct ComparisonPoint {
    std::string date;
    std::string market_ticker;
    double kalshi_price;        // Market implied probability (last price in cents)
    std::optional<double> forecast_high;  // What Open-Meteo predicted
    std::optional<double> actual_high;    // What actually happened
    std::string settlement;     // "yes", "no", or "" if not settled

    // Strike bounds parsed from ticker
    std::optional<int> floor_strike;  // Lower temp bound
    std::optional<int> cap_strike;    // Upper temp bound (or nullopt for "X or higher")
};

struct ComparisonSummary {
    std::string series_ticker;
    int total_markets = 0;
    int settled_markets = 0;
    int markets_with_forecast = 0;
    int markets_with_actual = 0;

    double forecast_mae = 0.0;   // Mean absolute error of forecasts
    double market_accuracy = 0.0; // % of settled markets where Kalshi majority was correct

    std::vector<ComparisonPoint> points;
};

// Parse KXHIGHNY ticker format to extract date and strike
// Example: KXHIGHNY-25APR07-T72 -> date=2025-04-07, floor=72, cap=nullopt
// Example: KXHIGHNY-25APR07-B62T65 -> date=2025-04-07, floor=62, cap=65
struct TickerInfo {
    std::string date;  // YYYY-MM-DD format
    std::optional<int> floor_strike;
    std::optional<int> cap_strike;
    bool valid = false;
};

inline TickerInfo parseHighNYTicker(const std::string& ticker) {
    TickerInfo info;

    // Pattern: KXHIGHNY-YYMMMDD-[B##]T##
    // Examples:
    //   KXHIGHNY-25APR07-T72  (72 or higher)
    //   KXHIGHNY-25APR07-B62T65 (between 62 and 65)
    std::regex pattern(R"(KXHIGHNY-(\d{2})([A-Z]{3})(\d{2})-(?:B(\d+))?T(\d+))");
    std::smatch match;

    if (!std::regex_match(ticker, match, pattern)) {
        return info;
    }

    // Parse date
    std::string year = "20" + match[1].str();
    std::string month_abbr = match[2].str();
    std::string day = match[3].str();

    // Convert month abbreviation to number
    static const std::unordered_map<std::string, std::string> months = {
        {"JAN", "01"}, {"FEB", "02"}, {"MAR", "03"}, {"APR", "04"},
        {"MAY", "05"}, {"JUN", "06"}, {"JUL", "07"}, {"AUG", "08"},
        {"SEP", "09"}, {"OCT", "10"}, {"NOV", "11"}, {"DEC", "12"}
    };

    auto it = months.find(month_abbr);
    if (it == months.end()) {
        return info;
    }

    info.date = year + "-" + it->second + "-" + day;

    // Parse strikes
    if (match[4].matched) {
        info.floor_strike = std::stoi(match[4].str());
    }
    info.cap_strike = std::stoi(match[5].str());

    // If no floor, this is "T## or higher" so cap becomes floor
    if (!info.floor_strike.has_value()) {
        info.floor_strike = info.cap_strike;
        info.cap_strike = std::nullopt;  // No upper bound
    }

    info.valid = true;
    return info;
}

// Check if actual temp would settle YES for given strike range
inline bool wouldSettleYes(double actual_temp, std::optional<int> floor, std::optional<int> cap) {
    if (floor.has_value() && cap.has_value()) {
        // Between floor and cap (inclusive)
        return actual_temp >= floor.value() && actual_temp <= cap.value();
    } else if (floor.has_value()) {
        // Floor or higher
        return actual_temp >= floor.value();
    } else if (cap.has_value()) {
        // Cap or lower
        return actual_temp <= cap.value();
    }
    // Invalid
    return false;
}

class WeatherComparisonService {
public:
    WeatherComparisonService(api::KalshiClient& kalshi, api::OpenMeteoClient& openmeteo);

    // Analyze a series over a date range
    api::Result<ComparisonSummary> analyze(
        const std::string& series_ticker,
        const std::string& start_date,
        const std::string& end_date);

    // Get comparison data for a single market
    api::Result<ComparisonPoint> getPoint(const api::Market& market);

private:
    api::KalshiClient& kalshi_;
    api::OpenMeteoClient& openmeteo_;
};

} // namespace predibloom::core
