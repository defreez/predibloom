#pragma once

#include "../../api/kalshi_client.hpp"
#include "../../core/config.hpp"
#include <string>

namespace predibloom::cli {

// Series command - list configured series with entry hours
int runSeries(const core::Config& config);

// NBM download command - pre-download NBM data for backtesting
int runNbmDownload(const core::Config& config,
                   const std::string& start_date,
                   const std::string& end_date);

}  // namespace predibloom::cli
