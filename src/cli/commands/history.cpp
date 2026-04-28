#include "history.hpp"
#include "../../api/gribstream_client.hpp"
#include "../../api/nws_client.hpp"
#include "../../core/weather_comparison.hpp"
#include "../../core/datetime.hpp"

#include <iostream>
#include <iomanip>
#include <map>

namespace predibloom::cli {

int runHistory(const std::string& series, const std::string& start_date,
               const std::string& end_date, api::KalshiClient& client) {
    api::GetMarketsParams params;
    params.series_ticker = series;

    auto markets_result = client.getAllMarkets(params);
    if (!markets_result.ok()) {
        std::cerr << "Error: " << markets_result.error().message << "\n";
        return 1;
    }

    std::cout << "timestamp,ticker,strike,price_cents\n";
    int processed = 0;
    int total = markets_result.value().size();

    for (const auto& market : markets_result.value()) {
        std::string market_date = core::parseDateFromEventTicker(market.event_ticker);
        if (market_date.empty()) continue;
        if (market_date < start_date || market_date > end_date) continue;

        auto trades_result = client.getAllTrades(market.ticker);
        if (!trades_result.ok()) continue;

        std::map<std::string, double> hourly_prices;
        for (const auto& trade : trades_result.value()) {
            std::string hour = trade.created_time.substr(0, 13);
            hourly_prices[hour] = trade.yes_price_cents();
        }

        std::string strike;
        if (market.floor_strike && !market.cap_strike) {
            strike = std::to_string(*market.floor_strike) + "+";
        } else if (!market.floor_strike && market.cap_strike) {
            strike = "<" + std::to_string(*market.cap_strike);
        } else if (market.floor_strike && market.cap_strike) {
            strike = std::to_string(*market.floor_strike) + "-" + std::to_string(*market.cap_strike);
        }

        for (const auto& [hour, price] : hourly_prices) {
            std::cout << hour << "," << market.ticker << "," << strike << "," << price << "\n";
        }
        processed++;
        std::cerr << "\rProcessed " << processed << "/" << total << " markets" << std::flush;
    }
    std::cerr << "\n";
    return 0;
}

int runWinners(const std::string& series, const std::string& start_date,
               const std::string& end_date, const core::Config& config,
               api::KalshiClient& client) {
    auto* series_config = config.findSeries(series);
    if (!series_config || series_config->nws_station.empty()) {
        std::cerr << "Series not configured: " << series << "\n";
        return 1;
    }
    if (!config.hasGribstream()) {
        std::cerr << "GribStream API token not configured\n";
        return 1;
    }

    api::GribStreamClient gribstream(config.gribstream_api_token);
    gribstream.setCaching(true);
    api::NwsClient nws;

    api::GetMarketsParams params;
    params.series_ticker = series;

    auto markets_result = client.getAllMarkets(params);
    if (!markets_result.ok()) {
        std::cerr << "Error: " << markets_result.error().message << "\n";
        return 1;
    }

    static const std::map<std::string, std::string> month_to_num = {
        {"JAN", "01"}, {"FEB", "02"}, {"MAR", "03"}, {"APR", "04"},
        {"MAY", "05"}, {"JUN", "06"}, {"JUL", "07"}, {"AUG", "08"},
        {"SEP", "09"}, {"OCT", "10"}, {"NOV", "11"}, {"DEC", "12"}
    };

    bool is_low = series_config->isLowTemp();
    std::map<std::string, api::Market> winners;

    for (const auto& market : markets_result.value()) {
        if (market.result != "yes") continue;
        std::string et = market.event_ticker;
        size_t dash = et.rfind('-');
        if (dash == std::string::npos || dash + 7 > et.size()) continue;

        std::string yy = et.substr(dash + 1, 2);
        std::string mmm = et.substr(dash + 3, 3);
        std::string dd = et.substr(dash + 6, 2);

        auto it = month_to_num.find(mmm);
        if (it == month_to_num.end()) continue;

        std::string date = "20" + yy + "-" + it->second + "-" + dd;
        if (date < start_date || date > end_date) continue;
        winners[date] = market;
    }

    std::cerr << "Found " << winners.size() << " winning brackets\n";

    int start_year = std::stoi(start_date.substr(0, 4));
    int end_year = std::stoi(end_date.substr(0, 4));
    std::map<std::string, int> nws_temps;

    for (int year = start_year; year <= end_year; year++) {
        auto nws_result = nws.getCliData(series_config->nws_station, year);
        if (nws_result.ok()) {
            for (const auto& obs : nws_result.value()) {
                if (obs.date >= start_date && obs.date <= end_date) {
                    nws_temps[obs.date] = is_low ? obs.low : obs.high;
                }
            }
        }
    }

    std::cerr << "Fetched " << nws_temps.size() << " NWS observations\n";
    std::cout << "timestamp,date,ticker,strike,price_cents,forecast_temp,gribstream_actual,nws_temp\n";

    int processed = 0;
    for (const auto& [date, market] : winners) {
        std::optional<double> forecast_temp;
        std::optional<double> gribstream_actual;
        std::optional<int> nws_temp;

        std::string as_of = core::computeAsOfIso(
            date, series_config->entry_day_offset, series_config->effectiveEntryHour());
        auto forecast_result = gribstream.getForecast(
            series_config->latitude, series_config->longitude, date, as_of);
        if (forecast_result.ok()) {
            forecast_temp = is_low
                ? api::getMinTemperatureForDate(forecast_result.value(), date)
                : api::getTemperatureForDate(forecast_result.value(), date);
        }

        auto actual_result = gribstream.getActuals(
            series_config->latitude, series_config->longitude, date);
        if (actual_result.ok()) {
            gribstream_actual = is_low
                ? api::getMinTemperatureForDate(actual_result.value(), date)
                : api::getTemperatureForDate(actual_result.value(), date);
        }
        if (nws_temps.count(date)) nws_temp = nws_temps.at(date);

        std::string strike;
        if (market.floor_strike && !market.cap_strike) {
            strike = std::to_string(*market.floor_strike) + "+";
        } else if (!market.floor_strike && market.cap_strike) {
            strike = "<" + std::to_string(*market.cap_strike);
        } else if (market.floor_strike && market.cap_strike) {
            strike = std::to_string(*market.floor_strike) + "-" + std::to_string(*market.cap_strike);
        }

        auto trades_result = client.getAllTrades(market.ticker);
        if (!trades_result.ok()) continue;

        std::map<std::string, double> hourly_prices;
        for (const auto& trade : trades_result.value()) {
            std::string hour = trade.created_time.substr(0, 13);
            hourly_prices[hour] = trade.yes_price_cents();
        }

        for (const auto& [hour, price] : hourly_prices) {
            std::cout << hour << "," << date << "," << market.ticker << "," << strike << "," << price;
            if (forecast_temp) std::cout << "," << std::fixed << std::setprecision(1) << *forecast_temp;
            else std::cout << ",";
            if (gribstream_actual) std::cout << "," << std::fixed << std::setprecision(1) << *gribstream_actual;
            else std::cout << ",";
            if (nws_temp) std::cout << "," << *nws_temp;
            else std::cout << ",";
            std::cout << "\n";
        }
        processed++;
        std::cerr << "\rProcessed " << processed << "/" << winners.size() << " winners" << std::flush;
    }
    std::cerr << "\n";
    return 0;
}

}  // namespace predibloom::cli
