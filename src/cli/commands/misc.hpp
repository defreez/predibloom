#pragma once

#include "../../api/kalshi_client.hpp"
#include "../../core/config.hpp"
#include <string>

namespace predibloom::cli {

// Series command - list configured series with entry hours
int runSeries(const core::Config& config);

}  // namespace predibloom::cli
