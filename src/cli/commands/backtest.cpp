#include "backtest.hpp"
#include "../bracket.hpp"
#include "../../api/weather_client.hpp"
#include "../../api/nws_client.hpp"
#include "../../core/time_utils.hpp"
#include "../../core/weather_comparison.hpp"

#include <iostream>
#include <iomanip>
#include <map>
#include <vector>
#include <algorithm>
#include <random>
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
    int total() const {
        return no_forecast + no_nws_data + between_brackets +
               margin_too_small + no_trades_at_entry + no_trades_at_exit +
               price_too_high + price_too_low + trade_fetch_error;
    }
};

void printParameters(const BacktestOptions& opts, const std::vector<std::string>& series) {
    std::cerr << "Backtest parameters:\n";
    std::cerr << "  Series: ";
    for (size_t i = 0; i < series.size(); i++) {
        if (i > 0) std::cerr << ", ";
        std::cerr << series[i];
    }
    std::cerr << "\n";
    std::cerr << "  Offset: per-series from config\n";
    std::cerr << "  Margin: " << opts.margin << "°F (min distance from bracket edge)\n";
    std::cerr << "  Price range: " << opts.min_price << "¢ - " << opts.max_price << "¢\n";

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

    if (opts.trade_size > 0) {
        std::cerr << "  Trade size: $" << opts.trade_size << " per trade\n";
    } else {
        std::cerr << "  Trade size: $10 per °F margin\n";
    }

    if (opts.jitter > 0) {
        std::cerr << "  Jitter: +/-" << opts.jitter << "hr";
        if (opts.seed >= 0) {
            std::cerr << " (seed=" << opts.seed << ")";
        }
        std::cerr << "\n";
    }
    std::cerr << "\n";
}

