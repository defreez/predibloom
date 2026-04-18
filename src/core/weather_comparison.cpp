#include "weather_comparison.hpp"
#include <cmath>
#include <algorithm>

namespace predibloom::core {

WeatherComparisonService::WeatherComparisonService(
    api::KalshiClient& kalshi,
    api::OpenMeteoClient& openmeteo)
    : kalshi_(kalshi)
    , openmeteo_(openmeteo) {
}

void WeatherComparisonService::setLocation(double latitude, double longitude, bool is_low_temp) {
    latitude_ = latitude;
    longitude_ = longitude;
    is_low_temp_ = is_low_temp;
}

api::Result<ComparisonPoint> WeatherComparisonService::getPoint(const api::Market& market) {
    ComparisonPoint point;
    point.market_ticker = market.ticker;
    point.kalshi_price = market.last_price_cents();
    point.settlement = market.result;

    // Parse date from event_ticker (works for any series)
    std::string date = parseDateFromEventTicker(market.event_ticker);
    if (date.empty()) {
        return api::Error(api::ApiError::ParseError,
            "Could not parse date from event_ticker: " + market.event_ticker);
    }

    point.date = date;

    // Use API-provided strikes directly
    point.floor_strike = market.floor_strike;
    point.cap_strike = market.cap_strike;

    // Fetch weather data for this date
    auto actual_result = openmeteo_.getHistoricalWeather(
        latitude_, longitude_, date, date);

    if (actual_result.ok()) {
        point.actual_temp = is_low_temp_
            ? getMinTemperatureForDate(actual_result.value(), date)
            : getTemperatureForDate(actual_result.value(), date);
    }

    auto forecast_result = openmeteo_.getHistoricalForecast(
        latitude_, longitude_, date, date);

    if (forecast_result.ok()) {
        point.forecast_temp = is_low_temp_
            ? getMinTemperatureForDate(forecast_result.value(), date)
            : getTemperatureForDate(forecast_result.value(), date);
    }

    return point;
}

api::Result<ComparisonSummary> WeatherComparisonService::analyze(
    const std::string& series_ticker,
    const std::string& start_date,
    const std::string& end_date) {

    ComparisonSummary summary;
    summary.series_ticker = series_ticker;

    // Fetch all markets for the series (both open and settled)
    api::GetMarketsParams params;
    params.series_ticker = series_ticker;

    auto markets_result = kalshi_.getAllMarkets(params);
    if (!markets_result.ok()) {
        return markets_result.error();
    }

    // Fetch weather data for the full range
    auto actual_result = openmeteo_.getHistoricalWeather(
        latitude_, longitude_, start_date, end_date);

    auto forecast_result = openmeteo_.getHistoricalForecast(
        latitude_, longitude_, start_date, end_date);

    // Process each market
    double forecast_error_sum = 0.0;
    int correct_predictions = 0;
    int total_predictions = 0;

    for (const auto& market : markets_result.value()) {
        std::string date = parseDateFromEventTicker(market.event_ticker);
        if (date.empty()) continue;

        // Check if market date is within our range
        if (date < start_date || date > end_date) continue;

        ComparisonPoint point;
        point.market_ticker = market.ticker;
        point.date = date;
        point.kalshi_price = market.last_price_cents();
        point.settlement = market.result;
        // Use API-provided strikes directly
        point.floor_strike = market.floor_strike;
        point.cap_strike = market.cap_strike;

        // Get weather data for this date
        if (actual_result.ok()) {
            point.actual_temp = is_low_temp_
                ? getMinTemperatureForDate(actual_result.value(), date)
                : getTemperatureForDate(actual_result.value(), date);
            if (point.actual_temp.has_value()) {
                summary.markets_with_actual++;
            }
        }

        if (forecast_result.ok()) {
            point.forecast_temp = is_low_temp_
                ? getMinTemperatureForDate(forecast_result.value(), date)
                : getTemperatureForDate(forecast_result.value(), date);
            if (point.forecast_temp.has_value()) {
                summary.markets_with_forecast++;
            }
        }

        // Calculate forecast accuracy if we have both
        if (point.forecast_temp.has_value() && point.actual_temp.has_value()) {
            forecast_error_sum += std::abs(point.forecast_temp.value() - point.actual_temp.value());
        }

        // Check market prediction accuracy
        if (!market.result.empty() && point.actual_temp.has_value() &&
            (point.floor_strike.has_value() || point.cap_strike.has_value())) {

            summary.settled_markets++;

            bool market_predicted_yes = point.kalshi_price >= 50.0;
            bool actual_was_yes = wouldSettleYes(
                point.actual_temp.value(),
                point.floor_strike,
                point.cap_strike);

            if (market_predicted_yes == actual_was_yes) {
                correct_predictions++;
            }
            total_predictions++;
        }

        summary.points.push_back(point);
        summary.total_markets++;
    }

    // Calculate summary statistics
    if (summary.markets_with_forecast > 0 && summary.markets_with_actual > 0) {
        int min_count = std::min(summary.markets_with_forecast, summary.markets_with_actual);
        if (min_count > 0) {
            summary.forecast_mae = forecast_error_sum / min_count;
        }
    }

    if (total_predictions > 0) {
        summary.market_accuracy = (double)correct_predictions / total_predictions * 100.0;
    }

    // Sort points by date
    std::sort(summary.points.begin(), summary.points.end(),
        [](const ComparisonPoint& a, const ComparisonPoint& b) {
            return a.date < b.date;
        });

    return summary;
}

} // namespace predibloom::core
