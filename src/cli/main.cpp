#include <CLI/CLI.hpp>
#include "../api/kalshi_client.hpp"
#include "../api/openmeteo_client.hpp"
#include "../api/nws_client.hpp"
#include "../core/service.hpp"
#include "../core/config.hpp"
#include "../core/weather_comparison.hpp"
#include "formatters.hpp"
#include "../core/time_utils.hpp"
#include <iostream>
#include <iomanip>
#include <map>
#include <algorithm>
#include <cmath>
#include <random>

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
    auto* series_cmd = app.add_subcommand("series", "List configured series with entry hours");
    auto* calibrate_cmd = app.add_subcommand("calibrate", "Calibrate forecast offset by comparing Open-Meteo vs NWS actuals");
    auto* fills_cmd = app.add_subcommand("fills", "Show your trade fills (requires auth)");
    auto* portfolio_cmd = app.add_subcommand("portfolio", "Show current positions and balance (requires auth)");
    auto* portfolio_positions_cmd = portfolio_cmd->add_subcommand("positions", "Show open positions");
    auto* portfolio_settlements_cmd = portfolio_cmd->add_subcommand("settlements", "Show recent settlements");
    int portfolio_settle_days = 7;
    portfolio_settlements_cmd->add_option("-d,--days", portfolio_settle_days, "Number of days to look back")
        ->default_val(7);

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
    int backtest_entry_hour = -1;      // -1 = use per-series config (5 high, 17 low)
    int backtest_exit_hour = -1;       // -1 = hold to settlement
    int backtest_seed = -1;            // -1 = random seed
    int backtest_jitter = 3;           // +/- hours for entry time randomization

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
    backtest_cmd->add_option("--entry-hour", backtest_entry_hour, "Hour UTC override (-1 = use per-series config)")
        ->default_val(-1)
        ->check(CLI::Range(-1, 23));
    backtest_cmd->add_option("--exit-hour", backtest_exit_hour, "Hour UTC to exit (-1 = hold to settlement)")
        ->default_val(-1)
        ->check(CLI::Range(-1, 23));
    backtest_cmd->add_option("--seed", backtest_seed, "RNG seed for jitter (-1 = random)")
        ->default_val(-1);
    backtest_cmd->add_option("--jitter", backtest_jitter, "Entry time jitter +/- hours (0 = no jitter)")
        ->default_val(3)
        ->check(CLI::Range(0, 12));

    // Predict command options
    std::string predict_series;
    std::string predict_date;
    double predict_margin = 2.0;
    double predict_max_price = 40.0;

    predict_cmd->add_option("-s,--series", predict_series, "Series ticker (optional, defaults to all configured)");
    predict_cmd->add_option("-d,--date", predict_date, "Date to predict (YYYY-MM-DD)")
        ->required();
    predict_cmd->add_option("--margin", predict_margin, "Min margin from bracket edge (°F)")
        ->default_val(2.0);
    predict_cmd->add_option("--max-price", predict_max_price, "Max price to pay (cents)")
        ->default_val(40.0);

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

    // Fills command options
    std::string fills_ticker;
    int fills_limit = 100;
    std::string fills_format = "table";

    fills_cmd->add_option("-t,--ticker", fills_ticker, "Filter by market ticker");
    fills_cmd->add_option("-n,--limit", fills_limit, "Number of results")
        ->default_val(100)
        ->check(CLI::Range(1, 1000));
    fills_cmd->add_option("-f,--format", fills_format, "Output format: table|json|csv")
        ->default_val("table")
        ->check(CLI::IsMember({"table", "json", "csv"}));

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
        auto* series_config = config.findSeries(compare_series);
        if (!series_config || series_config->latitude == 0) {
            std::cerr << "Series not configured or missing weather params: " << compare_series << "\n";
            std::cerr << "Add to ~/.config/predibloom/config.json\n";
            return 1;
        }

        predibloom::api::OpenMeteoClient openmeteo;
        predibloom::core::WeatherComparisonService comparison(client, openmeteo);
        comparison.setLocation(series_config->latitude, series_config->longitude,
                               series_config->isLowTemp());

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
            // Parse date from event_ticker (works for any series)
            std::string market_date = predibloom::core::parseDateFromEventTicker(market.event_ticker);
            if (market_date.empty()) continue;

            // Filter by date range
            if (market_date < history_start || market_date > history_end) continue;

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

        // Parse date from event_ticker (generic approach, works for any series)
        static const std::map<std::string, std::string> month_to_num_w = {
            {"JAN", "01"}, {"FEB", "02"}, {"MAR", "03"}, {"APR", "04"},
            {"MAY", "05"}, {"JUN", "06"}, {"JUL", "07"}, {"AUG", "08"},
            {"SEP", "09"}, {"OCT", "10"}, {"NOV", "11"}, {"DEC", "12"}
        };

        bool is_low = series_config->isLowTemp();

        // Group markets by date and find winners (settled YES)
        std::map<std::string, predibloom::api::Market> winners;  // date -> winning market
        for (const auto& market : markets_result.value()) {
            if (market.result != "yes") continue;

            std::string et = market.event_ticker;
            size_t dash = et.rfind('-');
            if (dash == std::string::npos || dash + 7 > et.size()) continue;

            std::string yy = et.substr(dash + 1, 2);
            std::string mmm = et.substr(dash + 3, 3);
            std::string dd = et.substr(dash + 6, 2);

            auto it = month_to_num_w.find(mmm);
            if (it == month_to_num_w.end()) continue;

            std::string date = "20" + yy + "-" + it->second + "-" + dd;
            if (date < winners_start || date > winners_end) continue;

            winners[date] = market;
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
        std::map<std::string, int> nws_temps;

        for (int year = start_year; year <= end_year; year++) {
            auto nws_result = nws.getCliData(series_config->nws_station, year);
            if (nws_result.ok()) {
                for (const auto& obs : nws_result.value()) {
                    if (obs.date >= winners_start && obs.date <= winners_end) {
                        nws_temps[obs.date] = is_low ? obs.low : obs.high;
                    }
                }
            }
        }

        std::cerr << "Fetched " << nws_temps.size() << " NWS observations\n";

        // Output CSV header
        std::cout << "timestamp,date,ticker,strike,price_cents,forecast_temp,openmeteo_actual,nws_temp\n";

        int processed = 0;
        for (const auto& [date, market] : winners) {
            // Get forecast and actuals
            std::optional<double> forecast_temp;
            std::optional<double> openmeteo_actual;
            std::optional<int> nws_temp;

            if (forecast_result.ok()) {
                forecast_temp = is_low
                    ? predibloom::api::getMinTemperatureForDate(forecast_result.value(), date)
                    : predibloom::api::getTemperatureForDate(forecast_result.value(), date);
            }
            if (openmeteo_actual_result.ok()) {
                openmeteo_actual = is_low
                    ? predibloom::api::getMinTemperatureForDate(openmeteo_actual_result.value(), date)
                    : predibloom::api::getTemperatureForDate(openmeteo_actual_result.value(), date);
            }
            if (nws_temps.count(date)) {
                nws_temp = nws_temps.at(date);
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
                if (forecast_temp) {
                    std::cout << "," << std::fixed << std::setprecision(1) << *forecast_temp;
                } else {
                    std::cout << ",";
                }
                if (openmeteo_actual) {
                    std::cout << "," << std::fixed << std::setprecision(1) << *openmeteo_actual;
                } else {
                    std::cout << ",";
                }
                if (nws_temp) {
                    std::cout << "," << *nws_temp;
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
        if (backtest_entry_hour >= 0) {
            int pt_hour = (backtest_entry_hour - 7 + 24) % 24;  // UTC to PT (PDT)
            std::string pt_ampm = (pt_hour >= 12) ? "pm" : "am";
            int pt_hour_12 = (pt_hour % 12 == 0) ? 12 : (pt_hour % 12);
            std::string day_note = (backtest_entry_hour < 7) ? " (previous day)" : "";
            std::cerr << "  Entry: " << backtest_entry_hour << ":00 UTC = " << pt_hour_12 << pt_ampm << " PT" << day_note << "\n";
        } else {
            std::cerr << "  Entry: per-series from config (high=5 UTC/9pm PT, low=17 UTC/9am PT)\n";
        }
        if (backtest_exit_hour >= 0) {
            int exit_pt = (backtest_exit_hour - 7 + 24) % 24;
            std::string exit_ampm = (exit_pt >= 12) ? "pm" : "am";
            int exit_12 = (exit_pt % 12 == 0) ? 12 : (exit_pt % 12);
            std::string exit_day_note = (backtest_exit_hour < 7) ? " (previous day)" : "";
            std::cerr << "  Exit: " << backtest_exit_hour << ":00 UTC = " << exit_12 << exit_ampm << " PT" << exit_day_note << "\n";
        } else {
            std::cerr << "  Exit: hold to settlement\n";
        }
        if (backtest_trade_size > 0) {
            std::cerr << "  Trade size: $" << backtest_trade_size << " per trade\n";
        } else {
            std::cerr << "  Trade size: $10 per °F margin\n";
        }
        if (backtest_jitter > 0) {
            std::cerr << "  Jitter: +/-" << backtest_jitter << "hr";
            if (backtest_seed >= 0) {
                std::cerr << " (seed=" << backtest_seed << ")";
            }
            std::cerr << "\n";
        }
        std::cerr << "\n";

        // Initialize RNG for entry time jitter
        std::mt19937 rng(backtest_seed >= 0
            ? static_cast<unsigned>(backtest_seed)
            : std::random_device{}());
        std::uniform_int_distribution<int> jitter_dist(-backtest_jitter, backtest_jitter);

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
            double exit_price;    // cents per contract (-1 = settlement)
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
            int no_trades_at_exit = 0;
            int total() const {
                return no_forecast + no_nws_data + between_brackets +
                       margin_too_small + no_trades_at_entry + no_trades_at_exit +
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

        // Group markets by date
        std::map<std::string, std::vector<predibloom::api::Market>> markets_by_date;

        for (const auto& market : markets_result.value()) {
            // Settled markets have status "finalized" and a result
            if (market.result.empty()) continue;

            std::string date = predibloom::core::parseDateFromEventTicker(market.event_ticker);
            if (date.empty()) continue;
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
            bool is_low = series_config->isLowTemp();
            int start_year = std::stoi(backtest_start.substr(0, 4));
            int end_year = std::stoi(backtest_end.substr(0, 4));
            std::map<std::string, int> nws_temps;

            for (int year = start_year; year <= end_year; year++) {
                auto nws_result = nws.getCliData(series_config->nws_station, year);
                if (nws_result.ok()) {
                    for (const auto& obs : nws_result.value()) {
                        if (obs.date >= backtest_start && obs.date <= backtest_end) {
                            nws_temps[obs.date] = is_low ? obs.low : obs.high;
                        }
                    }
                } else {
                    std::cerr << "WARNING: NWS data fetch failed for " << series_config->nws_station
                              << " year " << year << ": " << nws_result.error().message << "\n";
                }
            }

            if (nws_temps.empty()) {
                std::cerr << "ERROR: No NWS data for " << series_config->nws_station
                          << " in range " << backtest_start << " to " << backtest_end << "\n";
                std::cerr << "  All " << markets_by_date.size() << " trading days will be skipped\n";
                skip.no_nws_data += markets_by_date.size();
                continue;
            }

        // Process each day
        for (const auto& [date, day_markets] : markets_by_date) {
            // Get forecast for this date
            auto forecast_opt = is_low
                ? predibloom::api::getMinTemperatureForDate(forecast_result.value(), date)
                : predibloom::api::getTemperatureForDate(forecast_result.value(), date);
            if (!forecast_opt) {
                skip.no_forecast++;
                continue;
            }

            double forecast = *forecast_opt;
            double adjusted = forecast + series_config->offset;

            // Get NWS actual
            if (nws_temps.find(date) == nws_temps.end()) {
                skip.no_nws_data++;
                continue;
            }
            int nws_actual = nws_temps.at(date);

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
            int effective_entry_hour = (backtest_entry_hour >= 0)
                ? backtest_entry_hour : series_config->effectiveEntryHour();
            int delta = (backtest_jitter > 0) ? jitter_dist(rng) : 0;
            std::string target_hour = predibloom::core::computeEntryDatetimeWithJitter(
                date, series_config->entry_day_offset, effective_entry_hour, delta);

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

            // Determine exit price
            double exit_price = -1;
            bool won;
            double pnl;

            if (backtest_exit_hour >= 0) {
                // Find trade price at exit hour
                char exit_buf[3];
                snprintf(exit_buf, sizeof(exit_buf), "%02d", backtest_exit_hour);
                std::string exit_target_hour = date + "T" + exit_buf;

                std::string exit_time;
                for (const auto& trade : trades_result.value()) {
                    std::string trade_hour = trade.created_time.substr(0, 13);
                    if (trade_hour <= exit_target_hour) {
                        if (exit_time.empty() || trade_hour > exit_time) {
                            exit_price = trade.yes_price_cents();
                            exit_time = trade_hour;
                        }
                    }
                }

                if (exit_price < 0) {
                    skip.no_trades_at_exit++;
                    continue;
                }

                pnl = exit_price - entry_price;
                won = (pnl > 0);
            } else {
                // Hold to settlement
                won = (target_market->result == "yes");
                pnl = won ? (100 - entry_price) : (-entry_price);
            }

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

            Trade t{current_series, date, target_market->ticker, strike, forecast, adjusted, entry_time, entry_price, exit_price, contracts, nws_actual, won, is_bounded, pnl_dollars};
            trades.push_back(t);

            total_pnl_dollars += pnl_dollars;
            total_deployed += trade_size;
            if (won) wins++; else losses++;
        }
        } // end series loop

        // Sort by date, then series
        std::sort(trades.begin(), trades.end(), [](const Trade& a, const Trade& b) {
            if (a.date != b.date) return a.date < b.date;
            return a.series < b.series;
        });

        // Output results
        std::cout << "\n=== BACKTEST RESULTS ===\n\n";

        bool show_series = (backtest_series.size() > 1);

        bool show_exit = (backtest_exit_hour >= 0);

        if (show_series) {
            std::cout << std::left << std::setw(12) << "Series";
        }
        std::cout << std::left << std::setw(12) << "Date"
                  << std::setw(10) << "Strike"
                  << std::setw(6) << "NWS"
                  << std::setw(9) << "EntryT"
                  << std::setw(7) << "Entry";
        if (show_exit) {
            std::cout << std::setw(7) << "Exit";
        }
        std::cout << std::setw(6) << "Ctrs"
                  << std::setw(7) << "Result"
                  << std::setw(10) << "P&L"
                  << std::setw(10) << "Balance"
                  << "\n";

        int line_width = 82 + (show_series ? 12 : 0) + (show_exit ? 7 : 0);
        std::cout << std::string(line_width, '-') << "\n";

        double running_balance = 0;
        for (const auto& t : trades) {
            running_balance += t.pnl;
            if (show_series) {
                std::cout << std::left << std::setw(12) << t.series;
            }

            // Convert entry time to PT for display (e.g., "2026-02-17T05" -> "9pm")
            std::string entry_pt_str = "-";
            if (t.entry_time.size() >= 13) {
                int utc_hour = std::stoi(t.entry_time.substr(11, 2));
                int pt_hour = (utc_hour - 7 + 24) % 24;
                int pt12 = (pt_hour % 12 == 0) ? 12 : (pt_hour % 12);
                std::string ampm = (pt_hour >= 12) ? "pm" : "am";
                entry_pt_str = std::to_string(pt12) + ampm;
            }

            std::cout << std::left << std::setw(12) << t.date
                      << std::setw(10) << t.strike
                      << std::setw(6) << t.nws_actual
                      << std::setw(9) << entry_pt_str
                      << std::setw(7) << (std::to_string(static_cast<int>(t.entry_price)) + "c");
            if (show_exit) {
                std::cout << std::setw(7) << (std::to_string(static_cast<int>(t.exit_price)) + "c");
            }
            char pnl_buf[16], bal_buf[16];
            snprintf(pnl_buf, sizeof(pnl_buf), "%+.2f", t.pnl);
            snprintf(bal_buf, sizeof(bal_buf), "%+.0f", running_balance);
            std::cout << std::setw(6) << t.contracts
                      << std::setw(7) << (t.won ? "WIN" : "LOSS")
                      << std::left << std::setw(12) << (std::string("$") + pnl_buf)
                      << "$" << bal_buf
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
            if (skip.no_trades_at_exit > 0)
                std::cout << "    no trades at exit:  " << skip.no_trades_at_exit << "\n";
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
            bool within_window;     // current time is within entry window
            std::string entry_pt;   // entry time in PT (e.g. "5pm")
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

            auto forecast_opt = series_config->isLowTemp()
                ? predibloom::api::getMinTemperatureForDate(forecast_result.value(), predict_date)
                : predibloom::api::getTemperatureForDate(forecast_result.value(), predict_date);
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
                p.tradeable = (margin_from_edge >= predict_margin) && (p.ask <= predict_max_price);
            } else {
                p.strike = "---";
                p.ticker = "";
                p.margin = 0;
                p.bid = 0;
                p.ask = 0;
                p.tradeable = false;
            }

            // Check if current time is within entry window
            auto entry_dt = predibloom::core::computeEntryDatetime(
                predict_date, series_config->entry_day_offset,
                series_config->effectiveEntryHour());
            p.within_window = predibloom::core::isWithinEntryWindow(entry_dt, 3);

            // Format entry time in PT
            int utc = series_config->effectiveEntryHour();
            int pt = (utc - 7 + 24) % 24;
            int pt12 = (pt % 12 == 0) ? 12 : (pt % 12);
            std::string ampm = (pt >= 12) ? "pm" : "am";
            p.entry_pt = std::to_string(pt12) + ampm;

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
                  << std::setw(8) << "Entry"
                  << "Signal\n";
        std::cout << std::string(86, '-') << "\n";

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
                          << std::setw(8) << p.entry_pt
                          << "BETWEEN";
            } else {
                snprintf(margin_buf, sizeof(margin_buf), "%.1f°F", p.margin);
                std::cout << std::setw(8) << margin_buf
                          << std::setw(8) << (std::to_string((int)p.bid) + "¢")
                          << std::setw(8) << (std::to_string((int)p.ask) + "¢")
                          << std::setw(8) << p.entry_pt;
                if (p.tradeable && p.within_window) {
                    std::cout << "BUY";
                    tradeable_count++;
                } else if (p.tradeable && !p.within_window) {
                    std::cout << "WAIT";
                } else if (p.margin >= predict_margin && p.ask > predict_max_price) {
                    std::cout << "EXPENSIVE";
                } else {
                    std::cout << "-";
                }
            }
            std::cout << "\n";
        }

        std::cout << std::string(86, '-') << "\n";
        std::cout << "Tradeable signals: " << tradeable_count << "/" << predictions.size()
                  << " (margin >= " << predict_margin << "°F, ask <= " << (int)predict_max_price << "¢)\n";

        if (tradeable_count > 0) {
            std::cout << "\nTickers to buy:\n";
            for (const auto& p : predictions) {
                if (p.tradeable && p.within_window) {
                    std::cout << "  " << p.ticker << "  " << p.label << " " << p.strike
                              << " @ " << (int)p.ask << "¢\n";
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
            bool is_low = series_config->isLowTemp();
            int start_year = std::stoi(calibrate_start.substr(0, 4));
            int end_year = std::stoi(calibrate_end.substr(0, 4));
            std::map<std::string, int> nws_temps;

            for (int year = start_year; year <= end_year; year++) {
                auto nws_result = nws.getCliData(series_config->nws_station, year);
                if (nws_result.ok()) {
                    for (const auto& obs : nws_result.value()) {
                        if (obs.date >= calibrate_start && obs.date <= calibrate_end) {
                            nws_temps[obs.date] = is_low ? obs.low : obs.high;
                        }
                    }
                } else {
                    std::cerr << "  WARNING: NWS data fetch failed for year " << year
                              << ": " << nws_result.error().message << "\n";
                }
            }

            if (nws_temps.empty()) {
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
            const auto& temps = is_low
                ? forecast_result.value().daily.temperature_2m_min
                : forecast_result.value().daily.temperature_2m_max;

            for (size_t i = 0; i < times.size() && i < temps.size(); i++) {
                const std::string& date = times[i];
                double forecast_temp = temps[i];

                if (std::isnan(forecast_temp)) continue;
                if (date < calibrate_start || date > calibrate_end) continue;

                auto nws_it = nws_temps.find(date);
                if (nws_it == nws_temps.end()) continue;

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

    // Handle fills command
    if (*fills_cmd) {
        if (!config.hasAuth()) {
            std::cerr << "Error: Authentication not configured.\n";
            std::cerr << "Add api_key_id and key_file to ~/.config/predibloom/auth.json\n";
            return 1;
        }

        client.setAuth(config.api_key_id, config.key_file);

        predibloom::api::GetFillsParams params;
        if (!fills_ticker.empty()) params.ticker = fills_ticker;
        params.limit = fills_limit;

        auto result = client.getFills(params);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error().message << "\n";
            return 1;
        }

        const auto& fills = result.value().fills;

        if (fills_format == "json") {
            nlohmann::json j = nlohmann::json::array();
            for (const auto& f : fills) {
                j.push_back({
                    {"fill_id", f.fill_id},
                    {"ticker", f.ticker},
                    {"side", f.side},
                    {"action", f.action},
                    {"count", f.count()},
                    {"yes_price_cents", f.yes_price_cents()},
                    {"is_taker", f.is_taker},
                    {"created_time", f.created_time}
                });
            }
            std::cout << j.dump(2) << "\n";
        } else if (fills_format == "csv") {
            std::cout << "time,ticker,side,action,qty,price_cents,taker\n";
            for (const auto& f : fills) {
                std::cout << f.created_time << ","
                          << f.ticker << ","
                          << f.side << ","
                          << f.action << ","
                          << f.count() << ","
                          << static_cast<int>(f.yes_price_cents()) << ","
                          << (f.is_taker ? "yes" : "no") << "\n";
            }
        } else {
            // Table format
            std::cout << std::left
                      << std::setw(22) << "Time"
                      << std::setw(28) << "Ticker"
                      << std::setw(6) << "Side"
                      << std::setw(6) << "Act"
                      << std::right
                      << std::setw(5) << "Qty"
                      << std::setw(8) << "Price"
                      << "  " << "Taker\n";
            std::cout << std::string(79, '-') << "\n";

            for (const auto& f : fills) {
                std::string ticker_display = f.ticker;
                if (ticker_display.size() > 26) {
                    ticker_display = ticker_display.substr(0, 26);
                }

                std::cout << std::left
                          << std::setw(22) << f.created_time.substr(0, 19)
                          << std::setw(28) << ticker_display
                          << std::setw(6) << f.side
                          << std::setw(6) << f.action
                          << std::right
                          << std::setw(5) << f.count()
                          << std::setw(7) << static_cast<int>(f.yes_price_cents()) << "c"
                          << "  " << (f.is_taker ? "yes" : "no") << "\n";
            }

            std::cout << std::string(79, '-') << "\n";
            std::cout << fills.size() << " fills\n";
        }
    }

    // Handle portfolio command
    if (*portfolio_cmd) {
        if (!config.hasAuth()) {
            std::cerr << "Error: Authentication not configured.\n";
            std::cerr << "Add api_key_id and key_file to ~/.config/predibloom/auth.json\n";
            return 1;
        }

        client.setAuth(config.api_key_id, config.key_file);

        // Fetch balance (needed for all subcommands)
        auto balance_result = client.getBalance();
        if (!balance_result.ok()) {
            std::cerr << "Error fetching balance: " << balance_result.error().message << "\n";
            return 1;
        }
        const auto& bal = balance_result.value();

        if (*portfolio_positions_cmd) {
            // portfolio positions — detailed position list
            auto positions_result = client.getAllPositions();
            if (!positions_result.ok()) {
                std::cerr << "Error fetching positions: " << positions_result.error().message << "\n";
                return 1;
            }

            std::vector<predibloom::api::Position> open_positions;
            for (const auto& p : positions_result.value()) {
                if (p.position() != 0) {
                    open_positions.push_back(p);
                }
            }

            if (open_positions.empty()) {
                std::cout << "No open positions\n";
            } else {
                std::cout << std::left
                          << std::setw(36) << "Ticker"
                          << std::right
                          << std::setw(5) << "Pos"
                          << std::setw(10) << "Exposure"
                          << std::setw(10) << "Realized"
                          << std::setw(10) << "Fees"
                          << "\n";
                std::cout << std::string(71, '-') << "\n";

                for (const auto& p : open_positions) {
                    std::string ticker_display = p.ticker;
                    if (ticker_display.size() > 34) {
                        ticker_display = ticker_display.substr(0, 34);
                    }

                    char exp_buf[16], pnl_buf[16], fee_buf[16];
                    snprintf(exp_buf, sizeof(exp_buf), "$%.2f", p.exposure_cents() / 100.0);
                    snprintf(pnl_buf, sizeof(pnl_buf), "$%.2f", p.realized_pnl_cents() / 100.0);
                    snprintf(fee_buf, sizeof(fee_buf), "$%.2f", p.fees_cents() / 100.0);

                    std::cout << std::left
                              << std::setw(36) << ticker_display
                              << std::right
                              << std::setw(5) << p.position()
                              << std::setw(10) << exp_buf
                              << std::setw(10) << pnl_buf
                              << std::setw(10) << fee_buf
                              << "\n";
                }
                std::cout << std::string(71, '-') << "\n";
                std::cout << open_positions.size() << " positions\n";
            }

        } else if (*portfolio_settlements_cmd) {
            // portfolio settlements — recent settlements
            predibloom::api::GetSettlementsParams settle_params;
            auto now = std::time(nullptr);
            settle_params.min_ts = now - portfolio_settle_days * 24 * 3600;
            settle_params.limit = 100;

            auto settle_result = client.getSettlements(settle_params);
            if (!settle_result.ok()) {
                std::cerr << "Error fetching settlements: " << settle_result.error().message << "\n";
                return 1;
            }
            const auto& settlements = settle_result.value().settlements;

            if (settlements.empty()) {
                std::cout << "No settlements in the last " << portfolio_settle_days << " days\n";
            } else {
                std::cout << std::left
                          << std::setw(36) << "Ticker"
                          << std::setw(8) << "Result"
                          << std::right
                          << std::setw(10) << "Revenue"
                          << "\n";
                std::cout << std::string(54, '-') << "\n";

                double total_revenue = 0;
                for (const auto& s : settlements) {
                    std::string ticker_display = s.ticker;
                    if (ticker_display.size() > 34) {
                        ticker_display = ticker_display.substr(0, 34);
                    }

                    std::string result_upper = s.market_result;
                    for (auto& c : result_upper) c = std::toupper(c);

                    char rev_buf[16];
                    snprintf(rev_buf, sizeof(rev_buf), "$%.2f", s.revenue_dollars());

                    std::cout << std::left
                              << std::setw(36) << ticker_display
                              << std::setw(8) << result_upper
                              << std::right
                              << std::setw(10) << rev_buf
                              << "\n";
                    total_revenue += s.revenue_dollars();
                }

                char total_rev_buf[16];
                snprintf(total_rev_buf, sizeof(total_rev_buf), "$%.2f", total_revenue);

                std::cout << std::string(54, '-') << "\n";
                std::cout << settlements.size() << " settlements, total revenue: " << total_rev_buf << "\n";
            }

        } else {
            // Default: just show total
            char total_buf[32];
            snprintf(total_buf, sizeof(total_buf), "$%.2f", (bal.balance + bal.portfolio_value) / 100.0);
            std::cout << total_buf << "\n";
        }
    }

    // Handle series command
    if (*series_cmd) {
        std::cout << std::left << std::setw(16) << "Ticker"
                  << std::setw(20) << "Label"
                  << std::right << std::setw(8) << "Offset"
                  << "  " << std::left << std::setw(14) << "Entry (PT)"
                  << std::setw(6) << "NWS"
                  << "\n";
        std::cout << std::string(66, '-') << "\n";

        for (const auto& tab : config.tabs) {
            for (const auto& s : tab.series) {
                if (s.latitude == 0) continue;  // skip non-weather series
                int utc = s.effectiveEntryHour();
                int pt = (utc - 7 + 24) % 24;  // UTC to PT (PDT)
                std::string pt_ampm = (pt >= 12) ? "pm" : "am";
                int pt12 = (pt % 12 == 0) ? 12 : (pt % 12);
                std::string day_note = (utc < 7) ? " prev" : "";
                if (s.entry_day_offset != 0) {
                    char d_buf[8];
                    snprintf(d_buf, sizeof(d_buf), " D%+d", s.entry_day_offset);
                    day_note += d_buf;
                }

                char offset_buf[16];
                snprintf(offset_buf, sizeof(offset_buf), "%+.1f F", s.offset);

                char entry_buf[32];
                snprintf(entry_buf, sizeof(entry_buf), "%d%s%s", pt12, pt_ampm.c_str(), day_note.c_str());

                std::cout << std::left << std::setw(16) << s.series_ticker
                          << std::setw(20) << s.label
                          << std::right << std::setw(8) << offset_buf
                          << "  " << std::left << std::setw(14) << entry_buf
                          << std::setw(6) << s.nws_station
                          << "\n";
            }
        }
    }

    return 0;
}
