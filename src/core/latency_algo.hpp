#pragma once

#include "backtest_algo.hpp"

namespace predibloom::core {

// LatencyAlgo: HFT latency arbitrage strategy
// Tests the thesis that NBM data takes time to get priced into Kalshi markets.
// Entry is N hours after the latest NBM cycle becomes available.
//
// NBM cycle availability:
// - Cycles run at 01Z, 07Z, 13Z, 19Z
// - Data becomes available ~2 hours after cycle time on NOAA S3
// - So 19Z cycle is available around 21Z
//
// This algo:
// - Determines which NBM cycle would be latest available at entry time
// - Enters latency_hours after that cycle becomes available
// - Tracks cycle used for post-hoc analysis of edge decay
class LatencyAlgo : public BacktestAlgo {
public:
    explicit LatencyAlgo(const AlgoConfig& cfg);

    std::string name() const override { return "latency"; }

    TradeDecision evaluate(const TradeContext& ctx) override;

    void printSummary(const std::vector<TradeDecision>& decisions,
                      int wins, int losses,
                      double total_pnl, double total_deployed) override;

private:
    // NBM cycles are available ~2 hours after their nominal time
    static constexpr int kCycleDelayHours = 2;

    // CONUS cycles: 01Z, 07Z, 13Z, 19Z
    static constexpr int kCycleHours[] = {1, 7, 13, 19};

    // Find the latest cycle available at a given UTC datetime
    // Returns cycle_date and cycle_hour, or nullopt if none available
    std::pair<std::string, int> findLatestAvailableCycle(
        const std::string& datetime_hour) const;

    // Compute entry datetime given cycle availability + latency
    std::string computeLatencyEntryTime(
        const std::string& cycle_date, int cycle_hour) const;
};

}  // namespace predibloom::core
