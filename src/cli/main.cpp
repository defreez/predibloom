#include <CLI/CLI.hpp>
#include "../api/kalshi_client.hpp"
#include "../core/service.hpp"
#include "formatters.hpp"
#include <iostream>

int main(int argc, char** argv) {
    CLI::App app{"predibloom - Kalshi market viewer"};

    // Global options
    std::string format = "table";

    // Subcommands
    auto* markets_cmd = app.add_subcommand("markets", "List markets");
    auto* events_cmd = app.add_subcommand("events", "List events");
    auto* orderbook_cmd = app.add_subcommand("orderbook", "Show orderbook for a market");
    auto* market_cmd = app.add_subcommand("market", "Show details for a single market");

    // Markets command options
    std::string markets_status = "open";
    std::string markets_event_ticker;
    int markets_limit = 10;
    std::string markets_format = "table";

    markets_cmd->add_option("-s,--status", markets_status, "Filter by status (open, closed, settled)")
        ->default_val("open");
    markets_cmd->add_option("-e,--event", markets_event_ticker, "Filter by event ticker");
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

    return 0;
}
