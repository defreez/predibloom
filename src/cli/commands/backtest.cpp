#include "backtest.hpp"
#include "../bracket.hpp"
#include "../../api/weather_client.hpp"
#include "../../api/nws_client.hpp"
#include "../../core/time_utils.hpp"
#include "../../core/weather_comparison.hpp"
#include "../../core/backtest_algo.hpp"

#include <iostream>
#include <iomanip>
#include <map>
#include <vector>
#include <algorithm>
#include <cstdio>

namespace predibloom::cli {

namespace {

struct Trade {
    std::string series;
    std::string date;
    std::string ticker;
    std::string strike;
    double forecast;
    double adjusted;
    std::string entry_time;
    double entry_price;
    double exit_price;
    int contracts;
    int nws_actual;
    bool won;
    bool is_bounded;
    double pnl;

    // Latency algo metadata
    std::string cycle_used;
    int latency_hours;
};

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
    int no_cycle_available = 0;

    int total() const {
        return no_forecast + no_nws_data + between_brackets +
               margin_too_small + no_trades_at_entry + no_trades_at_exit +
               price_too_high + price_too_low + trade_fetch_error + no_cycle_available;
    }

    void addFromReason(const std::string& reason) {
        if (reason == core::SkipReason::NoForecast) no_forecast++;
        else if (reason == core::SkipReason::BetweenBrackets) between_brackets++;
        else if (reason == core::SkipReason::MarginTooSmall) margin_too_small++;
        else if (reason == core::SkipReason::NoTradesAtEntry) no_trades_at_entry++;
        else if (reason == core::SkipReason::NoTradesAtExit) no_trades_at_exit++;
        else if (reason == core::SkipReason::PriceTooHigh) price_too_high++;
        else if (reason == core::SkipReason::PriceTooLow) price_too_low++;
        else if (reason == core::SkipReason::NoCycleAvailable) no_cycle_available++;
    }
};

void printParameters(const BacktestOptions& opts, const std::vector<std::string>& series,
                     const std::string& algo_name) {
    std::cerr << "Backtest parameters:\n";
    std::cerr << "  Algo: " << (algo_name.empty() ? "simple" : algo_name) << "\n";
    std::cerr << "  Series: ";
    for (size_t i = 0; i < series.size(); i++) {
        if (i > 0) std::cerr << ", ";
        std::cerr << series[i];
    }
    std::cerr << "\n";
    std::cerr << "  Offset: per-series from config\n";
    std::cerr << "  Margin: " << opts.margin << "°F (min distance from bracket edge)\n";
    std::cerr << "  Price range: " << opts.min_price << "¢ - " << opts.max_price << "¢\n";

    if (opts.algo == "latency") {
        if (!opts.latency_sweep.empty()) {
            std::cerr << "  Latency sweep: ";
            for (size_t i = 0; i < opts.latency_sweep.size(); i++) {
                if (i > 0) std::cerr << ", ";
                std::cerr << opts.latency_sweep[i] << "hr";
            }
            std::cerr << "\n";
        } else {
            std::cerr << "  Latency: " << opts.latency_hours << " hours after cycle\n";
        }
    } else {
        if (opts.entry_hour >= 0) {
            int pt_hour = (opts.entry_hour - 7 + 24) % 24;
            std::string pt_ampm = (pt_hour >= 12) ? "pm" : "am";
            int pt_hour_12 = (pt_hour % 12 == 0) ? 12 : (pt_hour % 12);
            std::string day_note = (opts.entry_hour < 7) ? " (previous day)" : "";
            std::cerr << "  Entry: " << opts.entry_hour << ":00 UTC = " << pt_hour_12 << pt_ampm << " PT" << day_note << "\n";
        } else {
            std::cerr << "  Entry: per-series from config (high=5 UTC/9pm PT, low=17 UTC/9am PT)\n";
        }

        if (opts.exit_hour >= 0) {
            int exit_pt = (opts.exit_hour - 7 + 24) % 24;
            std::string exit_ampm = (exit_pt >= 12) ? "pm" : "am";
            int exit_12 = (exit_pt % 12 == 0) ? 12 : (exit_pt % 12);
            std::string exit_day_note = (opts.exit_hour < 7) ? " (previous day)" : "";
            std::cerr << "  Exit: " << opts.exit_hour << ":00 UTC = " << exit_12 << exit_ampm << " PT" << exit_day_note << "\n";
        } else {
            std::cerr << "  Exit: hold to settlement\n";
        }
    }

    if (opts.trade_size > 0) {
        std::cerr << "  Trade size: $" << opts.trade_size << " per trade\n";
    } else {
        std::cerr << "  Trade size: $10 per °F margin\n";
    }
    std::cerr << "\n";
}

void printResults(const std::vector<Trade>& trades, const SkipReasons& skip,
                  double total_pnl_dollars, double total_deployed,
                  int wins, int losses, bool show_series, bool show_exit,
                  bool show_latency) {
    std::cout << "\n=== BACKTEST RESULTS ===\n\n";

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
    if (show_latency) {
        std::cout << std::setw(8) << "Latency";
    }
    std::cout << std::setw(6) << "Ctrs"
              << std::setw(7) << "Result"
              << std::setw(10) << "P&L"
              << std::setw(10) << "Balance"
              << "\n";

    int line_width = 82 + (show_series ? 12 : 0) + (show_exit ? 7 : 0) + (show_latency ? 8 : 0);
    std::cout << std::string(line_width, '-') << "\n";

    double running_balance = 0;
    for (const auto& t : trades) {
        running_balance += t.pnl;
        if (show_series) {
            std::cout << std::left << std::setw(12) << t.series;
        }

        std::string entry_pt_str = "-";
        if (t.entry_time.size() >= 13) {
            int utc_hour = std::stoi(t.entry_time.substr(11, 2));
            int pt_hour = (utc_hour - 7 + 24) % 24;
            int pt12 = (pt_hour % 12 == 0) ? 12 : (pt_hour % 12);
            std::string ampm = (pt_hour >= 12) ? "pm" : "am";
            entry_pt_str = std::to_string(pt12) + ampm;
        }

        std::string nws_str = (t.nws_actual == -999) ? "-" : std::to_string(t.nws_actual);
        std::cout << std::left << std::setw(12) << t.date
                  << std::setw(10) << t.strike
                  << std::setw(6) << nws_str
                  << std::setw(9) << entry_pt_str
                  << std::setw(7) << (std::to_string(static_cast<int>(t.entry_price)) + "c");
        if (show_exit) {
            std::cout << std::setw(7) << (std::to_string(static_cast<int>(t.exit_price)) + "c");
        }
        if (show_latency) {
            std::cout << std::setw(8) << (std::to_string(t.latency_hours) + "hr");
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

    // Compute stats
    int bounded_count = 0, unbounded_count = 0;
    double bounded_pnl = 0, unbounded_pnl = 0;
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

    if (show_series) {
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
        if (skip.no_cycle_available > 0)
            std::cout << "    no cycle available: " << skip.no_cycle_available << "\n";
        if (skip.trade_fetch_error > 0)
            std::cout << "    trade fetch error:  " << skip.trade_fetch_error << "\n";
    }
    std::cout << "  Deployed: $" << std::fixed << std::setprecision(2) << total_deployed << "\n";
    std::cout << "  Total P&L: " << std::showpos << "$" << total_pnl_dollars << std::noshowpos << "\n";

    double roi = total_deployed > 0 ? (total_pnl_dollars / total_deployed * 100) : 0;
    std::cout << "  ROI: " << std::showpos << std::setprecision(1) << roi << "%" << std::noshowpos << "\n";
}

void printLatencySweep(const std::map<int, std::vector<Trade>>& trades_by_latency) {
    std::cout << "\n=== LATENCY SWEEP RESULTS ===\n\n";

    std::cout << std::left
              << std::setw(10) << "Latency"
              << std::setw(8) << "Trades"
              << std::setw(8) << "Wins"
              << std::setw(10) << "WinRate"
              << std::setw(12) << "P&L"
              << std::setw(10) << "ROI"
              << "\n";
    std::cout << std::string(58, '-') << "\n";

    std::vector<std::pair<int, double>> latency_roi;

    for (const auto& [latency, trades] : trades_by_latency) {
        int bucket_wins = 0;
        double bucket_pnl = 0;
        double bucket_deployed = 0;

        for (const auto& t : trades) {
            if (t.won) bucket_wins++;
            bucket_pnl += t.pnl;
            bucket_deployed += (t.contracts * t.entry_price) / 100.0;
        }

        int n = static_cast<int>(trades.size());
        double win_rate = n > 0 ? (100.0 * bucket_wins / n) : 0;
        double roi = bucket_deployed > 0 ? (100.0 * bucket_pnl / bucket_deployed) : 0;

        latency_roi.push_back({latency, roi});

        char pnl_buf[16], roi_buf[16];
        snprintf(pnl_buf, sizeof(pnl_buf), "$%+.2f", bucket_pnl);
        snprintf(roi_buf, sizeof(roi_buf), "%+.1f%%", roi);

        std::cout << std::left
                  << std::setw(10) << (std::to_string(latency) + "hr")
                  << std::setw(8) << n
                  << std::setw(8) << bucket_wins
                  << std::setw(10) << (std::to_string(static_cast<int>(win_rate)) + "%")
                  << std::setw(12) << pnl_buf
                  << std::setw(10) << roi_buf
                  << "\n";
    }

    // Calculate edge half-life (latency at which ROI drops to half of max)
    if (latency_roi.size() >= 2) {
        double max_roi = 0;
        for (const auto& [lat, roi] : latency_roi) {
            if (roi > max_roi) max_roi = roi;
        }

        if (max_roi > 0) {
            double half_roi = max_roi / 2;
            for (size_t i = 1; i < latency_roi.size(); i++) {
                if (latency_roi[i].second < half_roi && latency_roi[i-1].second >= half_roi) {
                    // Linear interpolation
                    double t = (latency_roi[i-1].second - half_roi) /
                               (latency_roi[i-1].second - latency_roi[i].second);
                    double half_life = latency_roi[i-1].first +
                                       t * (latency_roi[i].first - latency_roi[i-1].first);
                    std::cout << "\nEdge half-life: ~" << std::fixed << std::setprecision(1)
                              << half_life << " hours\n";
                    break;
                }
            }
        }
    }
}

}  // namespace

int runBacktest(const BacktestOptions& opts,
                const core::Config& config,
                api::KalshiClient& client) {
    // Build series list
    std::vector<std::string> series_list = opts.series;
    if (series_list.empty()) {
        for (const auto& tab : config.tabs) {
            for (const auto& sc : tab.series) {
                if (sc.latitude != 0 && !sc.nws_station.empty()) {
                    series_list.push_back(sc.series_ticker);
                }
            }
        }
        if (series_list.empty()) {
            std::cerr << "No series with weather params configured\n";
            return 1;
        }
    }

    // Validate all series
    for (const auto& series : series_list) {
        auto* sc = config.findSeries(series);
        if (!sc || sc->nws_station.empty()) {
            std::cerr << "Series not configured or missing weather params: " << series << "\n";
            return 1;
        }
    }

    // Check GribStream availability
    bool needs_gribstream = false;
    for (const auto& series : series_list) {
        auto* sc = config.findSeries(series);
        if (sc && sc->weather_source == core::WeatherSource::GribStream) {
            needs_gribstream = true;
            break;
        }
    }
    if (needs_gribstream && !config.hasGribstream()) {
        std::cerr << "GribStream API token not configured\n";
        return 1;
    }

    // Handle latency sweep mode
    std::vector<int> latencies_to_test;
    if (!opts.latency_sweep.empty()) {
        latencies_to_test = opts.latency_sweep;
    } else {
        latencies_to_test.push_back(opts.latency_hours);
    }

    // For sweep mode, we'll collect trades per latency
    std::map<int, std::vector<Trade>> trades_by_latency;
    bool is_sweep = opts.latency_sweep.size() > 1;

    api::NwsClient nws;
    client.setCaching(true);
    nws.setCaching(true);

    std::string algo_name = opts.algo.empty() ? "simple" : opts.algo;
    printParameters(opts, series_list, algo_name);

    // Aggregate results across all latencies
    std::vector<Trade> all_trades;
    double total_pnl_dollars = 0;
    double total_deployed = 0;
    int wins = 0, losses = 0;
    SkipReasons skip;

    for (int current_latency : latencies_to_test) {
        // Build algo config
        core::AlgoConfig algo_cfg;
        algo_cfg.margin = opts.margin;
        algo_cfg.min_price = opts.min_price;
        algo_cfg.max_price = opts.max_price;
        algo_cfg.entry_hour = opts.entry_hour;
        algo_cfg.exit_hour = opts.exit_hour;
        algo_cfg.trade_size = opts.trade_size;
        algo_cfg.latency_hours = current_latency;

        auto algo = core::createAlgo(algo_name, algo_cfg);

        for (const auto& current_series : series_list) {
            auto* series_config = config.findSeries(current_series);

            auto weather_client = api::WeatherClient::create(
                series_config->weather_source, config.gribstream_api_token);
            weather_client->setCaching(true);

            api::GetMarketsParams params;
            params.series_ticker = current_series;
            auto markets_result = client.getAllMarkets(params);
            if (!markets_result.ok()) {
                std::cerr << "Error fetching markets: " << markets_result.error().message << "\n";
                return 1;
            }

            std::map<std::string, std::vector<api::Market>> markets_by_date;
            for (const auto& market : markets_result.value()) {
                if (market.result.empty()) continue;
                std::string date = core::parseDateFromEventTicker(market.event_ticker);
                if (date.empty() || date < opts.start_date || date > opts.end_date) continue;
                markets_by_date[date].push_back(market);
            }

            if (!is_sweep) {
                std::cerr << current_series << ": " << markets_by_date.size() << " trading days\n";
            }

            // Fetch NWS data
            bool is_low = series_config->isLowTemp();
            int start_year = std::stoi(opts.start_date.substr(0, 4));
            int end_year = std::stoi(opts.end_date.substr(0, 4));
            std::map<std::string, int> nws_temps;

            for (int year = start_year; year <= end_year; year++) {
                auto nws_result = nws.getCliData(series_config->nws_station, year);
                if (nws_result.ok()) {
                    for (const auto& obs : nws_result.value()) {
                        if (obs.date >= opts.start_date && obs.date <= opts.end_date) {
                            nws_temps[obs.date] = is_low ? obs.low : obs.high;
                        }
                    }
                }
            }

            // NWS data is optional - used for display only, not for settlement
            // Settlement outcome comes from market.result

            for (const auto& [date, day_markets] : markets_by_date) {
                // NWS actual is optional - use -999 as sentinel for missing
                int nws_actual = -999;
                if (nws_temps.find(date) != nws_temps.end()) {
                    nws_actual = nws_temps.at(date);
                }

                // Get default forecast
                std::string as_of = core::computeAsOfIso(
                    date, series_config->entry_day_offset, series_config->effectiveEntryHour());
                auto forecast_result = weather_client->getForecast(
                    series_config->latitude, series_config->longitude, date, as_of);

                std::optional<double> forecast_opt;
                if (forecast_result.ok()) {
                    forecast_opt = is_low
                        ? api::getMinTemperatureForDate(forecast_result.value(), date)
                        : api::getTemperatureForDate(forecast_result.value(), date);
                }

                double forecast = forecast_opt.value_or(0);
                double adjusted = forecast + series_config->offset;

                // Find the target bracket to get trades
                std::vector<cli::Bracket> brackets;
                for (const auto& market : day_markets) {
                    brackets.push_back(cli::parseBracket(market));
                }

                const cli::Bracket* target_bracket = nullptr;
                for (const auto& b : brackets) {
                    if (b.contains(adjusted)) {
                        target_bracket = &b;
                        break;
                    }
                }

                // Get trades for the target bracket (if any)
                std::vector<api::Trade> bracket_trades;
                if (target_bracket) {
                    auto trades_result = client.getAllTrades(target_bracket->market->ticker);
                    if (!trades_result.ok()) {
                        skip.trade_fetch_error++;
                        continue;
                    }
                    bracket_trades = trades_result.value();
                }

                // Build trade context
                core::TradeContext ctx;
                ctx.date = date;
                ctx.series = series_config;
                ctx.markets = &day_markets;
                ctx.trades = &bracket_trades;
                ctx.nws_actual = nws_actual;
                ctx.default_forecast = forecast_opt;
                ctx.adjusted_forecast = adjusted;

                // Forecast accessor for latency algo (could fetch cycle-specific forecasts)
                ctx.getForecast = [&](const std::string& cycle_date, int cycle_hour)
                    -> std::optional<double> {
                    // For now, return the default forecast
                    // TODO: Implement cycle-specific forecast lookup from local NBM cache
                    return forecast_opt;
                };

                // Evaluate with algo
                auto decision = algo->evaluate(ctx);

                if (!decision.enter) {
                    skip.addFromReason(decision.skip_reason);
                    continue;
                }

                // Re-fetch trades for the chosen ticker if different from target_bracket
                std::vector<api::Trade> decision_trades = bracket_trades;
                if (decision.ticker != (target_bracket ? target_bracket->market->ticker : "")) {
                    auto trades_result = client.getAllTrades(decision.ticker);
                    if (!trades_result.ok()) {
                        skip.trade_fetch_error++;
                        continue;
                    }
                    decision_trades = trades_result.value();
                }

                // Find the market for the decision
                const api::Market* decision_market = nullptr;
                for (const auto& m : day_markets) {
                    if (m.ticker == decision.ticker) {
                        decision_market = &m;
                        break;
                    }
                }

                if (!decision_market) {
                    skip.trade_fetch_error++;
                    continue;
                }

                // Compute P&L
                bool won;
                double pnl;
                if (decision.exit_time == "settlement") {
                    won = (decision_market->result == "yes");
                    pnl = won ? (100 - decision.entry_price) : (-decision.entry_price);
                } else {
                    pnl = decision.exit_price - decision.entry_price;
                    won = (pnl > 0);
                }

                double trade_size = (algo_cfg.trade_size > 0)
                    ? algo_cfg.trade_size
                    : (10.0 * decision.margin_from_edge);
                double pnl_dollars = (pnl * decision.contracts) / 100.0;

                Trade t;
                t.series = current_series;
                t.date = date;
                t.ticker = decision.ticker;
                t.strike = decision.strike;
                t.forecast = forecast;
                t.adjusted = adjusted;
                t.entry_time = decision.entry_time;
                t.entry_price = decision.entry_price;
                t.exit_price = decision.exit_price;
                t.contracts = decision.contracts;
                t.nws_actual = nws_actual;
                t.won = won;
                t.is_bounded = decision.is_bounded;
                t.pnl = pnl_dollars;
                t.cycle_used = decision.cycle_used;
                t.latency_hours = decision.latency_hours;

                if (is_sweep) {
                    trades_by_latency[current_latency].push_back(t);
                } else {
                    all_trades.push_back(t);
                }

                total_pnl_dollars += pnl_dollars;
                total_deployed += trade_size;
                if (won) wins++; else losses++;
            }
        }
    }

    if (is_sweep) {
        printLatencySweep(trades_by_latency);
    } else {
        std::sort(all_trades.begin(), all_trades.end(), [](const Trade& a, const Trade& b) {
            if (a.date != b.date) return a.date < b.date;
            return a.series < b.series;
        });

        bool show_latency = (algo_name == "latency");
        printResults(all_trades, skip, total_pnl_dollars, total_deployed, wins, losses,
                     series_list.size() > 1, opts.exit_hour >= 0, show_latency);
    }

    return 0;
}

}  // namespace predibloom::cli
