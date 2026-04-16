#include <CLI/CLI.hpp>
#include "../api/kalshi_client.hpp"
#include "../api/openmeteo_client.hpp"
#include "../api/nws_client.hpp"
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
    auto* backtest_cmd = app.add_subcommand("backtest", "Backtest trading strategy using forecasts");

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
    std::string backtest_series;
    std::string backtest_start;
    std::string backtest_end;
    double backtest_offset = 2.0;      // Calibration offset: NWS typically runs hotter than Open-Meteo
    double backtest_margin = 3.0;      // Only trade if forecast is this far from bracket edge
    double backtest_max_price = 40.0;  // Only buy if price is below this (cents)
    double backtest_bankroll = 100.0;  // Starting bankroll in dollars
    int backtest_entry_hour = 12;      // Entry time: hour of day before settlement (0-23, in settlement day's timezone)

    backtest_cmd->add_option("-s,--series", backtest_series, "Series ticker (e.g., KXHIGHNY)")
        ->required();
    backtest_cmd->add_option("--start", backtest_start, "Start date YYYY-MM-DD")
        ->required();
    backtest_cmd->add_option("--end", backtest_end, "End date YYYY-MM-DD")
        ->required();
    backtest_cmd->add_option("--offset", backtest_offset, "Calibration offset to add to Open-Meteo forecast (°F)")
        ->default_val(2.0);
    backtest_cmd->add_option("--margin", backtest_margin, "Min distance from bracket edge to trade (°F)")
        ->default_val(3.0);
    backtest_cmd->add_option("--max-price", backtest_max_price, "Max price to pay (cents)")
        ->default_val(40.0);
    backtest_cmd->add_option("--bankroll", backtest_bankroll, "Starting bankroll ($)")
        ->default_val(100.0);
    backtest_cmd->add_option("--entry-hour", backtest_entry_hour, "Hour of day to enter (0-23, on settlement day)")
        ->default_val(12)
        ->check(CLI::Range(0, 23));

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
            predibloom::core::NYC_LATITUDE, predibloom::core::NYC_LONGITUDE,
            winners_start, winners_end);

        // Fetch Open-Meteo actual (ERA5 reanalysis - for comparison)
        auto openmeteo_actual_result = openmeteo.getHistoricalWeather(
            predibloom::core::NYC_LATITUDE, predibloom::core::NYC_LONGITUDE,
            winners_start, winners_end);

        // Fetch actual temperatures from NWS CLI (authoritative for Kalshi settlement)
        int start_year = std::stoi(winners_start.substr(0, 4));
        int end_year = std::stoi(winners_end.substr(0, 4));
        std::map<std::string, int> nws_highs;  // date -> NWS high temp

        for (int year = start_year; year <= end_year; year++) {
            auto nws_result = nws.getCliData(predibloom::api::stations::NYC_CENTRAL_PARK, year);
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
        predibloom::api::OpenMeteoClient openmeteo;
        predibloom::api::NwsClient nws;

        std::cerr << "Backtest parameters:\n";
        std::cerr << "  Offset: +" << backtest_offset << "°F (added to Open-Meteo forecast)\n";
        std::cerr << "  Margin: " << backtest_margin << "°F (min distance from bracket edge)\n";
        std::cerr << "  Max price: " << backtest_max_price << "¢\n";
        std::cerr << "  Entry hour: " << backtest_entry_hour << ":00 on settlement day\n";
        std::cerr << "  Bankroll: $" << backtest_bankroll << "\n\n";

        // Get all markets for the series
        predibloom::api::GetMarketsParams params;
        params.series_ticker = backtest_series;

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

            auto ticker_info = predibloom::core::parseHighNYTicker(market.ticker);
            if (!ticker_info.valid) continue;
            if (ticker_info.date < backtest_start || ticker_info.date > backtest_end) continue;

            markets_by_date[ticker_info.date].push_back(market);
        }

        std::cerr << "Found " << markets_by_date.size() << " trading days\n";

        // Fetch forecast data
        auto forecast_result = openmeteo.getHistoricalForecast(
            predibloom::core::NYC_LATITUDE, predibloom::core::NYC_LONGITUDE,
            backtest_start, backtest_end);

        if (!forecast_result.ok()) {
            std::cerr << "Error fetching forecasts: " << forecast_result.error().message << "\n";
            return 1;
        }

        // Fetch NWS actual data
        int start_year = std::stoi(backtest_start.substr(0, 4));
        int end_year = std::stoi(backtest_end.substr(0, 4));
        std::map<std::string, int> nws_highs;

        for (int year = start_year; year <= end_year; year++) {
            auto nws_result = nws.getCliData(predibloom::api::stations::NYC_CENTRAL_PARK, year);
            if (nws_result.ok()) {
                for (const auto& obs : nws_result.value()) {
                    if (obs.date >= backtest_start && obs.date <= backtest_end) {
                        nws_highs[obs.date] = obs.high;
                    }
                }
            }
        }

        // Backtest results
        struct Trade {
            std::string date;
            std::string ticker;
            std::string strike;
            double forecast;       // Raw Open-Meteo forecast
            double adjusted;       // After offset
            std::string entry_time;// When we entered (hour)
            double entry_price;    // Price paid (cents)
            int nws_actual;        // Settlement temp
            bool won;
            double pnl;            // Profit/loss in cents per contract
        };
        std::vector<Trade> trades;

        double total_pnl_cents = 0;
        int wins = 0, losses = 0, skipped = 0;

        // Process each day
        for (const auto& [date, day_markets] : markets_by_date) {
            // Get forecast for this date
            auto forecast_opt = predibloom::api::getTemperatureForDate(forecast_result.value(), date);
            if (!forecast_opt) {
                skipped++;
                continue;
            }

            double forecast = *forecast_opt;
            double adjusted = forecast + backtest_offset;

            // Get NWS actual
            if (nws_highs.find(date) == nws_highs.end()) {
                skipped++;
                continue;
            }
            int nws_actual = nws_highs.at(date);

            // Find the bracket our adjusted forecast falls into
            const predibloom::api::Market* target_market = nullptr;
            double margin_from_edge = 0;

            for (const auto& market : day_markets) {
                bool in_bracket = false;
                double dist_from_floor = 999, dist_from_cap = 999;

                if (market.floor_strike && market.cap_strike) {
                    // Range bracket: floor <= temp < cap
                    if (adjusted >= *market.floor_strike && adjusted < *market.cap_strike) {
                        in_bracket = true;
                        dist_from_floor = adjusted - *market.floor_strike;
                        dist_from_cap = *market.cap_strike - adjusted;
                    }
                } else if (market.floor_strike && !market.cap_strike) {
                    // Floor only: temp >= floor (e.g., 70+)
                    if (adjusted >= *market.floor_strike) {
                        in_bracket = true;
                        dist_from_floor = adjusted - *market.floor_strike;
                        dist_from_cap = 999;  // No cap
                    }
                } else if (!market.floor_strike && market.cap_strike) {
                    // Cap only: temp < cap (e.g., <39)
                    if (adjusted < *market.cap_strike) {
                        in_bracket = true;
                        dist_from_floor = 999;  // No floor
                        dist_from_cap = *market.cap_strike - adjusted;
                    }
                }

                if (in_bracket) {
                    margin_from_edge = std::min(dist_from_floor, dist_from_cap);
                    target_market = &market;
                    break;
                }
            }

            if (!target_market) {
                skipped++;
                continue;
            }

            // Check margin requirement
            if (margin_from_edge < backtest_margin) {
                skipped++;  // Too close to edge, skip
                continue;
            }

            // Get entry price at specific hour on settlement day
            auto trades_result = client.getAllTrades(target_market->ticker);
            if (!trades_result.ok()) {
                skipped++;
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
                skipped++;  // No trade before entry time
                continue;
            }

            // Check if price is within our max
            if (entry_price > backtest_max_price) {
                skipped++;  // Price too high at entry time
                continue;
            }

            // Determine if we won
            bool won = (target_market->result == "yes");

            // Calculate P&L (per $1 contract)
            double pnl = won ? (100 - entry_price) : (-entry_price);

            // Format strike for display
            std::string strike;
            if (target_market->floor_strike && !target_market->cap_strike) {
                strike = std::to_string(*target_market->floor_strike) + "+";
            } else if (!target_market->floor_strike && target_market->cap_strike) {
                strike = "<" + std::to_string(*target_market->cap_strike);
            } else if (target_market->floor_strike && target_market->cap_strike) {
                strike = std::to_string(*target_market->floor_strike) + "-" + std::to_string(*target_market->cap_strike);
            }

            Trade t{date, target_market->ticker, strike, forecast, adjusted, entry_time, entry_price, nws_actual, won, pnl};
            trades.push_back(t);

            total_pnl_cents += pnl;
            if (won) wins++; else losses++;
        }

        // Output results
        std::cout << "\n=== BACKTEST RESULTS ===\n\n";
        std::cout << std::left << std::setw(12) << "Date"
                  << std::setw(10) << "Strike"
                  << std::setw(9) << "Forecast"
                  << std::setw(9) << "Adjusted"
                  << std::setw(6) << "NWS"
                  << std::setw(15) << "Entry Time"
                  << std::setw(7) << "Price"
                  << std::setw(7) << "Result"
                  << std::setw(8) << "P&L"
                  << "\n";
        std::cout << std::string(88, '-') << "\n";

        for (const auto& t : trades) {
            std::cout << std::left << std::setw(12) << t.date
                      << std::setw(10) << t.strike
                      << std::fixed << std::setprecision(1)
                      << std::setw(9) << t.forecast
                      << std::setw(9) << t.adjusted
                      << std::setw(6) << t.nws_actual
                      << std::setw(15) << t.entry_time
                      << std::setw(7) << t.entry_price
                      << std::setw(7) << (t.won ? "WIN" : "LOSS")
                      << std::showpos << std::setw(8) << t.pnl << std::noshowpos
                      << "\n";
        }

        std::cout << std::string(88, '-') << "\n";
        std::cout << "\nSummary:\n";
        std::cout << "  Trades: " << trades.size() << " (" << wins << " wins, " << losses << " losses)\n";
        std::cout << "  Win rate: " << std::fixed << std::setprecision(1)
                  << (trades.empty() ? 0 : 100.0 * wins / trades.size()) << "%\n";
        std::cout << "  Skipped: " << skipped << " days (no forecast, bad margin, or no price)\n";
        std::cout << "  Total P&L: " << std::showpos << total_pnl_cents << std::noshowpos << "¢ per contract\n";

        // Calculate ROI based on capital deployed
        double avg_entry = 0;
        for (const auto& t : trades) avg_entry += t.entry_price;
        if (!trades.empty()) avg_entry /= trades.size();

        double total_deployed = avg_entry * trades.size();  // cents
        double roi = total_deployed > 0 ? (total_pnl_cents / total_deployed * 100) : 0;

        std::cout << "  Avg entry: " << std::fixed << std::setprecision(1) << avg_entry << "¢\n";
        std::cout << "  ROI: " << std::showpos << std::setprecision(1) << roi << "%" << std::noshowpos << "\n";
    }

    return 0;
}
