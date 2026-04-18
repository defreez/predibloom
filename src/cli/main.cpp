#include <CLI/CLI.hpp>
#include "../api/kalshi_client.hpp"
#include "../api/openmeteo_client.hpp"
#include "../api/nws_client.hpp"
#include "../core/service.hpp"
#include "../core/config.hpp"
#include "../core/weather_comparison.hpp"
#include "formatters.hpp"
#include <iostream>
#include <iomanip>
#include <map>
#include <cmath>

namespace {

// Bracket bounds: floor is inclusive, cap is exclusive
// e.g., "58 or above" -> floor=58, cap=nullopt (wins if actual >= 58)
// e.g., "77 or below" -> floor=nullopt, cap=78 (wins if actual < 78, i.e., <= 77)
// e.g., "70 to 71" -> floor=70, cap=72 (wins if actual >= 70 && actual < 72)
struct Bracket {
    const predibloom::api::Market* market = nullptr;
    std::optional<int> floor;  // inclusive lower bound
    std::optional<int> cap;    // exclusive upper bound

    // Generate display string from floor/cap - single source of truth
    std::string displayString() const {
        if (floor && cap) {
            // Range like "70-71" (cap is exclusive, so display cap-1)
            return std::to_string(*floor) + "-" + std::to_string(*cap - 1);
        } else if (floor && !cap) {
            // "58+" means 58 or above
            return std::to_string(*floor) + "+";
        } else if (!floor && cap) {
            // "77-" means 77 or below (cap is exclusive, so display cap-1)
            return std::to_string(*cap - 1) + "-";
        }
        return "?";
    }

    // Check if temperature falls in this bracket
    // Uses bracket boundaries (cap is exclusive internally, so boundary is cap-1)
    bool contains(double temp) const {
        if (floor && cap) {
            return temp >= *floor && temp <= (*cap - 1);
        } else if (floor && !cap) {
            return temp >= *floor;
        } else if (!floor && cap) {
            return temp <= (*cap - 1);
        }
        return false;
    }

    // Distance from nearest edge (for margin calculation)
    // Positive = inside bracket, negative = outside bracket
    // Cap is exclusive internally, so boundary is (cap - 1)
    double marginFrom(double temp) const {
        double dist_from_floor = floor ? (temp - *floor) : 999.0;
        double dist_from_cap = cap ? ((*cap - 1) - temp) : 999.0;
        return std::min(dist_from_floor, dist_from_cap);
    }
};

// Parse bracket from market text (subtitle or title)
Bracket parseBracket(const predibloom::api::Market& market) {
    Bracket b;
    b.market = &market;

    std::string text = market.subtitle;
    bool use_title = text.empty();
    if (use_title) {
        text = market.title;
    }

    if (use_title) {
        // Parse from title: "...be >57°...", "...be <78°...", "...be 71-72°..."
        size_t be_pos = text.find("be ");
        if (be_pos != std::string::npos) {
            std::string after_be = text.substr(be_pos + 3);
            size_t start = after_be.find_first_not_of(' ');
            if (start != std::string::npos) after_be = after_be.substr(start);

            if (!after_be.empty() && after_be[0] == '>') {
                // ">57" means strictly greater than 57, i.e., >= 58
                int temp = std::stoi(after_be.substr(1));
                b.floor = temp + 1;
            } else if (!after_be.empty() && after_be[0] == '<') {
                // "<78" means strictly less than 78, i.e., <= 77
                int temp = std::stoi(after_be.substr(1));
                b.cap = temp;
            } else if (!after_be.empty() && std::isdigit(after_be[0])) {
                size_t dash = after_be.find('-');
                if (dash != std::string::npos) {
                    int low = std::stoi(after_be.substr(0, dash));
                    int high = std::stoi(after_be.substr(dash + 1));
                    b.floor = low;
                    b.cap = high + 1;  // exclusive
                }
            }
        }
    } else {
        // Parse from subtitle: "58 or above", "77 or below", "70 to 71"
        if (text.find("or above") != std::string::npos) {
            int temp = std::stoi(text);
            b.floor = temp;
        } else if (text.find("or below") != std::string::npos) {
            int temp = std::stoi(text);
            b.cap = temp + 1;  // "77 or below" -> cap=78 (exclusive)
        } else if (text.find(" to ") != std::string::npos) {
            size_t pos = text.find(" to ");
            int low = std::stoi(text.substr(0, pos));
            int high = std::stoi(text.substr(pos + 4));
            b.floor = low;
            b.cap = high + 1;  // exclusive
        }
    }

    return b;
}

}  // namespace

