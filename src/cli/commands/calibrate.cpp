#include "calibrate.hpp"
#include "../../api/gribstream_client.hpp"
#include "../../api/gribstream_types.hpp"
#include "../../api/nws_client.hpp"
#include "../../core/time_utils.hpp"

#include <iostream>
#include <iomanip>
#include <map>
#include <vector>
#include <cmath>
#include <cstdio>

namespace predibloom::cli {

int runCalibrate(const CalibrateOptions& opts, const core::Config& config) {
    // Build series list
    std::vector<std::string> series_list = opts.series;
    if (series_list.empty()) {
        for (const auto& tab : config.tabs) {
            for (const auto& sc : tab.series) {
                if (sc.latitude != 0 && !sc.nws_station.empty()) {
                    series_list.push_back(sc.series_ticker);
                }
            }
        }
        if (series_list.empty()) {
            std::cerr << "No series with weather params configured\n";
            return 1;
        }
    }

    // Validate all series
    for (const auto& series : series_list) {
        auto* sc = config.findSeries(series);
        if (!sc || sc->nws_station.empty()) {
            std::cerr << "Series not configured or missing weather params: " << series << "\n";
            return 1;
        }
    }

    if (!config.hasGribstream()) {
        std::cerr << "GribStream API token not configured\n";
        return 1;
    }

    api::GribStreamClient gribstream(config.gribstream_api_token);
    api::NwsClient nws;
    gribstream.setCaching(true);
    nws.setCaching(true);

    struct SeriesSummary {
        std::string label;
        double current_offset;
        int count;
        double mean_offset;
        double mae;
        double rmse;
    };
    std::vector<SeriesSummary> summaries;

    for (const auto& current_series : series_list) {
        auto* series_config = config.findSeries(current_series);

        std::cerr << "Calibrating " << series_config->label
                  << " (" << current_series << ", station " << series_config->nws_station << ")...\n";

        // Fetch NWS actual data
        bool is_low = series_config->isLowTemp();
        int start_year = std::stoi(opts.start_date.substr(0, 4));
        int end_year = std::stoi(opts.end_date.substr(0, 4));
        std::map<std::string, int> nws_temps;

        for (int year = start_year; year <= end_year; year++) {
            auto nws_result = nws.getCliData(series_config->nws_station, year);
            if (nws_result.ok()) {
                for (const auto& obs : nws_result.value()) {
                    if (obs.date >= opts.start_date && obs.date <= opts.end_date) {
                        nws_temps[obs.date] = is_low ? obs.low : obs.high;
                    }
                }
            } else {
                std::cerr << "  WARNING: NWS data fetch failed for year " << year << "\n";
            }
        }

        if (nws_temps.empty()) {
            std::cerr << "  ERROR: No NWS data available, skipping\n";
            continue;
        }

        struct DayComparison {
            std::string date;
            double forecast;
            int nws_actual;
            double error;
        };
        std::vector<DayComparison> days;

        int effective_hour = (opts.entry_hour >= 0)
            ? opts.entry_hour : series_config->effectiveEntryHour();

        for (const auto& [date, nws_actual] : nws_temps) {
            std::string as_of = core::computeAsOfIso(
                date, series_config->entry_day_offset, effective_hour);
            auto forecast_result = gribstream.getForecast(
                series_config->latitude, series_config->longitude, date, as_of);
            if (!forecast_result.ok()) continue;

            auto forecast_opt = is_low
                ? api::getMinTemperatureForDate(forecast_result.value(), date)
                : api::getTemperatureForDate(forecast_result.value(), date);
            if (!forecast_opt) continue;

            DayComparison d;
            d.date = date;
            d.forecast = *forecast_opt;
            d.nws_actual = nws_actual;
            d.error = static_cast<double>(d.nws_actual) - d.forecast;
            days.push_back(d);
        }

        if (days.empty()) {
            std::cerr << "  No matching days found\n";
            continue;
        }

        // Compute stats
        double sum_error = 0, sum_abs_error = 0, sum_sq_error = 0;
        for (const auto& d : days) {
            sum_error += d.error;
            sum_abs_error += std::abs(d.error);
            sum_sq_error += d.error * d.error;
        }
        int n = static_cast<int>(days.size());
        double mean_offset = sum_error / n;
        double mae = sum_abs_error / n;
        double rmse = std::sqrt(sum_sq_error / n);

        // Print per-day table
        std::cout << "\n=== " << series_config->label << " (" << current_series
                  << ", station " << series_config->nws_station << ") ===\n\n";

        std::cout << std::left << std::setw(12) << "Date"
                  << std::right << std::setw(10) << "Forecast"
                  << std::setw(10) << "NWS"
                  << std::setw(10) << "Error"
                  << "\n";
        std::cout << std::string(42, '-') << "\n";

        for (const auto& d : days) {
            char forecast_buf[16], error_buf[16];
            snprintf(forecast_buf, sizeof(forecast_buf), "%.1f", d.forecast);
            snprintf(error_buf, sizeof(error_buf), "%+.1f", d.error);

            std::cout << std::left << std::setw(12) << d.date
                      << std::right << std::setw(10) << forecast_buf
                      << std::setw(10) << d.nws_actual
                      << std::setw(10) << error_buf
                      << "\n";
        }

        std::cout << std::string(42, '-') << "\n";
        char offset_str[16], current_str[16];
        snprintf(offset_str, sizeof(offset_str), "%+.1f", mean_offset);
        snprintf(current_str, sizeof(current_str), "%+.1f", series_config->offset);

        std::cout << "\n  Days compared: " << n << "\n";
        std::cout << "  Mean offset (recommended): " << offset_str << " F\n";
        std::cout << "  MAE:  " << std::fixed << std::setprecision(1) << mae << " F\n";
        std::cout << "  RMSE: " << std::fixed << std::setprecision(1) << rmse << " F\n";
        std::cout << "  Current config offset: " << current_str << " F\n";

        summaries.push_back({series_config->label, series_config->offset, n, mean_offset, mae, rmse});
    }

    // Multi-series summary table
    if (summaries.size() > 1) {
        std::cout << "\n\n=== CALIBRATION SUMMARY ===\n\n";

        std::cout << std::left << std::setw(18) << "City"
                  << std::right << std::setw(6) << "Days"
                  << std::setw(10) << "Offset"
                  << std::setw(8) << "MAE"
                  << std::setw(8) << "RMSE"
                  << std::setw(10) << "Current"
                  << std::setw(8) << "Delta"
                  << "\n";
        std::cout << std::string(68, '-') << "\n";

        for (const auto& s : summaries) {
            double delta = s.mean_offset - s.current_offset;
            char offset_buf[16], mae_buf[16], rmse_buf[16], current_buf[16], delta_buf[16];
            snprintf(offset_buf, sizeof(offset_buf), "%+.1f", s.mean_offset);
            snprintf(mae_buf, sizeof(mae_buf), "%.1f", s.mae);
            snprintf(rmse_buf, sizeof(rmse_buf), "%.1f", s.rmse);
            snprintf(current_buf, sizeof(current_buf), "%+.1f", s.current_offset);
            snprintf(delta_buf, sizeof(delta_buf), "%+.1f", delta);

            std::cout << std::left << std::setw(18) << s.label
                      << std::right << std::setw(6) << s.count
                      << std::setw(10) << offset_buf
                      << std::setw(8) << mae_buf
                      << std::setw(8) << rmse_buf
                      << std::setw(10) << current_buf
                      << std::setw(8) << delta_buf
                      << "\n";
        }

        std::cout << std::string(68, '-') << "\n";
        std::cout << "\nOffset = mean(NWS_actual - GribStream_forecast)\n";
        std::cout << "Delta = recommended offset - current config offset\n";
    }

    return 0;
}

}  // namespace predibloom::cli
