#pragma once

#include "../../api/kalshi_client.hpp"
#include "../../core/config.hpp"
#include <string>
#include <vector>

namespace predibloom::cli {

// Sync markets and trades for configured series from Kalshi API.
// If series is empty, syncs all configured series.
int runKalshiSync(const core::Config& config,
                  api::KalshiClient& client,
                  const std::vector<std::string>& series,
                  bool skip_trades);

// List synced Kalshi data.
int runKalshiList(const std::string& series,
                  const std::string& format);

// Show sync status for all series.
int runKalshiStatus(const std::string& format);

}  // namespace predibloom::cli
