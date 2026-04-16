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

api::Result<ComparisonPoint> WeatherComparisonService::getPoint(const api::Market& market) {
    ComparisonPoint point;
    point.market_ticker = market.ticker;
    point.kalshi_price = market.last_price_cents();
    point.settlement = market.result;

    // Parse ticker for date (and fallback strikes)
    auto ticker_info = parseHighNYTicker(market.ticker);
    if (!ticker_info.valid) {
        return api::Error(api::ApiError::ParseError,
            "Could not parse ticker: " + market.ticker);
    }

    point.date = ticker_info.date;

    // Use API-provided strikes directly
    point.floor_strike = market.floor_strike;
    point.cap_strike = market.cap_strike;

    // Fetch weather data for this date
    auto actual_result = openmeteo_.getHistoricalWeather(
        NYC_LATITUDE, NYC_LONGITUDE, ticker_info.date, ticker_info.date);

    if (actual_result.ok()) {
        point.actual_high = getTemperatureForDate(actual_result.value(), ticker_info.date);
    }

    auto forecast_result = openmeteo_.getHistoricalForecast(
        NYC_LATITUDE, NYC_LONGITUDE, ticker_info.date, ticker_info.date);

    if (forecast_result.ok()) {
        point.forecast_high = getTemperatureForDate(forecast_result.value(), ticker_info.date);
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
        NYC_LATITUDE, NYC_LONGITUDE, start_date, end_date);

    auto forecast_result = openmeteo_.getHistoricalForecast(
        NYC_LATITUDE, NYC_LONGITUDE, start_date, end_date);

    // Process each market
    double forecast_error_sum = 0.0;
    int correct_predictions = 0;
    int total_predictions = 0;

    for (const auto& market : markets_result.value()) {
        auto ticker_info = parseHighNYTicker(market.ticker);
        if (!ticker_info.valid) {
            continue;
        }

        // Check if market date is within our range
        if (ticker_info.date < start_date || ticker_info.date > end_date) {
            continue;
        }

        ComparisonPoint point;
        point.market_ticker = market.ticker;
        point.date = ticker_info.date;
        point.kalshi_price = market.last_price_cents();
        point.settlement = market.result;
        // Use API-provided strikes directly
        point.floor_strike = market.floor_strike;
        point.cap_strike = market.cap_strike;

        // Get weather data for this date
        if (actual_result.ok()) {
            point.actual_high = getTemperatureForDate(actual_result.value(), ticker_info.date);
            if (point.actual_high.has_value()) {
                summary.markets_with_actual++;
            }
        }

        if (forecast_result.ok()) {
            point.forecast_high = getTemperatureForDate(forecast_result.value(), ticker_info.date);
            if (point.forecast_high.has_value()) {
                summary.markets_with_forecast++;
            }
        }

        // Calculate forecast accuracy if we have both
        if (point.forecast_high.has_value() && point.actual_high.has_value()) {
            forecast_error_sum += std::abs(point.forecast_high.value() - point.actual_high.value());
        }

        // Check market prediction accuracy
        if (!market.result.empty() && point.actual_high.has_value() &&
            (point.floor_strike.has_value() || point.cap_strike.has_value())) {

            summary.settled_markets++;

            bool market_predicted_yes = point.kalshi_price >= 50.0;
            bool actual_was_yes = wouldSettleYes(
                point.actual_high.value(),
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
