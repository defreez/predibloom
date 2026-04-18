#pragma once

#include "../api/result.hpp"
#include "../api/types.hpp"
#include "../api/kalshi_client.hpp"
#include "../api/openmeteo_client.hpp"
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace predibloom::core {

struct ComparisonPoint {
    std::string date;
    std::string market_ticker;
    double kalshi_price;        // Market implied probability (last price in cents)
    std::optional<double> forecast_temp;  // What Open-Meteo predicted
    std::optional<double> actual_temp;    // What actually happened
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

// Parse date from event_ticker (works for any series: KXHIGHNY, KXLOWTCHI, etc.)
// Event tickers have format: SERIES-YYMMMDD (e.g., KXHIGHNY-26APR10, KXLOWTCHI-26APR18)
// Returns YYYY-MM-DD or empty string on failure
inline std::string parseDateFromEventTicker(const std::string& event_ticker) {
    static const std::unordered_map<std::string, std::string> months = {
        {"JAN", "01"}, {"FEB", "02"}, {"MAR", "03"}, {"APR", "04"},
        {"MAY", "05"}, {"JUN", "06"}, {"JUL", "07"}, {"AUG", "08"},
        {"SEP", "09"}, {"OCT", "10"}, {"NOV", "11"}, {"DEC", "12"}
    };

    size_t dash = event_ticker.rfind('-');
    if (dash == std::string::npos || dash + 7 > event_ticker.size()) return "";

    std::string yy = event_ticker.substr(dash + 1, 2);
    std::string mmm = event_ticker.substr(dash + 3, 3);
    std::string dd = event_ticker.substr(dash + 6, 2);

    auto it = months.find(mmm);
    if (it == months.end()) return "";

    return "20" + yy + "-" + it->second + "-" + dd;
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

    // Set coordinates and whether this is a low-temp series
    void setLocation(double latitude, double longitude, bool is_low_temp = false);

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
    double latitude_ = 0;
    double longitude_ = 0;
    bool is_low_temp_ = false;
};

} // namespace predibloom::core