void printResults(const std::vector<Trade>& trades, const SkipReasons& skip,
                  double total_pnl_dollars, double total_deployed,
                  int wins, int losses, bool show_series, bool show_exit) {
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
        if (skip.trade_fetch_error > 0)
            std::cout << "    trade fetch error:  " << skip.trade_fetch_error << "\n";
    }
    std::cout << "  Deployed: $" << std::fixed << std::setprecision(2) << total_deployed << "\n";
    std::cout << "  Total P&L: " << std::showpos << "$" << total_pnl_dollars << std::noshowpos << "\n";

    double roi = total_deployed > 0 ? (total_pnl_dollars / total_deployed * 100) : 0;
    std::cout << "  ROI: " << std::showpos << std::setprecision(1) << roi << "%" << std::noshowpos << "\n";
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

    api::NwsClient nws;
    client.setCaching(true);
    nws.setCaching(true);

    printParameters(opts, series_list);

    // Initialize RNG
    std::mt19937 rng(opts.seed >= 0 ? static_cast<unsigned>(opts.seed) : std::random_device{}());
    std::uniform_int_distribution<int> jitter_dist(-opts.jitter, opts.jitter);

    std::vector<Trade> trades;
    double total_pnl_dollars = 0;
    double total_deployed = 0;
    int wins = 0, losses = 0;
    SkipReasons skip;

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

        std::cerr << current_series << ": " << markets_by_date.size() << " trading days\n";

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

        if (nws_temps.empty()) {
            skip.no_nws_data += markets_by_date.size();
            continue;
        }

        for (const auto& [date, day_markets] : markets_by_date) {
            std::string as_of = core::computeAsOfIso(
                date, series_config->entry_day_offset, series_config->effectiveEntryHour());
            auto forecast_result = weather_client->getForecast(
                series_config->latitude, series_config->longitude, date, as_of);

            if (!forecast_result.ok()) { skip.no_forecast++; continue; }

            auto forecast_opt = is_low
                ? api::getMinTemperatureForDate(forecast_result.value(), date)
                : api::getTemperatureForDate(forecast_result.value(), date);
            if (!forecast_opt) { skip.no_forecast++; continue; }

            double forecast = *forecast_opt;
            double adjusted = forecast + series_config->offset;

            if (nws_temps.find(date) == nws_temps.end()) { skip.no_nws_data++; continue; }
            int nws_actual = nws_temps.at(date);

            std::vector<Bracket> brackets;
            for (const auto& market : day_markets) {
                brackets.push_back(parseBracket(market));
            }

            const Bracket* target = nullptr;
            double margin_from_edge = 0;
            for (const auto& b : brackets) {
                if (b.contains(adjusted)) {
                    margin_from_edge = b.marginFrom(adjusted);
                    target = &b;
                    break;
                }
            }

            if (!target) { skip.between_brackets++; continue; }
            if (margin_from_edge < opts.margin) { skip.margin_too_small++; continue; }

            auto trades_result = client.getAllTrades(target->market->ticker);
            if (!trades_result.ok()) { skip.trade_fetch_error++; continue; }

            int effective_entry_hour = (opts.entry_hour >= 0)
                ? opts.entry_hour : series_config->effectiveEntryHour();
            int delta = (opts.jitter > 0) ? jitter_dist(rng) : 0;
            std::string target_hour = core::computeEntryDatetimeWithJitter(
                date, series_config->entry_day_offset, effective_entry_hour, delta);

            double entry_price = -1;
            std::string entry_time;
            for (const auto& trade : trades_result.value()) {
                std::string trade_hour = trade.created_time.substr(0, 13);
                if (trade_hour <= target_hour && (entry_time.empty() || trade_hour > entry_time)) {
                    entry_price = trade.yes_price_cents();
                    entry_time = trade_hour;
                }
            }

            if (entry_price < 0) { skip.no_trades_at_entry++; continue; }
            if (entry_price > opts.max_price) { skip.price_too_high++; continue; }
            if (entry_price < opts.min_price) { skip.price_too_low++; continue; }

            bool is_bounded = target->floor.has_value() && target->cap.has_value();
            double exit_price = -1;
            bool won;
            double pnl;

            if (opts.exit_hour >= 0) {
                char exit_buf[3];
                snprintf(exit_buf, sizeof(exit_buf), "%02d", opts.exit_hour);
                std::string exit_target_hour = date + "T" + exit_buf;

                std::string exit_time;
                for (const auto& trade : trades_result.value()) {
                    std::string trade_hour = trade.created_time.substr(0, 13);
                    if (trade_hour <= exit_target_hour && (exit_time.empty() || trade_hour > exit_time)) {
                        exit_price = trade.yes_price_cents();
                        exit_time = trade_hour;
                    }
                }

                if (exit_price < 0) { skip.no_trades_at_exit++; continue; }
                pnl = exit_price - entry_price;
                won = (pnl > 0);
            } else {
                won = (target->market->result == "yes");
                pnl = won ? (100 - entry_price) : (-entry_price);
            }

            double trade_size = (opts.trade_size > 0) ? opts.trade_size : (10.0 * margin_from_edge);
            int contracts = static_cast<int>((trade_size * 100) / entry_price);
            if (contracts < 1) contracts = 1;
            double pnl_dollars = (pnl * contracts) / 100.0;

            Trade t{current_series, date, target->market->ticker, target->displayString(),
                    forecast, adjusted, entry_time, entry_price, exit_price, contracts,
                    nws_actual, won, is_bounded, pnl_dollars};
            trades.push_back(t);

            total_pnl_dollars += pnl_dollars;
            total_deployed += trade_size;
            if (won) wins++; else losses++;
        }
    }

    std::sort(trades.begin(), trades.end(), [](const Trade& a, const Trade& b) {
        if (a.date != b.date) return a.date < b.date;
        return a.series < b.series;
    });

    printResults(trades, skip, total_pnl_dollars, total_deployed, wins, losses,
                 series_list.size() > 1, opts.exit_hour >= 0);

    return 0;
}

}  // namespace predibloom::cli
