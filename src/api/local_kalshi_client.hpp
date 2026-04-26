#pragma once

#include "kalshi_client.hpp"
#include "kalshi_db.hpp"
#include "result.hpp"
#include <memory>
#include <string>

namespace predibloom::api {

// Client for reading Kalshi data from the local SQLite cache.
// Provides the same interface as KalshiClient for backtest compatibility.
// Data must be pre-populated via `kalshi sync` command.
class LocalKalshiClient {
public:
    LocalKalshiClient();
    explicit LocalKalshiClient(const std::string& db_path);
    ~LocalKalshiClient();

    LocalKalshiClient(const LocalKalshiClient&) = delete;
    LocalKalshiClient& operator=(const LocalKalshiClient&) = delete;

    // --- Read operations (same interface as KalshiClient) ---

    // Get all markets matching params. Only series_ticker filter is supported.
    Result<std::vector<Market>> getAllMarkets(const GetMarketsParams& params = {});

    // Get all trades for a market ticker.
    Result<std::vector<Trade>> getAllTrades(const std::string& ticker);

    // --- Sync operations (pull from remote KalshiClient) ---

    // Sync all markets for a series from remote API.
    Result<SyncStats> syncMarkets(KalshiClient& remote, const std::string& series_ticker);

    // Sync all trades for a market ticker from remote API.
    Result<SyncStats> syncTrades(KalshiClient& remote, const std::string& ticker);

    // Sync all trades for all markets in a series.
    Result<SyncStats> syncAllTrades(KalshiClient& remote, const std::string& series_ticker);

    // --- Stats ---

    // Get sync status for all series.
    std::vector<KalshiDb::SeriesStats> getSeriesStats();

    // Check if database is open.
    bool is_open() const;

    // No-op for interface compatibility.
    void setCaching(bool /*enabled*/) {}

private:
    std::unique_ptr<KalshiDb> db_;
};

}  // namespace predibloom::api
