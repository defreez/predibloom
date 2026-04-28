#include "simple_algo.hpp"
#include "datetime.hpp"
#include "../cli/bracket.hpp"

#include <algorithm>

namespace predibloom::core {

SimpleAlgo::SimpleAlgo(const AlgoConfig& cfg)
    : BacktestAlgo(cfg)
{
}

TradeDecision SimpleAlgo::evaluate(const TradeContext& ctx) {
    TradeDecision decision;
    decision.forecast_value = ctx.adjusted_forecast;

    // Check forecast availability
    if (!ctx.default_forecast) {
        decision.skip_reason = SkipReason::NoForecast;
        return decision;
    }

    // Parse brackets from markets
    std::vector<cli::Bracket> brackets;
    for (const auto& market : *ctx.markets) {
        brackets.push_back(cli::parseBracket(market));
    }

    // Find target bracket containing adjusted forecast
    const cli::Bracket* target = nullptr;
    double margin_from_edge = 0;
    for (const auto& b : brackets) {
        if (b.contains(ctx.adjusted_forecast)) {
            margin_from_edge = b.marginFrom(ctx.adjusted_forecast);
            target = &b;
            break;
        }
    }

    if (!target) {
        decision.skip_reason = SkipReason::BetweenBrackets;
        return decision;
    }

    if (margin_from_edge < config_.margin) {
        decision.skip_reason = SkipReason::MarginTooSmall;
        return decision;
    }

    decision.ticker = target->market->ticker;
    decision.strike = target->displayString();
    decision.margin_from_edge = margin_from_edge;
    decision.is_bounded = target->floor.has_value() && target->cap.has_value();

    // Compute entry time
    int effective_entry_hour = (config_.entry_hour >= 0)
        ? config_.entry_hour
        : ctx.series->effectiveEntryHour();
    std::string target_hour = computeEntryDatetime(
        ctx.date, ctx.series->entry_day_offset, effective_entry_hour);

    // Find entry price from trades
    double entry_price = -1;
    std::string entry_time;
    for (const auto& trade : *ctx.trades) {
        std::string trade_hour = trade.created_time.substr(0, 13);
        if (trade_hour <= target_hour && (entry_time.empty() || trade_hour > entry_time)) {
            entry_price = trade.yes_price_cents();
            entry_time = trade_hour;
        }
    }

    if (entry_price < 0) {
        decision.skip_reason = SkipReason::NoTradesAtEntry;
        return decision;
    }

    if (entry_price > config_.max_price) {
        decision.skip_reason = SkipReason::PriceTooHigh;
        return decision;
    }

    if (entry_price < config_.min_price) {
        decision.skip_reason = SkipReason::PriceTooLow;
        return decision;
    }

    decision.entry_price = entry_price;
    decision.entry_time = entry_time;

    // Find exit price if early exit configured
    if (config_.exit_hour >= 0) {
        char exit_buf[3];
        snprintf(exit_buf, sizeof(exit_buf), "%02d", config_.exit_hour);
        std::string exit_target_hour = ctx.date + "T" + exit_buf;

        double exit_price = -1;
        std::string exit_time_str;
        for (const auto& trade : *ctx.trades) {
            std::string trade_hour = trade.created_time.substr(0, 13);
            if (trade_hour <= exit_target_hour && (exit_time_str.empty() || trade_hour > exit_time_str)) {
                exit_price = trade.yes_price_cents();
                exit_time_str = trade_hour;
            }
        }

        if (exit_price < 0) {
            decision.skip_reason = SkipReason::NoTradesAtExit;
            return decision;
        }

        decision.exit_price = exit_price;
        decision.exit_time = exit_time_str;
    } else {
        decision.exit_time = "settlement";
    }

    // Calculate trade size and contracts
    double trade_size = (config_.trade_size > 0)
        ? config_.trade_size
        : (10.0 * margin_from_edge);
    int contracts = static_cast<int>((trade_size * 100) / entry_price);
    if (contracts < 1) contracts = 1;

    decision.enter = true;
    decision.contracts = contracts;
    decision.confidence = margin_from_edge;

    return decision;
}

}  // namespace predibloom::core
