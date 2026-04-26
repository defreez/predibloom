#pragma once

#include "config.hpp"
#include "../api/types.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace predibloom::core {

// Context passed to algo for each trading day
struct TradeContext {
    std::string date;                              // Settlement date YYYY-MM-DD
    const TrackedSeries* series = nullptr;         // Series config
    const std::vector<api::Market>* markets = nullptr;  // Day's brackets
    const std::vector<api::Trade>* trades = nullptr;    // Historical trades for selected bracket
    int nws_actual = 0;                            // Actual temperature (for analysis)

    // Forecast access - algo can request any cycle
    // Returns temperature forecast for the settlement date given a specific NBM cycle
    std::function<std::optional<double>(const std::string& cycle_date, int cycle_hour)> getForecast;

    // Default forecast (using configured entry time)
    std::optional<double> default_forecast;
    double adjusted_forecast = 0;                  // default_forecast + offset
};

// Output from algo evaluation
struct TradeDecision {
    bool enter = false;
    std::string entry_time;                        // When to enter (YYYY-MM-DDTHH)
    std::string ticker;                            // Which bracket
    std::string strike;                            // Display string for bracket (e.g., "70-71")
    int contracts = 0;
    double entry_price = 0;                        // Cents
    double exit_price = 0;                         // Cents (for early exit)
    std::string exit_time;                         // "settlement" = hold to settlement

    // Metadata for analysis
    std::string cycle_used;                        // e.g., "2026-04-24T19Z"
    int latency_hours = 0;                         // Hours after cycle availability
    double forecast_value = 0;
    double confidence = 0;
    double margin_from_edge = 0;
    bool is_bounded = false;                       // Bracket has both floor and cap

    // Skip reason (if not entering)
    std::string skip_reason;
};

// Skip reason codes
namespace SkipReason {
    constexpr const char* None = "";
    constexpr const char* NoForecast = "no_forecast";
    constexpr const char* BetweenBrackets = "between_brackets";
    constexpr const char* MarginTooSmall = "margin_too_small";
    constexpr const char* NoTradesAtEntry = "no_trades_at_entry";
    constexpr const char* NoTradesAtExit = "no_trades_at_exit";
    constexpr const char* PriceTooHigh = "price_too_high";
    constexpr const char* PriceTooLow = "price_too_low";
    constexpr const char* NoCycleAvailable = "no_cycle_available";
}

// Algo configuration
struct AlgoConfig {
    double margin = 0.0;                           // Min margin from bracket edge
    double min_price = 5.0;                        // Min entry price (cents)
    double max_price = 40.0;                       // Max entry price (cents)
    int entry_hour = -1;                           // UTC hour for entry (-1 = use series default)
    int exit_hour = -1;                            // UTC hour for exit (-1 = hold to settlement)
    double trade_size = 0;                         // Dollars per trade (0 = 10x margin)
    int jitter = 0;                                // Entry jitter +/- hours
    int seed = -1;                                 // RNG seed for jitter

    // Latency algo specific
    int latency_hours = 0;                         // Hours after cycle availability to enter
    std::vector<int> latency_sweep;                // Multiple latencies to test
};

// Base class for backtest algorithms
class BacktestAlgo {
public:
    virtual ~BacktestAlgo() = default;

    // Human-readable name
    virtual std::string name() const = 0;

    // Evaluate a single day and return trading decision
    virtual TradeDecision evaluate(const TradeContext& ctx) = 0;

    // Called after all days processed - opportunity for custom summary
    virtual void printSummary(const std::vector<TradeDecision>& decisions,
                              int wins, int losses,
                              double total_pnl, double total_deployed) {}

    // Access to config
    const AlgoConfig& config() const { return config_; }

protected:
    AlgoConfig config_;

    explicit BacktestAlgo(const AlgoConfig& cfg) : config_(cfg) {}
};

// Factory function to create algo by name
std::unique_ptr<BacktestAlgo> createAlgo(const std::string& name, const AlgoConfig& config);

}  // namespace predibloom::core
