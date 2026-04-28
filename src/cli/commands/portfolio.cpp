#include "portfolio.hpp"
#include <iostream>
#include <iomanip>
#include <cstdio>
#include <cctype>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

namespace predibloom::cli {

namespace {

int checkAuth(const core::Config& config, api::KalshiClient& client) {
    if (!config.hasAuth()) {
        std::cerr << "Error: Authentication not configured.\n";
        std::cerr << "Add api_key_id and key_file to ~/.config/predibloom/auth.json\n";
        return 1;
    }
    client.setAuth(config.api_key_id, config.key_file);
    return 0;
}

}  // namespace

int runPortfolioBalance(const core::Config& config, api::KalshiClient& client) {
    if (int err = checkAuth(config, client)) return err;

    auto balance_result = client.getBalance();
    if (!balance_result.ok()) {
        std::cerr << "Error fetching balance: " << balance_result.error().message << "\n";
        return 1;
    }
    const auto& bal = balance_result.value();

    char total_buf[32];
    snprintf(total_buf, sizeof(total_buf), "$%.2f", (bal.balance + bal.portfolio_value) / 100.0);
    std::cout << total_buf << "\n";
    return 0;
}

int runPortfolioPositions(const core::Config& config, api::KalshiClient& client, int watch_interval) {
    if (int err = checkAuth(config, client)) return err;

    while (true) {
        // Clear screen if in watch mode
        if (watch_interval > 0) {
            std::cout << "\033[2J\033[H" << std::flush;
        }

        auto positions_result = client.getAllPositions();
        if (!positions_result.ok()) {
            std::cerr << "Error fetching positions: " << positions_result.error().message << "\n";
            if (watch_interval > 0) {
                std::this_thread::sleep_for(std::chrono::seconds(watch_interval));
                continue;
            }
            return 1;
        }

        std::vector<api::Position> open_positions;
        for (const auto& p : positions_result.value()) {
            if (p.position() != 0) {
                open_positions.push_back(p);
            }
        }

        if (open_positions.empty()) {
            std::cout << "No open positions\n";
            if (watch_interval > 0) {
                std::this_thread::sleep_for(std::chrono::seconds(watch_interval));
                continue;
            }
            return 0;
        }

        // Fetch current prices for all positions
        // States: open (has orderbook), pending (closed, no result), settled (has result)
        struct PriceInfo {
            double price_cents = 0;
            double yes_bid_cents = 0;
            double no_bid_cents = 0;
            bool has_yes_bid = false;
            bool has_no_bid = false;
            std::string status;   // "open", "pending", "settled"
            std::string result;   // "yes" or "no" if settled
        };
        std::unordered_map<std::string, PriceInfo> prices;

        for (const auto& p : open_positions) {
            PriceInfo info;
            info.status = "open";

            // First try orderbook
            auto ob_result = client.getOrderbook(p.ticker);
            if (ob_result.ok() && (!ob_result.value().yes.empty() || !ob_result.value().no.empty())) {
                const auto& ob = ob_result.value();
                // yes/no arrays are sorted ascending; back() = highest bid
                if (!ob.yes.empty()) {
                    info.yes_bid_cents = ob.yes.back().price_cents();
                    info.has_yes_bid = true;
                }
                if (!ob.no.empty()) {
                    info.no_bid_cents = ob.no.back().price_cents();
                    info.has_no_bid = true;
                }
                info.price_cents = info.yes_bid_cents;
                prices[p.ticker] = info;
                continue;
            }

            // Orderbook empty - check market status
            auto mkt_result = client.getMarket(p.ticker);
            if (mkt_result.ok()) {
                const auto& mkt = mkt_result.value();
                if (!mkt.result.empty()) {
                    // Settled - payout is known
                    info.status = "settled";
                    info.result = mkt.result;
                    info.price_cents = (mkt.result == "yes") ? 100.0 : 0.0;
                } else if (mkt.status == "closed") {
                    // Closed but awaiting settlement - predict based on last price
                    info.status = "pending";
                    double last_price = std::atof(mkt.last_price_dollars.c_str()) * 100.0;
                    // If last price > 50, assume YES wins (100), else NO wins (0)
                    info.price_cents = (last_price >= 50.0) ? 100.0 : 0.0;
                    info.result = (last_price >= 50.0) ? "yes" : "no";
                }
            }
            prices[p.ticker] = info;
        }

        std::cout << std::left
                  << std::setw(36) << "Ticker"
                  << std::right
                  << std::setw(5) << "Pos"
                  << std::setw(8) << "Win%"
                  << std::setw(10) << "Mkt Value"
                  << std::setw(10) << "P/L"
                  << std::setw(10) << "Fees"
                  << "\n";
        std::cout << std::string(79, '-') << "\n";

        double total_mkt_value = 0;
        double total_pl = 0;
        for (const auto& p : open_positions) {
            std::string ticker_display = p.ticker;
            if (ticker_display.size() > 34) {
                ticker_display = ticker_display.substr(0, 34);
            }

            double mkt_value = 0;
            std::string status = "open";
            std::string result;
            const PriceInfo* info = nullptr;
            auto it = prices.find(p.ticker);
            if (it != prices.end()) {
                mkt_value = p.position() * it->second.price_cents / 100.0;
                status = it->second.status;
                result = it->second.result;
                info = &it->second;
            }
            total_mkt_value += mkt_value;

            double exposure = p.exposure_cents() / 100.0;
            double diff = mkt_value - exposure;
            total_pl += diff;

            // Kalshi-style win probability: contra-side ask, i.e. 100 - contra-side bid.
            // YES holder: yes_ask = 100 - best no_bid. NO holder: no_ask = 100 - best yes_bid.
            char win_buf[16];
            if (status == "settled" || status == "pending") {
                bool position_won = (result == "yes" && p.position() > 0) ||
                                    (result == "no" && p.position() < 0);
                snprintf(win_buf, sizeof(win_buf), "%s", position_won ? "100%" : "0%");
            } else if (info && p.position() > 0 && info->has_no_bid) {
                snprintf(win_buf, sizeof(win_buf), "%.0f%%", 100.0 - info->no_bid_cents);
            } else if (info && p.position() < 0 && info->has_yes_bid) {
                snprintf(win_buf, sizeof(win_buf), "%.0f%%", 100.0 - info->yes_bid_cents);
            } else if (info && p.position() > 0 && info->has_yes_bid) {
                // Fallback: only same-side bid available
                snprintf(win_buf, sizeof(win_buf), "%.0f%%", info->yes_bid_cents);
            } else if (info && p.position() < 0 && info->has_no_bid) {
                snprintf(win_buf, sizeof(win_buf), "%.0f%%", info->no_bid_cents);
            } else {
                snprintf(win_buf, sizeof(win_buf), "-");
            }

            char mkt_buf[16], diff_buf[16], fee_buf[16];
            if (status == "settled") {
                snprintf(mkt_buf, sizeof(mkt_buf), "%s", result == "yes" ? "[WIN]" : "[LOSS]");
            } else if (status == "pending") {
                // Show predicted result based on last price
                snprintf(mkt_buf, sizeof(mkt_buf), "%s", result == "yes" ? "[WIN?]" : "[LOSS?]");
            } else {
                snprintf(mkt_buf, sizeof(mkt_buf), "$%.2f", mkt_value);
            }
            snprintf(diff_buf, sizeof(diff_buf), "%+.2f", diff);
            snprintf(fee_buf, sizeof(fee_buf), "$%.2f", p.fees_cents() / 100.0);

            // Color: pending/settled by result, open by P/L
            const char* color = "";
            const char* reset = "";
            if (status == "settled" || status == "pending") {
                // Dim + green for win, dim + red for loss
                if (result == "yes") {
                    color = "\033[2;32m";  // dim green
                } else {
                    color = "\033[2;31m";  // dim red
                }
                reset = "\033[0m";
            } else if (diff >= 1.0) {
                color = "\033[32m";  // green
                reset = "\033[0m";
            } else if (diff <= -1.0) {
                color = "\033[31m";  // red
                reset = "\033[0m";
            }

            std::cout << color
                      << std::left
                      << std::setw(36) << ticker_display
                      << std::right
                      << std::setw(5) << p.position()
                      << std::setw(8) << win_buf
                      << std::setw(10) << mkt_buf
                      << std::setw(10) << diff_buf
                      << std::setw(10) << fee_buf
                      << reset << "\n";
        }
        std::cout << std::string(79, '-') << "\n";

        // Fetch balance (includes Kalshi's portfolio valuation)
        double cash = 0, kalshi_value = 0;
        auto balance_result = client.getBalance();
        if (balance_result.ok()) {
            cash = balance_result.value().balance / 100.0;
            kalshi_value = balance_result.value().portfolio_value / 100.0;
        }

        char bid_buf[16], kalshi_buf[16], cash_buf[16], total_bid_buf[16], total_kalshi_buf[16], pl_buf[16];
        snprintf(bid_buf, sizeof(bid_buf), "$%.2f", total_mkt_value);
        snprintf(kalshi_buf, sizeof(kalshi_buf), "$%.2f", kalshi_value);
        snprintf(cash_buf, sizeof(cash_buf), "$%.2f", cash);
        snprintf(total_bid_buf, sizeof(total_bid_buf), "$%.2f", total_mkt_value + cash);
        snprintf(total_kalshi_buf, sizeof(total_kalshi_buf), "$%.2f", kalshi_value + cash);
        snprintf(pl_buf, sizeof(pl_buf), "%+.2f", total_pl);

        const char* pl_color = "";
        const char* pl_reset = "";
        if (total_pl >= 1.0) {
            pl_color = "\033[32m";
            pl_reset = "\033[0m";
        } else if (total_pl <= -1.0) {
            pl_color = "\033[31m";
            pl_reset = "\033[0m";
        }

        std::cout << open_positions.size() << " positions\n\n";
        std::cout << "  P/L:        " << pl_color << std::setw(10) << pl_buf << pl_reset << "\n";
        std::cout << "  Bid Value:  " << std::setw(10) << bid_buf << "\n";
        std::cout << "  Cash:       " << std::setw(10) << cash_buf << "\n";
        std::cout << "  ─────────────────────\n";
        std::cout << "  Total:      " << std::setw(10) << total_bid_buf << " - " << total_kalshi_buf << "\n";

        if (watch_interval <= 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(watch_interval));
    }
    return 0;
}

int runPortfolioSettlements(const core::Config& config, api::KalshiClient& client, int days) {
    if (int err = checkAuth(config, client)) return err;

    api::GetSettlementsParams params;
    auto now = std::time(nullptr);
    params.min_ts = now - days * 24 * 3600;
    params.limit = 100;

    auto settle_result = client.getSettlements(params);
    if (!settle_result.ok()) {
        std::cerr << "Error fetching settlements: " << settle_result.error().message << "\n";
        return 1;
    }
    const auto& settlements = settle_result.value().settlements;

    if (settlements.empty()) {
        std::cout << "No settlements in the last " << days << " days\n";
        return 0;
    }

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
    return 0;
}

int runFills(const core::Config& config, api::KalshiClient& client,
             const std::string& ticker, int limit, const std::string& format) {
    if (int err = checkAuth(config, client)) return err;

    api::GetFillsParams params;
    if (!ticker.empty()) params.ticker = ticker;
    params.limit = limit;

    auto result = client.getFills(params);
    if (!result.ok()) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
    }

    const auto& fills = result.value().fills;

    if (format == "json") {
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
    } else if (format == "csv") {
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
    return 0;
}

}  // namespace predibloom::cli
