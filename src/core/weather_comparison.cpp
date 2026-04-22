#include "weather_comparison.hpp"
#include "time_utils.hpp"
#include <cmath>
#include <algorithm>

namespace predibloom::core {

WeatherComparisonService::WeatherComparisonService(
    api::KalshiClient& kalshi,
    api::GribStreamClient& gribstream)
    : kalshi_(kalshi)
    , gribstream_(gribstream) {
}

void WeatherComparisonService::setLocation(double latitude, double longitude,
                                            bool is_low_temp,
                                            int entry_day_offset,
                                            int entry_hour) {
    latitude_ = latitude;
    longitude_ = longitude;
    is_low_temp_ = is_low_temp;
    entry_day_offset_ = entry_day_offset;
    entry_hour_ = entry_hour;
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

    // Actuals: shortest-lead NBM value for the day.
    auto actual_result = gribstream_.getActuals(latitude_, longitude_, date);
    if (actual_result.ok()) {
        point.actual_temp = is_low_temp_
            ? api::getMinTemperatureForDate(actual_result.value(), date)
            : api::getTemperatureForDate(actual_result.value(), date);
    }

    // Forecast: NBM as-of the configured entry moment.
    std::string as_of = computeAsOfIso(date, entry_day_offset_, entry_hour_);
    auto forecast_result = gribstream_.getForecast(latitude_, longitude_, date, as_of);
    if (forecast_result.ok()) {
        point.forecast_temp = is_low_temp_
            ? api::getMinTemperatureForDate(forecast_result.value(), date)
            : api::getTemperatureForDate(forecast_result.value(), date);
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

    // Process each market; fetch weather per-date so we can use per-day asOf.
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
        point.floor_strike = market.floor_strike;
        point.cap_strike = market.cap_strike;

        // Actuals (shortest-lead NBM)
        auto actual_result = gribstream_.getActuals(latitude_, longitude_, date);
        if (actual_result.ok()) {
            point.actual_temp = is_low_temp_
                ? api::getMinTemperatureForDate(actual_result.value(), date)
                : api::getTemperatureForDate(actual_result.value(), date);
            if (point.actual_temp.has_value()) {
                summary.markets_with_actual++;
            }
        }

        // Forecast (asOf = entry moment)
        std::string as_of = computeAsOfIso(date, entry_day_offset_, entry_hour_);
        auto forecast_result = gribstream_.getForecast(latitude_, longitude_, date, as_of);
        if (forecast_result.ok()) {
            point.forecast_temp = is_low_temp_
                ? api::getMinTemperatureForDate(forecast_result.value(), date)
                : api::getTemperatureForDate(forecast_result.value(), date);
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
