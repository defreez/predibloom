#include "portfolio.hpp"
#include <iostream>
#include <iomanip>
#include <cstdio>
#include <cctype>
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

int runPortfolioPositions(const core::Config& config, api::KalshiClient& client) {
    if (int err = checkAuth(config, client)) return err;

    auto positions_result = client.getAllPositions();
    if (!positions_result.ok()) {
        std::cerr << "Error fetching positions: " << positions_result.error().message << "\n";
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
        return 0;
    }

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
