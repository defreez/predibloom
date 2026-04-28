#include "kalshi_sync.hpp"
#include "../../api/local_kalshi_client.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace predibloom::cli {

namespace {

void printProgress(const std::string& message) {
    std::cerr << message << std::flush;
}

}  // namespace

int runKalshiSync(const core::Config& config,
                  api::KalshiClient& client,
                  const std::vector<std::string>& series_filter,
                  bool skip_trades) {
    api::LocalKalshiClient local;

    if (!local.is_open()) {
        std::cerr << "Error: Could not open local database\n";
        return 1;
    }

    // Build list of series to sync
    std::vector<std::string> series_list;
    if (series_filter.empty()) {
        // Sync all configured series
        for (const auto& tab : config.tabs) {
            for (const auto& sc : tab.series) {
                series_list.push_back(sc.series_ticker);
            }
        }
        if (series_list.empty()) {
            std::cerr << "No series configured\n";
            return 1;
        }
    } else {
        series_list = series_filter;
    }

    auto start_time = std::chrono::steady_clock::now();

    for (const auto& series : series_list) {
        std::cerr << "Syncing " << series << "...\n";

        // Sync markets
        std::cerr << "  Markets: fetching... " << std::flush;
        auto markets_result = local.syncMarkets(client, series);
        if (!markets_result.ok()) {
            std::cerr << "error - " << markets_result.error().message << "\n";
            continue;
        }
        auto& ms = markets_result.value();
        std::cerr << ms.total_count << " (" << ms.new_count << " new, "
                  << ms.updated_count << " updated)\n";

        if (skip_trades) {
            continue;
        }

        // Get markets to iterate with progress
        api::GetMarketsParams params;
        params.series_ticker = series;
        auto local_markets = local.getAllMarkets(params);
        if (!local_markets.ok()) {
            std::cerr << "  Trades: error reading markets\n";
            continue;
        }

        const auto& markets = local_markets.value();
        std::cerr << "  Trades: 0/" << markets.size() << " markets" << std::flush;

        int total_trades = 0;
        int new_trades = 0;
        std::vector<std::pair<std::string, std::string>> failed_markets;  // ticker, error message
        auto trades_start = std::chrono::steady_clock::now();

        for (size_t i = 0; i < markets.size(); i++) {
            // Retry loop for transient errors
            int retries = 0;
            constexpr int max_retries = 3;
            api::Result<api::SyncStats> trades_result = api::Error(api::ApiError::NetworkError, "init");

            while (retries <= max_retries) {
                trades_result = local.syncTrades(client, markets[i].ticker);
                if (trades_result.ok()) {
                    total_trades += trades_result.value().total_count;
                    new_trades += trades_result.value().new_count;
                    break;
                }

                // Retry on transient errors (rate limit, network)
                auto err_type = trades_result.error().type;
                bool is_transient = (err_type == api::ApiError::RateLimitError ||
                                     err_type == api::ApiError::NetworkError);
                if (is_transient && retries < max_retries) {
                    retries++;
                    std::this_thread::sleep_for(std::chrono::seconds(2 * retries));
                    continue;
                }

                // Non-retryable or max retries exceeded
                failed_markets.push_back({markets[i].ticker, trades_result.error().message});
                break;
            }

            // Update progress with ETA
            auto now = std::chrono::steady_clock::now();
            double elapsed_sec = std::chrono::duration<double>(now - trades_start).count();
            double rate = (i + 1) / elapsed_sec;  // markets per second
            int remaining = static_cast<int>((markets.size() - i - 1) / rate);
            int eta_min = remaining / 60;
            int eta_sec = remaining % 60;

            std::cerr << "\r  Trades: " << (i + 1) << "/" << markets.size()
                      << " markets, " << total_trades << " trades";
            if (i > 0 && i + 1 < markets.size()) {
                std::cerr << " (ETA: " << eta_min << "m" << eta_sec << "s)";
            }
            std::cerr << "        " << std::flush;  // padding to clear old text
        }

        std::cerr << " (" << new_trades << " new";
        if (!failed_markets.empty()) {
            std::cerr << ", " << failed_markets.size() << " failed";
        }
        std::cerr << ")\n";

        // Show first few errors if any
        if (!failed_markets.empty()) {
            std::cerr << "  Failed markets:\n";
            size_t show_count = std::min(failed_markets.size(), size_t(5));
            for (size_t i = 0; i < show_count; i++) {
                std::cerr << "    " << failed_markets[i].first << ": " << failed_markets[i].second << "\n";
            }
            if (failed_markets.size() > 5) {
                std::cerr << "    ... and " << (failed_markets.size() - 5) << " more\n";
            }
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    double seconds = duration.count() / 1000.0;

    std::cerr << "Done in " << std::fixed << std::setprecision(1) << seconds << "s\n";
    return 0;
}

int runKalshiList(const std::string& series,
                  const std::string& format) {
    api::LocalKalshiClient local;

    if (!local.is_open()) {
        std::cerr << "Error: Could not open local database\n";
        return 1;
    }

    if (series.empty()) {
        std::cerr << "Error: --series required for list command\n";
        return 1;
    }

    api::GetMarketsParams params;
    params.series_ticker = series;
    auto result = local.getAllMarkets(params);

    if (!result.ok()) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
    }

    const auto& markets = result.value();

    if (format == "json") {
        std::cout << "[\n";
        for (size_t i = 0; i < markets.size(); i++) {
            const auto& m = markets[i];
            std::cout << "  {\"ticker\": \"" << m.ticker << "\""
                      << ", \"event_ticker\": \"" << m.event_ticker << "\""
                      << ", \"result\": \"" << m.result << "\"";
            if (m.floor_strike.has_value()) {
                std::cout << ", \"floor_strike\": " << m.floor_strike.value();
            }
            if (m.cap_strike.has_value()) {
                std::cout << ", \"cap_strike\": " << m.cap_strike.value();
            }
            std::cout << "}";
            if (i < markets.size() - 1) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "]\n";
    } else {
        // Table format
        std::cout << std::left
                  << std::setw(30) << "Ticker"
                  << std::setw(30) << "Event"
                  << std::setw(10) << "Floor"
                  << std::setw(10) << "Cap"
                  << std::setw(10) << "Result"
                  << "\n";
        std::cout << std::string(90, '-') << "\n";

        for (const auto& m : markets) {
            std::cout << std::left
                      << std::setw(30) << m.ticker
                      << std::setw(30) << m.event_ticker
                      << std::setw(10) << (m.floor_strike.has_value() ? std::to_string(m.floor_strike.value()) : "-")
                      << std::setw(10) << (m.cap_strike.has_value() ? std::to_string(m.cap_strike.value()) : "-")
                      << std::setw(10) << (m.result.empty() ? "-" : m.result)
                      << "\n";
        }

        std::cout << "\nTotal: " << markets.size() << " markets\n";
    }

    return 0;
}

int runKalshiStatus(const std::string& format) {
    api::LocalKalshiClient local;

    if (!local.is_open()) {
        std::cerr << "Error: Could not open local database\n";
        return 1;
    }

    auto stats = local.getSeriesStats();

    if (stats.empty()) {
        std::cout << "No synced data. Run 'kalshi sync' to download data.\n";
        return 0;
    }

    if (format == "json") {
        std::cout << "[\n";
        for (size_t i = 0; i < stats.size(); i++) {
            const auto& s = stats[i];
            std::cout << "  {\"series\": \"" << s.series_ticker << "\""
                      << ", \"markets\": " << s.market_count
                      << ", \"trades\": " << s.trade_count
                      << ", \"last_sync\": \"" << s.last_sync << "\"}";
            if (i < stats.size() - 1) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "]\n";
    } else {
        // Table format
        std::cout << std::left
                  << std::setw(20) << "Series"
                  << std::setw(12) << "Markets"
                  << std::setw(12) << "Trades"
                  << std::setw(24) << "Last Sync"
                  << "\n";
        std::cout << std::string(68, '-') << "\n";

        for (const auto& s : stats) {
            std::cout << std::left
                      << std::setw(20) << s.series_ticker
                      << std::setw(12) << s.market_count
                      << std::setw(12) << s.trade_count
                      << std::setw(24) << s.last_sync
                      << "\n";
        }
    }

    return 0;
}

}  // namespace predibloom::cli