int main(int argc, char** argv) {
    // Load config
    auto config = predibloom::core::Config::load();
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
    auto* backtest_cmd = app.add_subcommand("backtest", "Backtest trading strategy using forecasts");
    auto* predict_cmd = app.add_subcommand("predict", "Predict trade for a given day");
    auto* calibrate_cmd = app.add_subcommand("calibrate", "Calibrate forecast offset by comparing Open-Meteo vs NWS actuals");

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

    // Backtest command options
    std::vector<std::string> backtest_series;
    std::string backtest_start;
    std::string backtest_end;
    double backtest_margin = 0.0;      // Only trade if forecast is this far from bracket edge
    double backtest_min_price = 5.0;   // Only buy if price is at least this (cents)
    double backtest_max_price = 40.0;  // Only buy if price is below this (cents)
    double backtest_trade_size = 0.0;  // Dollars per trade (0 = use 10x margin)
    int backtest_entry_hour = 5;       // 5am UTC = 9pm PST previous day

    backtest_cmd->add_option("-s,--series", backtest_series, "Series ticker(s) (default: all configured)")
        ->delimiter(',');
    backtest_cmd->add_option("--start", backtest_start, "Start date YYYY-MM-DD")
        ->required();
    backtest_cmd->add_option("--end", backtest_end, "End date YYYY-MM-DD")
        ->required();
    backtest_cmd->add_option("--margin", backtest_margin, "Min distance from bracket edge to trade (°F)")
        ->default_val(0.0);
    backtest_cmd->add_option("--min-price", backtest_min_price, "Min price to pay (cents)")
        ->default_val(5.0);
    backtest_cmd->add_option("--max-price", backtest_max_price, "Max price to pay (cents)")
        ->default_val(40.0);
    backtest_cmd->add_option("--trade-size", backtest_trade_size, "Dollars per trade (default: $10 per °F margin)")
        ->default_val(0.0);
    backtest_cmd->add_option("--entry-hour", backtest_entry_hour, "Hour UTC on settlement day (5 = 9pm PST night before)")
        ->default_val(5)
        ->check(CLI::Range(0, 23));

    // Predict command options
    std::string predict_series;
    std::string predict_date;
    double predict_margin = 2.0;

    predict_cmd->add_option("-s,--series", predict_series, "Series ticker (optional, defaults to all configured)");
    predict_cmd->add_option("-d,--date", predict_date, "Date to predict (YYYY-MM-DD)")
        ->required();
    predict_cmd->add_option("--margin", predict_margin, "Min margin from bracket edge (°F)")
        ->default_val(2.0);

    // Calibrate command options
    std::vector<std::string> calibrate_series;
    std::string calibrate_start;
    std::string calibrate_end;

    calibrate_cmd->add_option("-s,--series", calibrate_series, "Series ticker(s) (default: all configured)")
        ->delimiter(',');
    calibrate_cmd->add_option("--start", calibrate_start, "Start date YYYY-MM-DD")
        ->required();
    calibrate_cmd->add_option("--end", calibrate_end, "End date YYYY-MM-DD")
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
        auto* series_config = config.findSeries(winners_series);
        if (!series_config || series_config->nws_station.empty()) {
            std::cerr << "Series not configured or missing weather params: " << winners_series << "\n";
            std::cerr << "Add to ~/.config/predibloom/config.json\n";
            return 1;
        }

        predibloom::api::OpenMeteoClient openmeteo;
        predibloom::api::NwsClient nws;

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

        // Fetch forecast data from Open-Meteo
        auto forecast_result = openmeteo.getHistoricalForecast(
            series_config->latitude, series_config->longitude,
            winners_start, winners_end);

        // Fetch Open-Meteo actual (ERA5 reanalysis - for comparison)
        auto openmeteo_actual_result = openmeteo.getHistoricalWeather(
            series_config->latitude, series_config->longitude,
            winners_start, winners_end);

        // Fetch actual temperatures from NWS CLI (authoritative for Kalshi settlement)
        int start_year = std::stoi(winners_start.substr(0, 4));
        int end_year = std::stoi(winners_end.substr(0, 4));
        std::map<std::string, int> nws_highs;  // date -> NWS high temp

        for (int year = start_year; year <= end_year; year++) {
            auto nws_result = nws.getCliData(series_config->nws_station, year);
            if (nws_result.ok()) {
                for (const auto& obs : nws_result.value()) {
                    if (obs.date >= winners_start && obs.date <= winners_end) {
                        nws_highs[obs.date] = obs.high;
                    }
                }
            }
        }

        std::cerr << "Fetched " << nws_highs.size() << " NWS observations\n";

        // Output CSV header
        std::cout << "timestamp,date,ticker,strike,price_cents,forecast_high,openmeteo_actual,nws_high\n";

        int processed = 0;
        for (const auto& [date, market] : winners) {
            // Get forecast and actuals
            std::optional<double> forecast_high;
            std::optional<double> openmeteo_actual;
            std::optional<int> nws_high;

            if (forecast_result.ok()) {
                forecast_high = predibloom::api::getTemperatureForDate(forecast_result.value(), date);
            }
            if (openmeteo_actual_result.ok()) {
                openmeteo_actual = predibloom::api::getTemperatureForDate(openmeteo_actual_result.value(), date);
            }
            if (nws_highs.count(date)) {
                nws_high = nws_highs.at(date);
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
                if (openmeteo_actual) {
                    std::cout << "," << std::fixed << std::setprecision(1) << *openmeteo_actual;
                } else {
                    std::cout << ",";
                }
                if (nws_high) {
                    std::cout << "," << *nws_high;
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

    // Handle backtest command
    if (*backtest_cmd) {
        // Default to all configured series with weather params
        if (backtest_series.empty()) {
            for (const auto& tab : config.tabs) {
                for (const auto& sc : tab.series) {
                    if (sc.latitude != 0 && !sc.nws_station.empty()) {
                        backtest_series.push_back(sc.series_ticker);
                    }
                }
            }
            if (backtest_series.empty()) {
                std::cerr << "No series with weather params configured\n";
                return 1;
            }
        }

        // Validate all series first
        for (const auto& series : backtest_series) {
            auto* sc = config.findSeries(series);
            if (!sc || sc->nws_station.empty()) {
                std::cerr << "Series not configured or missing weather params: " << series << "\n";
                std::cerr << "Add to ~/.config/predibloom/config.json\n";
                return 1;
            }
        }

        predibloom::api::OpenMeteoClient openmeteo;
        predibloom::api::NwsClient nws;
        client.setCaching(true);
        openmeteo.setCaching(true);
        nws.setCaching(true);

        std::cerr << "Backtest parameters:\n";
        std::cerr << "  Series: ";
        for (size_t i = 0; i < backtest_series.size(); i++) {
            if (i > 0) std::cerr << ", ";
            std::cerr << backtest_series[i];
        }
        std::cerr << "\n";
        std::cerr << "  Offset: per-series from config\n";
        std::cerr << "  Margin: " << backtest_margin << "°F (min distance from bracket edge)\n";
        std::cerr << "  Price range: " << backtest_min_price << "¢ - " << backtest_max_price << "¢\n";
        int pt_hour = (backtest_entry_hour - 7 + 24) % 24;  // UTC to PT (PDT)
        std::string pt_ampm = (pt_hour >= 12) ? "pm" : "am";
        int pt_hour_12 = (pt_hour % 12 == 0) ? 12 : (pt_hour % 12);
        std::string day_note = (backtest_entry_hour < 7) ? " (previous day)" : "";
        std::cerr << "  Entry: " << backtest_entry_hour << ":00 UTC = " << pt_hour_12 << pt_ampm << " PT" << day_note << "\n";
        std::cerr << "  Exit: hold to settlement\n";
        if (backtest_trade_size > 0) {
            std::cerr << "  Trade size: $" << backtest_trade_size << " per trade\n\n";
        } else {
            std::cerr << "  Trade size: $10 per °F margin\n\n";
        }

        // Backtest results (moved before loop)
        struct Trade {
            std::string series;
            std::string date;
            std::string ticker;
            std::string strike;
            double forecast;
            double adjusted;
            std::string entry_time;
            double entry_price;   // cents per contract
            int contracts;        // number of contracts
            int nws_actual;
            bool won;
            bool is_bounded;
            double pnl;           // total P&L in dollars
        };
        std::vector<Trade> trades;
        double total_pnl_dollars = 0;
        double total_deployed = 0;
        int wins = 0, losses = 0;

        // Categorized skip reasons for diagnostics
        struct SkipReasons {
            int no_forecast = 0;
            int no_nws_data = 0;
            int between_brackets = 0;
            int margin_too_small = 0;
            int no_trades_at_entry = 0;
            int price_too_high = 0;
            int price_too_low = 0;
            int trade_fetch_error = 0;
            int total() const {
                return no_forecast + no_nws_data + between_brackets +
                       margin_too_small + no_trades_at_entry +
                       price_too_high + price_too_low + trade_fetch_error;
            }
        } skip;

        // Process each series
        for (const auto& current_series : backtest_series) {
            auto* series_config = config.findSeries(current_series);

            // Get all markets for this series
            predibloom::api::GetMarketsParams params;
            params.series_ticker = current_series;

        auto markets_result = client.getAllMarkets(params);
        if (!markets_result.ok()) {
            std::cerr << "Error fetching markets: " << markets_result.error().message << "\n";
            return 1;
        }

        // Group markets by date (parse from event_ticker like KXHIGHNY-26APR10)
        std::map<std::string, std::vector<predibloom::api::Market>> markets_by_date;
        static const std::map<std::string, std::string> month_to_num = {
            {"JAN", "01"}, {"FEB", "02"}, {"MAR", "03"}, {"APR", "04"},
            {"MAY", "05"}, {"JUN", "06"}, {"JUL", "07"}, {"AUG", "08"},
            {"SEP", "09"}, {"OCT", "10"}, {"NOV", "11"}, {"DEC", "12"}
        };

        for (const auto& market : markets_result.value()) {
            // Settled markets have status "finalized" and a result
            if (market.result.empty()) continue;

            // Parse date from event_ticker (e.g., KXHIGHNY-26APR10)
            std::string et = market.event_ticker;
            if (et.size() < 15) continue;  // KXHIGHNY-YYMMMDD = 15 chars min

            size_t dash = et.rfind('-');
            if (dash == std::string::npos || dash + 7 > et.size()) continue;

            std::string yy = et.substr(dash + 1, 2);
            std::string mmm = et.substr(dash + 3, 3);
            std::string dd = et.substr(dash + 6, 2);

            auto it = month_to_num.find(mmm);
            if (it == month_to_num.end()) continue;

            std::string date = "20" + yy + "-" + it->second + "-" + dd;
            if (date < backtest_start || date > backtest_end) continue;

            markets_by_date[date].push_back(market);
        }

            std::cerr << current_series << ": " << markets_by_date.size() << " trading days\n";

            // Fetch forecast data
            auto forecast_result = openmeteo.getHistoricalForecast(
                series_config->latitude, series_config->longitude,
                backtest_start, backtest_end);

            if (!forecast_result.ok()) {
                std::cerr << "Error fetching forecasts for " << current_series << ": " << forecast_result.error().message << "\n";
                continue;
            }

            // Fetch NWS actual data
            int start_year = std::stoi(backtest_start.substr(0, 4));
            int end_year = std::stoi(backtest_end.substr(0, 4));
            std::map<std::string, int> nws_highs;

            for (int year = start_year; year <= end_year; year++) {
                auto nws_result = nws.getCliData(series_config->nws_station, year);
                if (nws_result.ok()) {
                    for (const auto& obs : nws_result.value()) {
                        if (obs.date >= backtest_start && obs.date <= backtest_end) {
                            nws_highs[obs.date] = obs.high;
                        }
                    }
                } else {
                    std::cerr << "WARNING: NWS data fetch failed for " << series_config->nws_station
                              << " year " << year << ": " << nws_result.error().message << "\n";
                }
            }

            if (nws_highs.empty()) {
                std::cerr << "ERROR: No NWS data for " << series_config->nws_station
                          << " in range " << backtest_start << " to " << backtest_end << "\n";
                std::cerr << "  All " << markets_by_date.size() << " trading days will be skipped\n";
                skip.no_nws_data += markets_by_date.size();
                continue;
            }

        // Process each day
        for (const auto& [date, day_markets] : markets_by_date) {
            // Get forecast for this date
            auto forecast_opt = predibloom::api::getTemperatureForDate(forecast_result.value(), date);
            if (!forecast_opt) {
                skip.no_forecast++;
                continue;
            }

            double forecast = *forecast_opt;
            double adjusted = forecast + series_config->offset;

            // Get NWS actual
            if (nws_highs.find(date) == nws_highs.end()) {
                skip.no_nws_data++;
                continue;
            }
            int nws_actual = nws_highs.at(date);

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

            const predibloom::api::Market* target_market = target ? target->market : nullptr;

            if (!target_market) {
                skip.between_brackets++;
                continue;
            }

            // Check margin requirement
            if (margin_from_edge < backtest_margin) {
                skip.margin_too_small++;
                continue;
            }

            // Get entry price at specific hour on settlement day
            auto trades_result = client.getAllTrades(target_market->ticker);
            if (!trades_result.ok()) {
                skip.trade_fetch_error++;
                continue;
            }

            // Build target hour prefix: "YYYY-MM-DDTHH"
            char hour_buf[3];
            snprintf(hour_buf, sizeof(hour_buf), "%02d", backtest_entry_hour);
            std::string target_hour = date + "T" + hour_buf;

            // Find the most recent trade at or before the target hour
            // (trades may not be sorted, so we find the max timestamp <= target)
            double entry_price = -1;
            std::string entry_time;
            for (const auto& trade : trades_result.value()) {
                std::string trade_hour = trade.created_time.substr(0, 13);
                // Use trades up to and including target hour
                if (trade_hour <= target_hour) {
                    // Take the trade with highest timestamp (most recent)
                    if (entry_time.empty() || trade_hour > entry_time) {
                        entry_price = trade.yes_price_cents();
                        entry_time = trade_hour;
                    }
                }
            }

            if (entry_price < 0) {
                skip.no_trades_at_entry++;
                continue;
            }

            // Check if price is within our range
            if (entry_price > backtest_max_price) {
                skip.price_too_high++;
                continue;
            }
            if (entry_price < backtest_min_price) {
                skip.price_too_low++;
                continue;
            }

            // Determine if bracket is bounded (has both floor and cap)
            bool is_bounded = target->floor.has_value() && target->cap.has_value();

            // Determine if we won (for settlement-based P&L)
            bool won = (target_market->result == "yes");

            // Calculate P&L - hold to settlement
            double pnl = won ? (100 - entry_price) : (-entry_price);

            // Calculate trade size: fixed or 10x margin
            double trade_size = (backtest_trade_size > 0) ? backtest_trade_size : (10.0 * margin_from_edge);

            // Calculate contracts from trade size
            // $10 at 25¢/contract = 1000¢ / 25¢ = 40 contracts
            int contracts = static_cast<int>((trade_size * 100) / entry_price);
            if (contracts < 1) contracts = 1;

            // Convert P&L from cents/contract to dollars total
            double pnl_dollars = (pnl * contracts) / 100.0;

            // Use parsed strike string
            std::string strike = target->displayString();

            Trade t{current_series, date, target_market->ticker, strike, forecast, adjusted, entry_time, entry_price, contracts, nws_actual, won, is_bounded, pnl_dollars};
            trades.push_back(t);

            total_pnl_dollars += pnl_dollars;
            total_deployed += trade_size;
            if (won) wins++; else losses++;
        }
        } // end series loop

        // Output results
        std::cout << "\n=== BACKTEST RESULTS ===\n\n";

        bool show_series = (backtest_series.size() > 1);

        if (show_series) {
            std::cout << std::left << std::setw(12) << "Series";
        }
        std::cout << std::left << std::setw(12) << "Date"
                  << std::setw(10) << "Strike"
                  << std::setw(6) << "NWS"
                  << std::setw(7) << "Entry"
                  << std::setw(6) << "Ctrs"
                  << std::setw(7) << "Result"
                  << std::setw(10) << "P&L"
                  << "\n";

        int line_width = 63 + (show_series ? 12 : 0);
        std::cout << std::string(line_width, '-') << "\n";

        for (const auto& t : trades) {
            if (show_series) {
                std::cout << std::left << std::setw(12) << t.series;
            }
            std::cout << std::left << std::setw(12) << t.date
                      << std::setw(10) << t.strike
                      << std::setw(6) << t.nws_actual
                      << std::setw(7) << (std::to_string(static_cast<int>(t.entry_price)) + "c")
                      << std::setw(6) << t.contracts
                      << std::setw(7) << (t.won ? "WIN" : "LOSS")
                      << std::showpos << std::fixed << std::setprecision(2) << "$" << t.pnl << std::noshowpos
                      << "\n";
        }

        std::cout << std::string(line_width, '-') << "\n";

        int bounded_count = 0, unbounded_count = 0;
        double bounded_pnl = 0, unbounded_pnl = 0;

        // Per-series stats
        std::map<std::string, double> series_pnl;
        std::map<std::string, int> series_trades;

        for (const auto& t : trades) {
            series_pnl[t.series] += t.pnl;
            series_trades[t.series]++;
            if (t.is_bounded) {
                bounded_count++;
                bounded_pnl += t.pnl;
            } else {
                unbounded_count++;
                unbounded_pnl += t.pnl;
            }
        }

        std::cout << "\nSummary:\n";
        std::cout << "  Trades: " << trades.size() << " (" << wins << " wins, " << losses << " losses)\n";
        std::cout << "  Win rate: " << std::fixed << std::setprecision(1)
                  << (100.0 * wins / (wins + losses)) << "%\n";

        // Per-series breakdown
        if (backtest_series.size() > 1) {
            for (const auto& [series, pnl] : series_pnl) {
                std::cout << "  " << series << ": " << series_trades[series] << " trades, "
                          << std::showpos << std::fixed << std::setprecision(2) << "$" << pnl << std::noshowpos << "\n";
            }
        }

        if (bounded_count > 0 || unbounded_count > 0) {
            std::cout << "  Bounded: " << bounded_count << " trades, "
                      << std::showpos << std::fixed << std::setprecision(2) << "$" << bounded_pnl << std::noshowpos << "\n";
            std::cout << "  Unbounded: " << unbounded_count << " trades, "
                      << std::showpos << std::fixed << std::setprecision(2) << "$" << unbounded_pnl << std::noshowpos << "\n";
        }

        std::cout << "  Skipped: " << skip.total() << " days\n";
        if (skip.total() > 0) {
            if (skip.no_nws_data > 0)
                std::cout << "    no NWS data:        " << skip.no_nws_data << "\n";
            if (skip.no_forecast > 0)
                std::cout << "    no forecast:        " << skip.no_forecast << "\n";
            if (skip.between_brackets > 0)
                std::cout << "    between brackets:   " << skip.between_brackets << "\n";
            if (skip.margin_too_small > 0)
                std::cout << "    margin too small:   " << skip.margin_too_small << "\n";
            if (skip.no_trades_at_entry > 0)
                std::cout << "    no trades at entry: " << skip.no_trades_at_entry << "\n";
            if (skip.price_too_high > 0)
                std::cout << "    price too high:     " << skip.price_too_high << "\n";
            if (skip.price_too_low > 0)
                std::cout << "    price too low:      " << skip.price_too_low << "\n";
            if (skip.trade_fetch_error > 0)
                std::cout << "    trade fetch error:  " << skip.trade_fetch_error << "\n";
        }
        std::cout << "  Deployed: $" << std::fixed << std::setprecision(2) << total_deployed << "\n";
        std::cout << "  Total P&L: " << std::showpos << "$" << total_pnl_dollars << std::noshowpos << "\n";

        double roi = total_deployed > 0 ? (total_pnl_dollars / total_deployed * 100) : 0;
        std::cout << "  ROI: " << std::showpos << std::setprecision(1) << roi << "%" << std::noshowpos << "\n";
    }

    // Handle predict command
    if (*predict_cmd) {
        predibloom::api::OpenMeteoClient openmeteo;

        // Build list of series to process
        std::vector<const predibloom::core::TrackedSeries*> series_list;
        if (!predict_series.empty()) {
            auto* sc = config.findSeries(predict_series);
            if (!sc || sc->latitude == 0) {
                std::cerr << "Series not configured or missing weather params: " << predict_series << "\n";
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

        // Date parsing for event ticker matching
        std::string yy = predict_date.substr(2, 2);
        std::string mm = predict_date.substr(5, 2);
        std::string dd = predict_date.substr(8, 2);
        static const std::map<std::string, std::string> month_map = {
            {"01", "JAN"}, {"02", "FEB"}, {"03", "MAR"}, {"04", "APR"},
            {"05", "MAY"}, {"06", "JUN"}, {"07", "JUL"}, {"08", "AUG"},
            {"09", "SEP"}, {"10", "OCT"}, {"11", "NOV"}, {"12", "DEC"}
        };

        // Collect predictions for all series
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
            bool between_brackets;  // forecast doesn't fall into any bracket
        };
        std::vector<Prediction> predictions;

        std::cerr << "Fetching forecasts for " << series_list.size() << " series...\n";

        for (const auto* series_config : series_list) {
            double effective_offset = series_config->offset;

            // Get forecast
            auto forecast_result = openmeteo.getHistoricalForecast(
                series_config->latitude, series_config->longitude,
                predict_date, predict_date);

            if (!forecast_result.ok()) {
                std::cerr << "  " << series_config->label << ": forecast error\n";
                continue;
            }

            auto forecast_opt = predibloom::api::getTemperatureForDate(forecast_result.value(), predict_date);
            if (!forecast_opt) {
                std::cerr << "  " << series_config->label << ": no forecast data\n";
                continue;
            }

            double forecast = *forecast_opt;
            double adjusted = forecast + effective_offset;

            // Get markets
            predibloom::api::GetMarketsParams params;
            params.series_ticker = series_config->series_ticker;
            auto markets_result = client.getAllMarkets(params);
            if (!markets_result.ok()) {
                std::cerr << "  " << series_config->label << ": market error\n";
                continue;
            }

            std::string expected_event = series_config->series_ticker + "-" + yy + month_map.at(mm) + dd;

            // Find markets for this date
            std::vector<predibloom::api::Market> day_markets;
            for (const auto& market : markets_result.value()) {
                if (market.event_ticker == expected_event) {
                    day_markets.push_back(market);
                }
            }

            if (day_markets.empty()) {
                std::cerr << "  " << series_config->label << ": no markets for date\n";
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
                p.tradeable = (margin_from_edge >= predict_margin);
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

        // Output results
        std::cout << "\n=== PREDICTIONS FOR " << predict_date << " ===\n\n";

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
                } else {
                    std::cout << "-";
                }
            }
            std::cout << "\n";
        }

        std::cout << std::string(78, '-') << "\n";
        std::cout << "Tradeable signals: " << tradeable_count << "/" << predictions.size()
                  << " (margin >= " << predict_margin << "°F)\n";

        if (tradeable_count > 0) {
            std::cout << "\nTickers to buy:\n";
            for (const auto& p : predictions) {
                if (p.tradeable) {
                    std::cout << "  " << p.ticker << " (" << p.label << " " << p.strike << " @ " << (int)p.ask << "¢)\n";
                }
            }
        }
    }

    // Handle calibrate command
    if (*calibrate_cmd) {
        // Default to all configured series with weather params
        if (calibrate_series.empty()) {
            for (const auto& tab : config.tabs) {
                for (const auto& sc : tab.series) {
                    if (sc.latitude != 0 && !sc.nws_station.empty()) {
                        calibrate_series.push_back(sc.series_ticker);
                    }
                }
            }
            if (calibrate_series.empty()) {
                std::cerr << "No series with weather params configured\n";
                return 1;
            }
        }

        // Validate all series
        for (const auto& series : calibrate_series) {
            auto* sc = config.findSeries(series);
            if (!sc || sc->nws_station.empty()) {
                std::cerr << "Series not configured or missing weather params: " << series << "\n";
                return 1;
            }
        }

        predibloom::api::OpenMeteoClient openmeteo;
        predibloom::api::NwsClient nws;
        openmeteo.setCaching(true);
        nws.setCaching(true);

        struct SeriesSummary {
            std::string label;
            double current_offset;
            int count;
            double mean_offset;
            double mae;
            double rmse;
        };
        std::vector<SeriesSummary> summaries;

        for (const auto& current_series : calibrate_series) {
            auto* series_config = config.findSeries(current_series);

            std::cerr << "Calibrating " << series_config->label
                      << " (" << current_series << ", station " << series_config->nws_station << ")...\n";

            // Fetch Open-Meteo historical forecasts
            auto forecast_result = openmeteo.getHistoricalForecast(
                series_config->latitude, series_config->longitude,
                calibrate_start, calibrate_end);

            if (!forecast_result.ok()) {
                std::cerr << "  Error fetching forecasts: " << forecast_result.error().message << "\n";
                continue;
            }

            // Fetch NWS actual data
            int start_year = std::stoi(calibrate_start.substr(0, 4));
            int end_year = std::stoi(calibrate_end.substr(0, 4));
            std::map<std::string, int> nws_highs;

            for (int year = start_year; year <= end_year; year++) {
                auto nws_result = nws.getCliData(series_config->nws_station, year);
                if (nws_result.ok()) {
                    for (const auto& obs : nws_result.value()) {
                        if (obs.date >= calibrate_start && obs.date <= calibrate_end) {
                            nws_highs[obs.date] = obs.high;
                        }
                    }
                } else {
                    std::cerr << "  WARNING: NWS data fetch failed for year " << year
                              << ": " << nws_result.error().message << "\n";
                }
            }

            if (nws_highs.empty()) {
                std::cerr << "  ERROR: No NWS data available, skipping\n";
                continue;
            }

            // Build per-day comparisons
            struct DayComparison {
                std::string date;
                double forecast;
                int nws_actual;
                double error;  // NWS - OpenMeteo
            };
            std::vector<DayComparison> days;

            const auto& times = forecast_result.value().daily.time;
            const auto& temps = forecast_result.value().daily.temperature_2m_max;

            for (size_t i = 0; i < times.size() && i < temps.size(); i++) {
                const std::string& date = times[i];
                double forecast_temp = temps[i];

                if (std::isnan(forecast_temp)) continue;
                if (date < calibrate_start || date > calibrate_end) continue;

                auto nws_it = nws_highs.find(date);
                if (nws_it == nws_highs.end()) continue;

                DayComparison d;
                d.date = date;
                d.forecast = forecast_temp;
                d.nws_actual = nws_it->second;
                d.error = static_cast<double>(d.nws_actual) - d.forecast;
                days.push_back(d);
            }

            if (days.empty()) {
                std::cerr << "  No matching days found\n";
                continue;
            }

            // Compute stats
            double sum_error = 0, sum_abs_error = 0, sum_sq_error = 0;
            for (const auto& d : days) {
                sum_error += d.error;
                sum_abs_error += std::abs(d.error);
                sum_sq_error += d.error * d.error;
            }
            int n = static_cast<int>(days.size());
            double mean_offset = sum_error / n;
            double mae = sum_abs_error / n;
            double rmse = std::sqrt(sum_sq_error / n);

            // Print per-day table
            std::cout << "\n=== " << series_config->label << " (" << current_series
                      << ", station " << series_config->nws_station << ") ===\n\n";

            std::cout << std::left << std::setw(12) << "Date"
                      << std::right << std::setw(10) << "Forecast"
                      << std::setw(10) << "NWS"
                      << std::setw(10) << "Error"
                      << "\n";
            std::cout << std::string(42, '-') << "\n";

            for (const auto& d : days) {
                char forecast_buf[16], error_buf[16];
                snprintf(forecast_buf, sizeof(forecast_buf), "%.1f", d.forecast);
                snprintf(error_buf, sizeof(error_buf), "%+.1f", d.error);

                std::cout << std::left << std::setw(12) << d.date
                          << std::right << std::setw(10) << forecast_buf
                          << std::setw(10) << d.nws_actual
                          << std::setw(10) << error_buf
                          << "\n";
            }

            std::cout << std::string(42, '-') << "\n";
            char offset_str[16];
            snprintf(offset_str, sizeof(offset_str), "%+.1f", mean_offset);
            char current_str[16];
            snprintf(current_str, sizeof(current_str), "%+.1f", series_config->offset);

            std::cout << "\n  Days compared: " << n << "\n";
            std::cout << "  Mean offset (recommended): " << offset_str << " F\n";
            std::cout << "  MAE:  " << std::fixed << std::setprecision(1) << mae << " F\n";
            std::cout << "  RMSE: " << std::fixed << std::setprecision(1) << rmse << " F\n";
            std::cout << "  Current config offset: " << current_str << " F\n";

            summaries.push_back({
                series_config->label,
                series_config->offset,
                n,
                mean_offset,
                mae,
                rmse
            });
        }

        // Multi-series summary table
        if (summaries.size() > 1) {
            std::cout << "\n\n=== CALIBRATION SUMMARY ===\n\n";

            std::cout << std::left << std::setw(18) << "City"
                      << std::right << std::setw(6) << "Days"
                      << std::setw(10) << "Offset"
                      << std::setw(8) << "MAE"
                      << std::setw(8) << "RMSE"
                      << std::setw(10) << "Current"
                      << std::setw(8) << "Delta"
                      << "\n";
            std::cout << std::string(68, '-') << "\n";

            for (const auto& s : summaries) {
                double delta = s.mean_offset - s.current_offset;
                char offset_buf[16], mae_buf[16], rmse_buf[16], current_buf[16], delta_buf[16];
                snprintf(offset_buf, sizeof(offset_buf), "%+.1f", s.mean_offset);
                snprintf(mae_buf, sizeof(mae_buf), "%.1f", s.mae);
                snprintf(rmse_buf, sizeof(rmse_buf), "%.1f", s.rmse);
                snprintf(current_buf, sizeof(current_buf), "%+.1f", s.current_offset);
                snprintf(delta_buf, sizeof(delta_buf), "%+.1f", delta);

                std::cout << std::left << std::setw(18) << s.label
                          << std::right << std::setw(6) << s.count
                          << std::setw(10) << offset_buf
                          << std::setw(8) << mae_buf
                          << std::setw(8) << rmse_buf
                          << std::setw(10) << current_buf
                          << std::setw(8) << delta_buf
                          << "\n";
            }

            std::cout << std::string(68, '-') << "\n";
            std::cout << "\nOffset = mean(NWS_actual - OpenMeteo_forecast)\n";
            std::cout << "Delta = recommended offset - current config offset\n";
        }
    }

    return 0;
}
