#include "latency_algo.hpp"
#include "time_utils.hpp"
#include "../cli/bracket.hpp"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <map>

namespace predibloom::core {

LatencyAlgo::LatencyAlgo(const AlgoConfig& cfg)
    : BacktestAlgo(cfg)
{
}

std::pair<std::string, int> LatencyAlgo::findLatestAvailableCycle(
    const std::string& datetime_hour) const {
    // Parse the target datetime (YYYY-MM-DDTHH)
    if (datetime_hour.size() < 13) return {"", -1};

    std::string date = datetime_hour.substr(0, 10);
    int hour = std::stoi(datetime_hour.substr(11, 2));

    // Effective hour considering the cycle delay
    // If it's 23Z, then cycles available are those from 21Z or earlier availability
    // 21Z availability means 19Z cycle, 15Z availability means 13Z cycle, etc.
    int effective_hour = hour;

    // Try today's cycles first (in reverse order for latest)
    for (int i = 3; i >= 0; i--) {
        int cycle_hour = kCycleHours[i];
        int available_hour = cycle_hour + kCycleDelayHours;

        if (effective_hour >= available_hour) {
            return {date, cycle_hour};
        }
    }

    // No cycle from today, try yesterday's latest cycle (19Z)
    std::string yesterday = addDaysToDate(date, -1);
    if (!yesterday.empty()) {
        // Yesterday's 19Z is available at 21Z yesterday, which is before midnight today
        return {yesterday, 19};
    }

    return {"", -1};
}

std::string LatencyAlgo::computeLatencyEntryTime(
    const std::string& cycle_date, int cycle_hour) const {
    // Cycle becomes available at cycle_hour + kCycleDelayHours
    // Entry is latency_hours after that
    int available_hour = cycle_hour + kCycleDelayHours;
    int entry_hour = available_hour + config_.latency_hours;

    // Handle day overflow
    std::string entry_date = cycle_date;
    while (entry_hour >= 24) {
        entry_hour -= 24;
        entry_date = addDaysToDate(entry_date, 1);
        if (entry_date.empty()) return "";
    }

    char buf[14];
    snprintf(buf, sizeof(buf), "%sT%02d", entry_date.c_str(), entry_hour);
    return buf;
}

TradeDecision LatencyAlgo::evaluate(const TradeContext& ctx) {
    TradeDecision decision;
    decision.forecast_value = ctx.adjusted_forecast;

    // For latency algo, we need to determine the entry time based on cycle availability
    // The entry time is: (latest cycle availability time) + latency_hours

    // First, determine what datetime we're targeting for entry
    // Use series default entry hour as the baseline, but we'll adjust based on cycle
    int baseline_entry_hour = ctx.series->effectiveEntryHour();

    // Construct a baseline datetime to find which cycle would be used
    std::string baseline_datetime = computeEntryDatetime(
        ctx.date, ctx.series->entry_day_offset, baseline_entry_hour);

    // Find the latest available cycle at baseline time
    auto [cycle_date, cycle_hour] = findLatestAvailableCycle(baseline_datetime);

    if (cycle_date.empty()) {
        decision.skip_reason = SkipReason::NoCycleAvailable;
        return decision;
    }

    // Record which cycle we're using
    char cycle_buf[24];
    snprintf(cycle_buf, sizeof(cycle_buf), "%sT%02dZ", cycle_date.c_str(), cycle_hour);
    decision.cycle_used = cycle_buf;
    decision.latency_hours = config_.latency_hours;

    // Compute actual entry time based on cycle + latency
    std::string entry_target = computeLatencyEntryTime(cycle_date, cycle_hour);
    if (entry_target.empty()) {
        decision.skip_reason = SkipReason::NoCycleAvailable;
        return decision;
    }

    // Get forecast for this specific cycle (if available through getForecast)
    std::optional<double> cycle_forecast;
    if (ctx.getForecast) {
        cycle_forecast = ctx.getForecast(cycle_date, cycle_hour);
    }

    // Fall back to default forecast if cycle-specific not available
    double forecast = cycle_forecast.value_or(ctx.default_forecast.value_or(0));
    double adjusted = forecast + ctx.series->offset;
    decision.forecast_value = adjusted;

    if (!ctx.default_forecast && !cycle_forecast) {
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
        if (b.contains(adjusted)) {
            margin_from_edge = b.marginFrom(adjusted);
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

    // Find entry price from trades at our computed entry time
    double entry_price = -1;
    std::string entry_time;
    for (const auto& trade : *ctx.trades) {
        std::string trade_hour = trade.created_time.substr(0, 13);
        if (trade_hour <= entry_target && (entry_time.empty() || trade_hour > entry_time)) {
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

    // For latency algo, always hold to settlement (measuring raw forecast accuracy)
    decision.exit_time = "settlement";

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

void LatencyAlgo::printSummary(const std::vector<TradeDecision>& decisions,
                               int wins, int losses,
                               double total_pnl, double total_deployed) {
    // Group results by latency bucket (for sweep mode, latency varies per decision)
    std::map<int, std::vector<const TradeDecision*>> by_latency;

    for (const auto& d : decisions) {
        if (d.enter) {
            by_latency[d.latency_hours].push_back(&d);
        }
    }

    if (by_latency.size() <= 1) {
        // Single latency, no special summary needed
        return;
    }

    std::cout << "\n=== LATENCY ANALYSIS ===\n\n";
    std::cout << std::left
              << std::setw(10) << "Latency"
              << std::setw(8) << "Trades"
              << std::setw(8) << "Wins"
              << std::setw(10) << "WinRate"
              << std::setw(12) << "P&L"
              << std::setw(10) << "ROI"
              << "\n";
    std::cout << std::string(58, '-') << "\n";

    for (const auto& [latency, decisions_at_latency] : by_latency) {
        int bucket_wins = 0;
        double bucket_pnl = 0;
        double bucket_deployed = 0;

        for (const auto* d : decisions_at_latency) {
            // Need to recalculate P&L from decision data
            // This is approximate since we don't have market result here
            // In actual use, this would be called with enriched decisions
            bucket_deployed += (d->contracts * d->entry_price) / 100.0;
        }

        int trades = static_cast<int>(decisions_at_latency.size());
        double win_rate = trades > 0 ? (100.0 * bucket_wins / trades) : 0;
        double roi = bucket_deployed > 0 ? (100.0 * bucket_pnl / bucket_deployed) : 0;

        char pnl_buf[16], roi_buf[16];
        snprintf(pnl_buf, sizeof(pnl_buf), "$%+.2f", bucket_pnl);
        snprintf(roi_buf, sizeof(roi_buf), "%+.1f%%", roi);

        std::cout << std::left
                  << std::setw(10) << (std::to_string(latency) + "hr")
                  << std::setw(8) << trades
                  << std::setw(8) << bucket_wins
                  << std::setw(10) << (std::to_string(static_cast<int>(win_rate)) + "%")
                  << std::setw(12) << pnl_buf
                  << std::setw(10) << roi_buf
                  << "\n";
    }
}

}  // namespace predibloom::core
