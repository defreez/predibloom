#include "backtest_algo.hpp"
#include "simple_algo.hpp"
#include "latency_algo.hpp"

#include <stdexcept>

namespace predibloom::core {

std::unique_ptr<BacktestAlgo> createAlgo(const std::string& name, const AlgoConfig& config) {
    if (name.empty() || name == "simple") {
        return std::make_unique<SimpleAlgo>(config);
    } else if (name == "latency") {
        return std::make_unique<LatencyAlgo>(config);
    } else {
        throw std::invalid_argument("Unknown algo: " + name);
    }
}

}  // namespace predibloom::core
