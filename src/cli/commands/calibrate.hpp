#pragma once

#include "../../core/config.hpp"
#include <string>
#include <vector>

namespace predibloom::cli {

struct CalibrateOptions {
    std::vector<std::string> series;
    std::string start_date;
    std::string end_date;
    int entry_hour = -1;  // -1 = use per-series default
};

int runCalibrate(const CalibrateOptions& opts, const core::Config& config);

}  // namespace predibloom::cli
