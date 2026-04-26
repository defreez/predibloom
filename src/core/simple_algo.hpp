#pragma once

#include "backtest_algo.hpp"
#include <random>

namespace predibloom::core {

// SimpleAlgo: The original backtest strategy
// - Fixed entry hour (from config or per-series default)
// - Buy bracket containing adjusted forecast
// - Hold to settlement or fixed exit hour
// - Trade size based on margin from bracket edge
class SimpleAlgo : public BacktestAlgo {
public:
    explicit SimpleAlgo(const AlgoConfig& cfg);

    std::string name() const override { return "simple"; }

    TradeDecision evaluate(const TradeContext& ctx) override;

private:
    std::mt19937 rng_;
    std::uniform_int_distribution<int> jitter_dist_;
};

}  // namespace predibloom::core
