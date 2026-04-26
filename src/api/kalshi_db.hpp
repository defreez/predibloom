#pragma once

#include "types.hpp"
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace predibloom::api {

// Sync statistics returned by sync operations.
struct SyncStats {
    int new_count = 0;
    int updated_count = 0;
    int total_count = 0;
};

// SQLite database for cached Kalshi market and trade data.
// Shares the same database file as ForecastDb (~/.cache/predibloom/forecasts.db).
class KalshiDb {
public:
    KalshiDb();
    explicit KalshiDb(const std::string& db_path);
    ~KalshiDb();

    KalshiDb(const KalshiDb&) = delete;
    KalshiDb& operator=(const KalshiDb&) = delete;

    // --- Markets ---

    // Get all markets for a series ticker.
    std::vector<Market> getMarkets(const std::string& series_ticker);

    // Get all markets for an event ticker.
    std::vector<Market> getMarketsByEvent(const std::string& event_ticker);

    // Get a single market by ticker.
    std::optional<Market> getMarket(const std::string& ticker);

    // Insert or update a market.
    bool putMarket(const Market& market, const std::string& series_ticker);

    // Bulk insert/update markets.
    SyncStats putMarkets(const std::vector<Market>& markets, const std::string& series_ticker);

    // --- Trades ---

    // Get all trades for a market ticker.
    std::vector<Trade> getTrades(const std::string& ticker);

    // Get trades for a market ticker within a time range.
    std::vector<Trade> getTrades(const std::string& ticker,
                                  const std::string& start_time,
                                  const std::string& end_time);

    // Insert or update a trade.
    bool putTrade(const Trade& trade);

    // Bulk insert/update trades.
    SyncStats putTrades(const std::vector<Trade>& trades);

    // --- Stats ---

    // Get count of markets for a series.
    int countMarkets(const std::string& series_ticker);

    // Get count of trades for a series (all markets in series).
    int countTrades(const std::string& series_ticker);

    // Get the latest trade timestamp for a market (for incremental sync).
    // Returns 0 if no trades exist.
    int64_t getLatestTradeTs(const std::string& ticker);

    // Get list of synced series with counts.
    struct SeriesStats {
        std::string series_ticker;
        int market_count;
        int trade_count;
        std::string last_sync;
    };
    std::vector<SeriesStats> getSeriesStats();

    // Check if database is open and valid.
    bool is_open() const { return db_ != nullptr; }

    // Get the database path.
    const std::string& db_path() const { return db_path_; }

private:
    void open(const std::string& path);
    void create_schema();

    sqlite3* db_ = nullptr;
    std::string db_path_;
};

}  // namespace predibloom::api
