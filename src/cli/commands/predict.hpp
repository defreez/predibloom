#pragma once

#include "../../api/kalshi_client.hpp"
#include "../../core/config.hpp"
#include <string>

namespace predibloom::cli {

struct PredictOptions {
    std::string series;      // Optional series ticker filter
    std::string date;        // Date to predict (YYYY-MM-DD)
    double margin = 2.0;     // Min margin from bracket edge (°F)
    double min_price = 5.0;  // Min price to consider (cents)
    double max_price = 40.0; // Max price to pay (cents)
};

// Run the predict command
// Returns 0 on success, non-zero on error
int runPredict(const PredictOptions& opts,
               const core::Config& config,
               api::KalshiClient& client);

}  // namespace predibloom::cli
