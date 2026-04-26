#pragma once

#include "../../api/kalshi_client.hpp"
#include "../../core/config.hpp"
#include <string>
#include <vector>

namespace predibloom::cli {

struct BacktestOptions {
    std::vector<std::string> series;
    std::string start_date;
    std::string end_date;
    double margin = 2.0;
    double min_price = 0.0;
    double max_price = 40.0;
    int entry_hour = -1;  // -1 = use per-series default
    int exit_hour = -1;   // -1 = hold to settlement
    double trade_size = 0; // 0 = 10x margin
    int jitter = 0;
    int seed = -1;

    // Algo selection
    std::string algo;                      // "simple" (default), "latency"
    int latency_hours = 0;                 // For latency algo: hours after cycle availability
    std::vector<int> latency_sweep;        // For latency sweep: test multiple latencies
};

int runBacktest(const BacktestOptions& opts,
                const core::Config& config,
                api::KalshiClient& client);

}  // namespace predibloom::cli
