#pragma once

#include "../../api/kalshi_client.hpp"
#include "../../core/config.hpp"
#include <string>

namespace predibloom::cli {

int runHistory(const std::string& series, const std::string& start_date,
               const std::string& end_date, api::KalshiClient& client);

int runWinners(const std::string& series, const std::string& start_date,
               const std::string& end_date, const core::Config& config,
               api::KalshiClient& client);

}  // namespace predibloom::cli
