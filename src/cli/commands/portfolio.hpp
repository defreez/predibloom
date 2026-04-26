#pragma once

#include "../../api/kalshi_client.hpp"
#include "../../core/config.hpp"

namespace predibloom::cli {

int runPortfolioBalance(const core::Config& config, api::KalshiClient& client);
int runPortfolioPositions(const core::Config& config, api::KalshiClient& client, int watch_interval = 0);
int runPortfolioSettlements(const core::Config& config, api::KalshiClient& client, int days);
int runFills(const core::Config& config, api::KalshiClient& client,
             const std::string& ticker, int limit, const std::string& format);

}  // namespace predibloom::cli
