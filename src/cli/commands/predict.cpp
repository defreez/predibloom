#include "predict.hpp"
#include "../bracket.hpp"
#include "../../api/weather_client.hpp"
#include "../../api/gribstream_types.hpp"
#include "../../core/time_utils.hpp"

#include <iostream>
#include <iomanip>
#include <map>
#include <vector>
#include <cstdio>

namespace predibloom::cli {

namespace {

const std::map<std::string, std::string> MONTH_MAP = {
    {"01", "JAN"}, {"02", "FEB"}, {"03", "MAR"}, {"04", "APR"},
    {"05", "MAY"}, {"06", "JUN"}, {"07", "JUL"}, {"08", "AUG"},
    {"09", "SEP"}, {"10", "OCT"}, {"11", "NOV"}, {"12", "DEC"}
};

struct Prediction {
    std::string label;
    std::string series;
    double forecast;
    double adjusted;
    std::string strike;
    std::string ticker;
    double margin;
    double bid;
    double ask;
    bool tradeable;
    bool between_brackets;
};

}  // namespace

int runPredict(const PredictOptions& opts,
               const core::Config& config,
               api::KalshiClient& client) {
    // Build list of series to process
    std::vector<const core::TrackedSeries*> series_list;
    if (!opts.series.empty()) {
        auto* sc = config.findSeries(opts.series);
        if (!sc || sc->latitude == 0) {
            std::cerr << "Series not configured or missing weather params: " << opts.series << "\n";
            return 1;
        }
        series_list.push_back(sc);
    } else {
        // Use all configured series with weather params
        for (const auto& tab : config.tabs) {
            for (const auto& sc : tab.series) {
                if (sc.latitude != 0 && !sc.nws_station.empty()) {
                    series_list.push_back(&sc);
                }
            }
        }
    }

    if (series_list.empty()) {
        std::cerr << "No series configured with weather params\n";
        return 1;
    }

    // Check if any series needs GribStream
    bool needs_gribstream = false;
    for (const auto* sc : series_list) {
        if (sc->weather_source == core::WeatherSource::GribStream) {
            needs_gribstream = true;
            break;
        }
    }
    if (needs_gribstream && !config.hasGribstream()) {
        std::cerr << "GribStream API token not configured. Add gribstream_api_token to ~/.config/predibloom/auth.json\n";
        return 1;
    }

    // Date parsing for event ticker matching
    std::string yy = opts.date.substr(2, 2);
    std::string mm = opts.date.substr(5, 2);
    std::string dd = opts.date.substr(8, 2);

    // Collect predictions for all series
    std::vector<Prediction> predictions;
    std::string most_recent_forecast_time;

    size_t total = series_list.size();
    size_t current = 0;

    for (const auto* series_config : series_list) {
        current++;
        std::cerr << "\rFetching " << current << "/" << total << ": "
                  << std::left << std::setw(20) << series_config->label << std::flush;

        double effective_offset = series_config->offset;

        // Create weather client for this series
        auto weather_client = api::WeatherClient::create(
            series_config->weather_source, config.gribstream_api_token);

        // Get forecast (best/latest run — no asOf for current-day predict)
        auto forecast_result = weather_client->getForecast(
            series_config->latitude, series_config->longitude, opts.date);

        if (!forecast_result.ok()) {
            continue;
        }

        // Track the most recent forecast issue time
        const auto& forecasted_at = forecast_result.value().forecasted_at;
        if (!forecasted_at.empty() && forecasted_at > most_recent_forecast_time) {
            most_recent_forecast_time = forecasted_at;
        }

        auto forecast_opt = series_config->isLowTemp()
            ? api::getMinTemperatureForDate(forecast_result.value(), opts.date)
            : api::getTemperatureForDate(forecast_result.value(), opts.date);
        if (!forecast_opt) {
            continue;
        }

        double forecast = *forecast_opt;
        double adjusted = forecast + effective_offset;

        // Get markets
        api::GetMarketsParams params;
        params.series_ticker = series_config->series_ticker;
        auto markets_result = client.getAllMarkets(params);
        if (!markets_result.ok()) {
            continue;
        }

        std::string expected_event = series_config->series_ticker + "-" + yy + MONTH_MAP.at(mm) + dd;

        // Find markets for this date
        std::vector<api::Market> day_markets;
        for (const auto& market : markets_result.value()) {
            if (market.event_ticker == expected_event) {
                day_markets.push_back(market);
            }
        }

        if (day_markets.empty()) {
            continue;
        }

        // Parse brackets and find target
        std::vector<Bracket> brackets;
        for (const auto& market : day_markets) {
            brackets.push_back(parseBracket(market));
        }

        // Find the bracket our adjusted forecast falls into
        const Bracket* target = nullptr;
        double margin_from_edge = 0;
        for (const auto& b : brackets) {
            if (b.contains(adjusted)) {
                margin_from_edge = b.marginFrom(adjusted);
                target = &b;
                break;
            }
        }

        Prediction p;
        p.label = series_config->label;
        p.series = series_config->series_ticker;
        p.forecast = forecast;
        p.adjusted = adjusted;
        p.between_brackets = (target == nullptr);
        if (target) {
            p.strike = target->displayString();
            p.ticker = target->market->ticker;
            p.margin = margin_from_edge;
            p.bid = target->market->yes_bid_cents();
            p.ask = target->market->yes_ask_cents();
            p.tradeable = (margin_from_edge >= opts.margin) && (p.ask >= opts.min_price) && (p.ask <= opts.max_price);
        } else {
            p.strike = "---";
            p.ticker = "";
            p.margin = 0;
            p.bid = 0;
            p.ask = 0;
            p.tradeable = false;
        }

        predictions.push_back(p);
    }

    // Clear progress line
    std::cerr << "\r" << std::string(50, ' ') << "\r";

    // Output results
    std::cout << "=== PREDICTIONS FOR " << opts.date << " ===\n";
    if (!most_recent_forecast_time.empty()) {
        std::string formatted = core::formatUtcAsPtWithAge(most_recent_forecast_time);
        if (!formatted.empty()) {
            std::cout << "Forecast issued: " << formatted << "\n";
        } else {
            std::cout << "Forecast issued: " << most_recent_forecast_time << "\n";
        }
    }
    std::cout << "\n";

    std::cout << std::left
              << std::setw(18) << "City"
              << std::setw(10) << "Forecast"
              << std::setw(10) << "Adjusted"
              << std::setw(10) << "Bracket"
              << std::setw(8) << "Margin"
              << std::setw(8) << "Bid"
              << std::setw(8) << "Ask"
              << "Signal\n";
    std::cout << std::string(78, '-') << "\n";

    int tradeable_count = 0;
    for (const auto& p : predictions) {
        char forecast_buf[16], adjusted_buf[16], margin_buf[16];
        snprintf(forecast_buf, sizeof(forecast_buf), "%.1f°F", p.forecast);
        snprintf(adjusted_buf, sizeof(adjusted_buf), "%.1f°F", p.adjusted);

        std::cout << std::left
                  << std::setw(18) << p.label
                  << std::setw(10) << forecast_buf
                  << std::setw(10) << adjusted_buf
                  << std::setw(10) << p.strike;

        if (p.between_brackets) {
            std::cout << std::setw(8) << "---"
                      << std::setw(8) << "---"
                      << std::setw(8) << "---"
                      << "BETWEEN";
        } else {
            snprintf(margin_buf, sizeof(margin_buf), "%.1f°F", p.margin);
            std::cout << std::setw(8) << margin_buf
                      << std::setw(8) << (std::to_string((int)p.bid) + "¢")
                      << std::setw(8) << (std::to_string((int)p.ask) + "¢");
            if (p.tradeable) {
                std::cout << "BUY";
                tradeable_count++;
            } else if (p.margin >= opts.margin && p.ask > opts.max_price) {
                std::cout << "EXPENSIVE";
            } else if (p.margin >= opts.margin && p.ask < opts.min_price) {
                std::cout << "SUS";
            } else {
                std::cout << "-";
            }
        }
        std::cout << "\n";
    }

    std::cout << std::string(78, '-') << "\n";
    std::cout << "Tradeable signals: " << tradeable_count << "/" << predictions.size()
              << " (margin >= " << opts.margin << "°F, ask " << (int)opts.min_price << "-" << (int)opts.max_price << "¢)\n";

    if (tradeable_count > 0) {
        std::cout << "\nTickers to buy:\n";
        for (const auto& p : predictions) {
            if (p.tradeable) {
                std::cout << "  " << p.ticker << "  " << p.label << " " << p.strike
                          << " @ " << (int)p.ask << "¢\n";
            }
        }
    }

    return 0;
}

}  // namespace predibloom::cli
