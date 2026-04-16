#include <CLI/CLI.hpp>
#include "../api/kalshi_client.hpp"
#include "../api/openmeteo_client.hpp"
#include "../core/service.hpp"
#include "../core/weather_comparison.hpp"
#include "formatters.hpp"
#include <iostream>
#include <iomanip>
#include <map>

int main(int argc, char** argv) {
    CLI::App app{"predibloom - Kalshi market viewer"};

    // Global options
    std::string format = "table";

    // Subcommands
    auto* markets_cmd = app.add_subcommand("markets", "List markets");
    auto* events_cmd = app.add_subcommand("events", "List events");
    auto* orderbook_cmd = app.add_subcommand("orderbook", "Show orderbook for a market");
    auto* market_cmd = app.add_subcommand("market", "Show details for a single market");
    auto* compare_cmd = app.add_subcommand("compare", "Compare Kalshi prices with weather data");
    auto* history_cmd = app.add_subcommand("history", "Hourly price history for all brackets");
    auto* winners_cmd = app.add_subcommand("winners", "Price history for settled brackets with weather overlay");

    // Markets command options
    std::string markets_status = "open";
    std::string markets_event_ticker;
    std::string markets_series_ticker;
    int markets_limit = 10;
    std::string markets_format = "table";

    markets_cmd->add_option("-s,--status", markets_status, "Filter by status (open, closed, settled)")
        ->default_val("open");
    markets_cmd->add_option("-e,--event", markets_event_ticker, "Filter by event ticker");
    markets_cmd->add_option("--series-ticker", markets_series_ticker, "Filter by series ticker");
    markets_cmd->add_option("-n,--limit", markets_limit, "Number of results")
        ->default_val(10)
        ->check(CLI::Range(1, 1000));
    markets_cmd->add_option("-f,--format", markets_format, "Output format: table|json|csv")
        ->default_val("table")
        ->check(CLI::IsMember({"table", "json", "csv"}));

    // Events command options
    std::string events_status;
    int events_limit = 10;
    std::string events_format = "table";

    events_cmd->add_option("-s,--status", events_status, "Filter by status");
    events_cmd->add_option("-n,--limit", events_limit, "Number of results")
        ->default_val(10)
        ->check(CLI::Range(1, 1000));
    events_cmd->add_option("-f,--format", events_format, "Output format: table|json|csv")
        ->default_val("table")
        ->check(CLI::IsMember({"table", "json", "csv"}));

    // Orderbook command options
    std::string orderbook_ticker;
    int orderbook_depth = 10;
    std::string orderbook_format = "table";

    orderbook_cmd->add_option("ticker", orderbook_ticker, "Market ticker")
        ->required();
    orderbook_cmd->add_option("-d,--depth", orderbook_depth, "Depth of orderbook")
        ->default_val(10)
        ->check(CLI::Range(1, 100));
    orderbook_cmd->add_option("-f,--format", orderbook_format, "Output format: table|json|csv")
        ->default_val("table")
        ->check(CLI::IsMember({"table", "json", "csv"}));

    // Market detail command options
    std::string market_ticker;
    std::string market_format = "table";

    market_cmd->add_option("ticker", market_ticker, "Market ticker")
        ->required();
    market_cmd->add_option("-f,--format", market_format, "Output format: table|json")
        ->default_val("table")
        ->check(CLI::IsMember({"table", "json"}));

    // Compare command options
    std::string compare_series;
    std::string compare_start;
    std::string compare_end;
    std::string compare_format = "table";

    compare_cmd->add_option("-s,--series", compare_series, "Series ticker (e.g., KXHIGHNY)")
        ->required();
    compare_cmd->add_option("--start", compare_start, "Start date YYYY-MM-DD")
        ->required();
    compare_cmd->add_option("--end", compare_end, "End date YYYY-MM-DD")
        ->required();
    compare_cmd->add_option("-f,--format", compare_format, "Output format: table|json|csv")
        ->default_val("table")
        ->check(CLI::IsMember({"table", "json", "csv"}));

    // History command options
    std::string history_series;
    std::string history_start;
    std::string history_end;

    history_cmd->add_option("-s,--series", history_series, "Series ticker (e.g., KXHIGHNY)")
        ->required();
    history_cmd->add_option("--start", history_start, "Start date YYYY-MM-DD")
        ->required();
    history_cmd->add_option("--end", history_end, "End date YYYY-MM-DD")
        ->required();

    // Winners command options
    std::string winners_series;
    std::string winners_start;
    std::string winners_end;

    winners_cmd->add_option("-s,--series", winners_series, "Series ticker (e.g., KXHIGHNY)")
        ->required();
    winners_cmd->add_option("--start", winners_start, "Start date YYYY-MM-DD")
        ->required();
    winners_cmd->add_option("--end", winners_end, "End date YYYY-MM-DD")
        ->required();

    // Require at least one subcommand
    app.require_subcommand(1);

    CLI11_PARSE(app, argc, argv);

    // Create client and service
    predibloom::api::KalshiClient client;
    predibloom::core::MarketService service(client);

    // Handle markets command
    if (*markets_cmd) {
        predibloom::core::MarketFilter filter;
        filter.status = markets_status;
        if (!markets_event_ticker.empty()) {
            filter.event_ticker = markets_event_ticker;
        }
        if (!markets_series_ticker.empty()) {
            filter.series_ticker = markets_series_ticker;
        }
        filter.limit = markets_limit;

        auto result = service.listMarkets(filter);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error().message << "\n";
            return 1;
        }

        predibloom::cli::printMarkets(result.value(),
            predibloom::cli::parseFormat(markets_format));
    }

    // Handle events command
    if (*events_cmd) {
        predibloom::core::EventFilter filter;
        if (!events_status.empty()) {
            filter.status = events_status;
        }
        filter.limit = events_limit;

        auto result = service.listEvents(filter);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error().message << "\n";
            return 1;
        }

        predibloom::cli::printEvents(result.value(),
            predibloom::cli::parseFormat(events_format));
    }

    // Handle orderbook command
    if (*orderbook_cmd) {
        auto result = service.getOrderbook(orderbook_ticker, orderbook_depth);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error().message << "\n";
            return 1;
        }

        predibloom::cli::printOrderbook(result.value(),
            predibloom::cli::parseFormat(orderbook_format));
    }

    // Handle market detail command
    if (*market_cmd) {
        auto result = service.getMarket(market_ticker);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error().message << "\n";
            return 1;
        }

        predibloom::cli::printMarketDetail(result.value(),
            predibloom::cli::parseFormat(market_format));
    }

    // Handle compare command
    if (*compare_cmd) {
        predibloom::api::OpenMeteoClient openmeteo;
        predibloom::core::WeatherComparisonService comparison(client, openmeteo);

        auto result = comparison.analyze(compare_series, compare_start, compare_end);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error().message << "\n";
            return 1;
        }

        predibloom::cli::printComparison(result.value(),
            predibloom::cli::parseFormat(compare_format));
    }

    // Handle history command
    if (*history_cmd) {
        // Get all markets for the series
        predibloom::api::GetMarketsParams params;
        params.series_ticker = history_series;

        auto markets_result = client.getAllMarkets(params);
        if (!markets_result.ok()) {
            std::cerr << "Error fetching markets: " << markets_result.error().message << "\n";
            return 1;
        }

        // Output CSV header
        std::cout << "timestamp,ticker,strike,price_cents\n";

        // For each market, fetch trades and output hourly prices
        int processed = 0;
        int total = markets_result.value().size();
        for (const auto& market : markets_result.value()) {
            // Parse ticker to get date
            auto ticker_info = predibloom::core::parseHighNYTicker(market.ticker);
            if (!ticker_info.valid) continue;

            // Filter by date range
            if (ticker_info.date < history_start || ticker_info.date > history_end) continue;

            // Fetch all trades for this market
            auto trades_result = client.getAllTrades(market.ticker);
            if (!trades_result.ok()) {
                std::cerr << "Error fetching trades for " << market.ticker << "\n";
                continue;
            }

            // Group trades by hour and output
            std::map<std::string, double> hourly_prices;
            for (const auto& trade : trades_result.value()) {
                // Extract hour from timestamp (YYYY-MM-DDTHH)
                std::string hour = trade.created_time.substr(0, 13);
                hourly_prices[hour] = trade.yes_price_cents();
            }

            // Format strike
            std::string strike;
            if (market.floor_strike && !market.cap_strike) {
                strike = std::to_string(*market.floor_strike) + "+";
            } else if (!market.floor_strike && market.cap_strike) {
                strike = "<" + std::to_string(*market.cap_strike);
            } else if (market.floor_strike && market.cap_strike) {
                strike = std::to_string(*market.floor_strike) + "-" + std::to_string(*market.cap_strike);
            }

            // Output each hour
            for (const auto& [hour, price] : hourly_prices) {
                std::cout << hour << "," << market.ticker << "," << strike << "," << price << "\n";
            }

            processed++;
            std::cerr << "\rProcessed " << processed << "/" << total << " markets" << std::flush;
        }
        std::cerr << "\n";
    }

    // Handle winners command
    if (*winners_cmd) {
        predibloom::api::OpenMeteoClient openmeteo;

        // Get all markets for the series
        predibloom::api::GetMarketsParams params;
        params.series_ticker = winners_series;

        auto markets_result = client.getAllMarkets(params);
        if (!markets_result.ok()) {
            std::cerr << "Error fetching markets: " << markets_result.error().message << "\n";
            return 1;
        }

        // Group markets by date and find winners (settled YES)
        std::map<std::string, predibloom::api::Market> winners;  // date -> winning market
        for (const auto& market : markets_result.value()) {
            if (market.result != "yes") continue;

            auto ticker_info = predibloom::core::parseHighNYTicker(market.ticker);
            if (!ticker_info.valid) continue;
            if (ticker_info.date < winners_start || ticker_info.date > winners_end) continue;

            winners[ticker_info.date] = market;
        }

        std::cerr << "Found " << winners.size() << " winning brackets\n";

        // Fetch weather data for full range
        auto forecast_result = openmeteo.getHistoricalForecast(
            predibloom::core::NYC_LATITUDE, predibloom::core::NYC_LONGITUDE,
            winners_start, winners_end);

        auto actual_result = openmeteo.getHistoricalWeather(
            predibloom::core::NYC_LATITUDE, predibloom::core::NYC_LONGITUDE,
            winners_start, winners_end);

        // Output CSV header
        std::cout << "timestamp,date,ticker,strike,price_cents,forecast_high,actual_high\n";

        int processed = 0;
        for (const auto& [date, market] : winners) {
            // Get forecast and actual for this date
            std::optional<double> forecast_high, actual_high;
            if (forecast_result.ok()) {
                forecast_high = predibloom::api::getTemperatureForDate(forecast_result.value(), date);
            }
            if (actual_result.ok()) {
                actual_high = predibloom::api::getTemperatureForDate(actual_result.value(), date);
            }

            // Format strike
            std::string strike;
            if (market.floor_strike && !market.cap_strike) {
                strike = std::to_string(*market.floor_strike) + "+";
            } else if (!market.floor_strike && market.cap_strike) {
                strike = "<" + std::to_string(*market.cap_strike);
            } else if (market.floor_strike && market.cap_strike) {
                strike = std::to_string(*market.floor_strike) + "-" + std::to_string(*market.cap_strike);
            }

            // Fetch trades for this market
            auto trades_result = client.getAllTrades(market.ticker);
            if (!trades_result.ok()) {
                std::cerr << "Error fetching trades for " << market.ticker << "\n";
                continue;
            }

            // Group trades by hour
            std::map<std::string, double> hourly_prices;
            for (const auto& trade : trades_result.value()) {
                std::string hour = trade.created_time.substr(0, 13);
                hourly_prices[hour] = trade.yes_price_cents();
            }

            // Output each hour
            for (const auto& [hour, price] : hourly_prices) {
                std::cout << hour << "," << date << "," << market.ticker << "," << strike << "," << price;
                if (forecast_high) {
                    std::cout << "," << std::fixed << std::setprecision(1) << *forecast_high;
                } else {
                    std::cout << ",";
                }
                if (actual_high) {
                    std::cout << "," << std::fixed << std::setprecision(1) << *actual_high;
                } else {
                    std::cout << ",";
                }
                std::cout << "\n";
            }

            processed++;
            std::cerr << "\rProcessed " << processed << "/" << winners.size() << " winners" << std::flush;
        }
        std::cerr << "\n";
    }

    return 0;
}
