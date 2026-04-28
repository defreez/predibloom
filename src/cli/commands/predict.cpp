#include "predict.hpp"
#include "../bracket.hpp"
#include "../../api/weather_client.hpp"
#include "../../api/gribstream_types.hpp"
#include "../../core/datetime.hpp"

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <map>
#include <vector>
#include <cstdio>
#include <thread>
#include <chrono>

namespace predibloom::cli {

namespace {

const std::map<std::string, std::string> MONTH_MAP = {
    {"01", "JAN"}, {"02", "FEB"}, {"03", "MAR"}, {"04", "APR"},
    {"05", "MAY"}, {"06", "JUN"}, {"07", "JUL"}, {"08", "AUG"},
    {"09", "SEP"}, {"10", "OCT"}, {"11", "NOV"}, {"12", "DEC"}
};

struct Prediction {
    std::string label;
    std::string series;
    double forecast;
    double adjusted;
    std::string strike;
    std::string ticker;
    double margin;
    double bid;
    double ask;
    bool tradeable;
    bool between_brackets;
    std::string peak;  // e.g., "4pm EDT (1pm PDT)" or "3pm PDT" for PT stations
    int64_t peak_utc_epoch = 0;  // 0 = unknown
    double peak_midnight_distance_hours = -1.0;  // -1 = unknown; otherwise [0, 12]
};

// Distance from local midnight to a HH:MM time string, in hours [0, 12].
// Returns -1 on parse failure or empty input.
double midnightDistanceHours(const std::string& hhmm_local) {
    if (hhmm_local.size() < 4) return -1.0;
    auto colon = hhmm_local.find(':');
    if (colon == std::string::npos) return -1.0;
    try {
        int hh = std::stoi(hhmm_local.substr(0, colon));
        int mm = std::stoi(hhmm_local.substr(colon + 1));
        if (hh < 0 || hh >= 24 || mm < 0 || mm >= 60) return -1.0;
        double t = hh + mm / 60.0;
        return std::min(t, 24.0 - t);
    } catch (...) {
        return -1.0;
    }
}

// Format peak time as "<station_local> (<pt>)" with the parens omitted when the
// station is itself in Pacific Time. Returns empty string on parse failure.
std::string formatPeakTime(const std::string& target_date,
                           const std::string& hhmm_local,
                           const std::string& station_tz) {
    if (hhmm_local.empty()) return "";
    auto utc = core::parseLocalDatetime(target_date, hhmm_local, station_tz);
    if (!utc) return "";

    std::string local_str = core::formatLocalAmPm(*utc, station_tz);
    std::string pt_str = core::formatLocalAmPm(*utc, "America/Los_Angeles");
    if (local_str.empty() || pt_str.empty()) return "";

    // Compare just the time portion (everything before the abbreviation) to
    // detect Pacific stations and avoid redundant "3pm PDT (3pm PDT)".
    auto strip_abbrev = [](const std::string& s) {
        auto sp = s.find(' ');
        return sp == std::string::npos ? s : s.substr(0, sp);
    };
    if (strip_abbrev(local_str) == strip_abbrev(pt_str)) {
        return local_str;
    }
    return local_str + " (" + pt_str + ")";
}

}  // namespace

int runPredict(const PredictOptions& opts,
               const core::Config& config,
               api::KalshiClient& client) {
    // Build list of series to process
    std::vector<const core::TrackedSeries*> series_list;
    if (!opts.series.empty()) {
        auto* sc = config.findSeries(opts.series);
        if (!sc || sc->latitude == 0) {
            std::cerr << "Series not configured or missing weather params: " << opts.series << "\n";
            return 1;
        }
        series_list.push_back(sc);
    } else {
        // Use all configured series with weather params
        for (const auto& tab : config.tabs) {
            for (const auto& sc : tab.series) {
                if (sc.latitude != 0 && !sc.nws_station.empty()) {
                    series_list.push_back(&sc);
                }
            }
        }
    }

    if (series_list.empty()) {
        std::cerr << "No series configured with weather params\n";
        return 1;
    }

    // Check if any series needs GribStream
    bool needs_gribstream = false;
    for (const auto* sc : series_list) {
        if (sc->weather_source == core::WeatherSource::GribStream) {
            needs_gribstream = true;
            break;
        }
    }
    if (needs_gribstream && !config.hasGribstream()) {
        std::cerr << "GribStream API token not configured. Add gribstream_api_token to ~/.config/predibloom/auth.json\n";
        return 1;
    }

    // Date parsing for event ticker matching
    std::string yy = opts.date.substr(2, 2);
    std::string mm = opts.date.substr(5, 2);
    std::string dd = opts.date.substr(8, 2);

    // Collect predictions for all series
    std::vector<Prediction> predictions;
    std::vector<std::pair<std::string, std::string>> failures;  // label, reason
    std::string most_recent_forecast_time;

    size_t total = series_list.size();
    size_t current = 0;

    for (const auto* series_config : series_list) {
        current++;
        std::cerr << "\rFetching " << current << "/" << total << ": "
                  << std::left << std::setw(20) << series_config->label << std::flush;

        double effective_offset = series_config->offset;

        // Create weather client for this series
        auto weather_client = api::WeatherClient::create(
            series_config->weather_source, config.gribstream_api_token);

        // Determine asOf time: use specified cycle or current time
        std::string as_of;
        if (!opts.cycle.empty()) {
            // User specified a cycle like "2026-04-25T19" meaning the 19Z cycle
            // NBM cycles become available ~2hr after their nominal time
            // So to get the 19Z cycle, we pretend it's 21Z (19 + 2hr delay)
            std::string cycle_str = opts.cycle;
            if (cycle_str.size() >= 13) {
                int cycle_hour = std::stoi(cycle_str.substr(11, 2));
                int avail_hour = cycle_hour + 2;  // Add delay
                std::string date_part = cycle_str.substr(0, 10);
                if (avail_hour >= 24) {
                    avail_hour -= 24;
                    date_part = core::addDaysToDate(date_part, 1);
                }
                char buf[24];
                snprintf(buf, sizeof(buf), "%sT%02d:00:00Z", date_part.c_str(), avail_hour);
                as_of = buf;
            }
        } else {
            as_of = core::currentUtcDatetimeHour() + ":00:00Z";
        }
        auto forecast_result = weather_client->getForecast(
            series_config->latitude, series_config->longitude, opts.date,
            series_config->timezone, as_of);

        if (!forecast_result.ok()) {
            failures.push_back({series_config->label, "forecast: " + forecast_result.error().message});
            continue;
        }

        // Track the most recent forecast issue time
        const auto& forecasted_at = forecast_result.value().forecasted_at;
        if (!forecasted_at.empty() && forecasted_at > most_recent_forecast_time) {
            most_recent_forecast_time = forecasted_at;
        }

        auto forecast_opt = series_config->isLowTemp()
            ? api::getMinTemperatureForDate(forecast_result.value(), opts.date)
            : api::getTemperatureForDate(forecast_result.value(), opts.date);
        if (!forecast_opt) {
            failures.push_back({series_config->label, "no temp for date"});
            continue;
        }

        double forecast = *forecast_opt;
        double adjusted = forecast + effective_offset;

        // Pull the local time of high/low and format both station-local and PT.
        const auto& times_local = series_config->isLowTemp()
            ? forecast_result.value().daily.time_of_min
            : forecast_result.value().daily.time_of_max;
        std::string peak;
        int64_t peak_utc_epoch = 0;
        double peak_midnight_dist = -1.0;
        if (!times_local.empty()) {
            peak = formatPeakTime(opts.date, times_local[0], series_config->timezone);
            auto utc = core::parseLocalDatetime(opts.date, times_local[0],
                                                  series_config->timezone);
            if (utc) peak_utc_epoch = utc->epoch();
            peak_midnight_dist = midnightDistanceHours(times_local[0]);
        }

        // Get markets (with delay to avoid rate limiting)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        api::GetMarketsParams params;
        params.series_ticker = series_config->series_ticker;
        auto markets_result = client.getAllMarkets(params);
        if (!markets_result.ok()) {
            failures.push_back({series_config->label, "markets: " + markets_result.error().message});
            continue;
        }

        std::string expected_event = series_config->series_ticker + "-" + yy + MONTH_MAP.at(mm) + dd;

        // Find markets for this date
        std::vector<api::Market> day_markets;
        for (const auto& market : markets_result.value()) {
            if (market.event_ticker == expected_event) {
                day_markets.push_back(market);
            }
        }

        if (day_markets.empty()) {
            failures.push_back({series_config->label, "no markets for " + expected_event});
            continue;
        }

        // Parse brackets and find target
        std::vector<Bracket> brackets;
        for (const auto& market : day_markets) {
            brackets.push_back(parseBracket(market));
        }

        // Find the bracket our adjusted forecast falls into
        const Bracket* target = nullptr;
        double margin_from_edge = 0;
        for (const auto& b : brackets) {
            if (b.contains(adjusted)) {
                margin_from_edge = b.marginFrom(adjusted);
                target = &b;
                break;
            }
        }

        Prediction p;
        p.label = series_config->label;
        p.series = series_config->series_ticker;
        p.forecast = forecast;
        p.adjusted = adjusted;
        p.peak = peak;
        p.peak_utc_epoch = peak_utc_epoch;
        p.peak_midnight_distance_hours = peak_midnight_dist;
        p.between_brackets = (target == nullptr);
        if (target) {
            p.strike = target->displayString();
            p.ticker = target->market->ticker;
            p.margin = margin_from_edge;
            p.bid = target->market->yes_bid_cents();
            p.ask = target->market->yes_ask_cents();
            // A peak that lands within `midnight_margin_hours` of midnight is
            // suspect — the actual extreme could fall on either calendar day,
            // so the bracket we landed in might be wrong. Refuse the trade.
            // Unknown peak (parse failure or empty forecast time) also fails
            // closed: we can't verify, so don't trade.
            bool peak_safe = (peak_midnight_dist >= opts.midnight_margin_hours);
            p.tradeable = (margin_from_edge >= opts.margin)
                       && (p.ask >= opts.min_price)
                       && (p.ask <= opts.max_price)
                       && peak_safe;
        } else {
            p.strike = "---";
            p.ticker = "";
            p.margin = 0;
            p.bid = 0;
            p.ask = 0;
            p.tradeable = false;
        }

        predictions.push_back(p);
    }

    // Clear progress line
    std::cerr << "\r" << std::string(50, ' ') << "\r";

    // Order rows so the soonest-upcoming peak is at the bottom of the table.
    // Tier 0 (top): rows with no parseable peak time.
    // Tier 1: peaks already in the past, oldest first.
    // Tier 2 (bottom): future peaks, latest first → soonest-future last.
    int64_t now_epoch = core::DateTime::now().epoch();
    auto sort_key = [&](const Prediction& p) -> std::tuple<int, int64_t> {
        if (p.peak_utc_epoch == 0) return {0, 0};
        if (p.peak_utc_epoch < now_epoch) return {1, p.peak_utc_epoch};
        return {2, -p.peak_utc_epoch};
    };
    std::stable_sort(predictions.begin(), predictions.end(),
                     [&](const Prediction& a, const Prediction& b) {
                         return sort_key(a) < sort_key(b);
                     });

    // Output results
    std::cout << "=== PREDICTIONS FOR " << opts.date << " ===\n";
    if (!opts.cycle.empty()) {
        std::cout << "Using cycle: " << opts.cycle << "\n";
    }
    if (!most_recent_forecast_time.empty()) {
        std::string formatted = core::formatUtcAsPtWithAge(most_recent_forecast_time);
        if (!formatted.empty()) {
            std::cout << "Forecast issued: " << formatted << "\n";
        } else {
            std::cout << "Forecast issued: " << most_recent_forecast_time << "\n";
        }
    }
    std::cout << "\n";

    std::cout << std::left
              << std::setw(18) << "City"
              << std::setw(10) << "Forecast"
              << std::setw(10) << "Adjusted"
              << std::setw(10) << "Bracket"
              << std::setw(22) << "Peak (local / PT)"
              << std::setw(8) << "Margin"
              << std::setw(8) << "Bid"
              << std::setw(8) << "Ask"
              << "Signal\n";
    std::cout << std::string(100, '-') << "\n";

    int tradeable_count = 0;
    for (const auto& p : predictions) {
        char forecast_buf[16], adjusted_buf[16], margin_buf[16];
        snprintf(forecast_buf, sizeof(forecast_buf), "%.1f°F", p.forecast);
        snprintf(adjusted_buf, sizeof(adjusted_buf), "%.1f°F", p.adjusted);

        std::cout << std::left
                  << std::setw(18) << p.label
                  << std::setw(10) << forecast_buf
                  << std::setw(10) << adjusted_buf
                  << std::setw(10) << p.strike
                  << std::setw(22) << (p.peak.empty() ? "-" : p.peak);

        if (p.between_brackets) {
            std::cout << std::setw(8) << "---"
                      << std::setw(8) << "---"
                      << std::setw(8) << "---"
                      << "BETWEEN";
        } else {
            snprintf(margin_buf, sizeof(margin_buf), "%.1f°F", p.margin);
            std::cout << std::setw(8) << margin_buf
                      << std::setw(8) << (std::to_string((int)p.bid) + "¢")
                      << std::setw(8) << (std::to_string((int)p.ask) + "¢");
            bool peak_near_midnight =
                (p.peak_midnight_distance_hours >= 0) &&
                (p.peak_midnight_distance_hours < opts.midnight_margin_hours);
            if (p.tradeable) {
                std::cout << "BUY";
                tradeable_count++;
            } else if (p.margin >= opts.margin
                    && p.ask >= opts.min_price
                    && p.ask <= opts.max_price
                    && peak_near_midnight) {
                std::cout << "EDGE";
            } else if (p.margin >= opts.margin && p.ask > opts.max_price) {
                std::cout << "EXPENSIVE";
            } else if (p.margin >= opts.margin && p.ask < opts.min_price) {
                std::cout << "SUS";
            } else {
                std::cout << "-";
            }
        }
        std::cout << "\n";
    }

    std::cout << std::string(100, '-') << "\n";
    std::cout << "Tradeable signals: " << tradeable_count << "/" << predictions.size()
              << " (margin >= " << opts.margin << "°F, ask "
              << (int)opts.min_price << "-" << (int)opts.max_price << "¢, peak "
              << opts.midnight_margin_hours << "h+ from midnight)\n";

    if (tradeable_count > 0) {
        std::cout << "\nTickers to buy:\n";
        for (const auto& p : predictions) {
            if (p.tradeable) {
                std::cout << "  " << p.ticker << "  " << p.label << " " << p.strike
                          << " @ " << (int)p.ask << "¢\n";
            }
        }
    }

    if (!failures.empty()) {
        std::cerr << "\nFailed (" << failures.size() << "):\n";
        for (const auto& [label, reason] : failures) {
            std::cerr << "  " << label << ": " << reason << "\n";
        }
    }

    return 0;
}

}  // namespace predibloom::cli
