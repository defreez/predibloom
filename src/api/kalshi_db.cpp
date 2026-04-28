#include "kalshi_db.hpp"

#include <sqlite3.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace predibloom::api {

namespace {

std::string default_db_path() {
    const char* home = std::getenv("HOME");
    if (!home) {
        throw std::runtime_error("HOME environment variable not set");
    }
    return std::string(home) + "/.local/share/predibloom/predibloom.db";
}

}  // namespace

KalshiDb::KalshiDb() {
    open(default_db_path());
}

KalshiDb::KalshiDb(const std::string& db_path) {
    open(db_path);
}

KalshiDb::~KalshiDb() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void KalshiDb::open(const std::string& path) {
    db_path_ = path;

    // Create parent directories if needed.
    std::filesystem::path p(path);
    std::filesystem::create_directories(p.parent_path());

    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "unknown error";
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error("Failed to open database: " + err);
    }

    create_schema();
}

void KalshiDb::create_schema() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS kalshi_markets (
            ticker TEXT PRIMARY KEY,
            event_ticker TEXT NOT NULL,
            series_ticker TEXT NOT NULL,
            market_type TEXT,
            title TEXT,
            subtitle TEXT,
            status TEXT,
            result TEXT,
            floor_strike INTEGER,
            cap_strike INTEGER,
            close_time TEXT,
            synced_at TEXT DEFAULT (datetime('now'))
        );

        CREATE INDEX IF NOT EXISTS idx_kalshi_markets_series
            ON kalshi_markets(series_ticker);
        CREATE INDEX IF NOT EXISTS idx_kalshi_markets_event
            ON kalshi_markets(event_ticker);

        CREATE TABLE IF NOT EXISTS kalshi_trades (
            trade_id TEXT PRIMARY KEY,
            ticker TEXT NOT NULL,
            created_time TEXT NOT NULL,
            yes_price_dollars TEXT,
            no_price_dollars TEXT,
            count_fp TEXT,
            taker_side TEXT,
            synced_at TEXT DEFAULT (datetime('now'))
        );

        CREATE INDEX IF NOT EXISTS idx_kalshi_trades_ticker
            ON kalshi_trades(ticker);
        CREATE INDEX IF NOT EXISTS idx_kalshi_trades_time
            ON kalshi_trades(created_time);
    )";

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "unknown error";
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to create Kalshi schema: " + err);
    }
}

std::vector<Market> KalshiDb::getMarkets(const std::string& series_ticker) {
    std::vector<Market> results;
    if (!db_) return results;

    const char* sql = R"(
        SELECT ticker, event_ticker, series_ticker, market_type, title, subtitle,
               status, result, floor_strike, cap_strike, close_time
        FROM kalshi_markets
        WHERE series_ticker = ?
        ORDER BY event_ticker, floor_strike;
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;

    sqlite3_bind_text(stmt, 1, series_ticker.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Market m;
        m.ticker = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        m.event_ticker = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        // series_ticker is column 2 but not stored in Market struct
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            m.market_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            m.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
            m.subtitle = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
            m.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
            m.result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            m.floor_strike = sqlite3_column_int(stmt, 8);
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
            m.cap_strike = sqlite3_column_int(stmt, 9);
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
            m.close_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        results.push_back(m);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<Market> KalshiDb::getMarketsByEvent(const std::string& event_ticker) {
    std::vector<Market> results;
    if (!db_) return results;

    const char* sql = R"(
        SELECT ticker, event_ticker, series_ticker, market_type, title, subtitle,
               status, result, floor_strike, cap_strike, close_time
        FROM kalshi_markets
        WHERE event_ticker = ?
        ORDER BY floor_strike;
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;

    sqlite3_bind_text(stmt, 1, event_ticker.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Market m;
        m.ticker = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        m.event_ticker = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            m.market_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            m.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
            m.subtitle = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
            m.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
            m.result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            m.floor_strike = sqlite3_column_int(stmt, 8);
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
            m.cap_strike = sqlite3_column_int(stmt, 9);
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
            m.close_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        results.push_back(m);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::optional<Market> KalshiDb::getMarket(const std::string& ticker) {
    if (!db_) return std::nullopt;

    const char* sql = R"(
        SELECT ticker, event_ticker, series_ticker, market_type, title, subtitle,
               status, result, floor_strike, cap_strike, close_time
        FROM kalshi_markets
        WHERE ticker = ?;
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return std::nullopt;

    sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Market> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Market m;
        m.ticker = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        m.event_ticker = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            m.market_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            m.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
            m.subtitle = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
            m.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
            m.result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            m.floor_strike = sqlite3_column_int(stmt, 8);
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
            m.cap_strike = sqlite3_column_int(stmt, 9);
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
            m.close_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        result = m;
    }

    sqlite3_finalize(stmt);
    return result;
}

bool KalshiDb::putMarket(const Market& market, const std::string& series_ticker) {
    if (!db_) return false;

    const char* sql = R"(
        INSERT OR REPLACE INTO kalshi_markets
            (ticker, event_ticker, series_ticker, market_type, title, subtitle,
             status, result, floor_strike, cap_strike, close_time, synced_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'));
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, market.ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, market.event_ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, series_ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, market.market_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, market.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, market.subtitle.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, market.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, market.result.c_str(), -1, SQLITE_TRANSIENT);

    if (market.floor_strike.has_value()) {
        sqlite3_bind_int(stmt, 9, market.floor_strike.value());
    } else {
        sqlite3_bind_null(stmt, 9);
    }

    if (market.cap_strike.has_value()) {
        sqlite3_bind_int(stmt, 10, market.cap_strike.value());
    } else {
        sqlite3_bind_null(stmt, 10);
    }

    sqlite3_bind_text(stmt, 11, market.close_time.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

SyncStats KalshiDb::putMarkets(const std::vector<Market>& markets, const std::string& series_ticker) {
    SyncStats stats;
    if (!db_ || markets.empty()) return stats;

    // Begin transaction for performance
    sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

    for (const auto& market : markets) {
        // Check if exists
        auto existing = getMarket(market.ticker);
        if (putMarket(market, series_ticker)) {
            stats.total_count++;
            if (existing.has_value()) {
                stats.updated_count++;
            } else {
                stats.new_count++;
            }
        }
    }

    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    return stats;
}

std::vector<Trade> KalshiDb::getTrades(const std::string& ticker) {
    std::vector<Trade> results;
    if (!db_) return results;

    const char* sql = R"(
        SELECT trade_id, ticker, created_time, yes_price_dollars, no_price_dollars,
               count_fp, taker_side
        FROM kalshi_trades
        WHERE ticker = ?
        ORDER BY created_time;
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;

    sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Trade t;
        t.trade_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        t.ticker = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        t.created_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            t.yes_price_dollars = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            t.no_price_dollars = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
            t.count_fp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
            t.taker_side = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        results.push_back(t);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<Trade> KalshiDb::getTrades(const std::string& ticker,
                                        const std::string& start_time,
                                        const std::string& end_time) {
    std::vector<Trade> results;
    if (!db_) return results;

    const char* sql = R"(
        SELECT trade_id, ticker, created_time, yes_price_dollars, no_price_dollars,
               count_fp, taker_side
        FROM kalshi_trades
        WHERE ticker = ? AND created_time >= ? AND created_time <= ?
        ORDER BY created_time;
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;

    sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, start_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, end_time.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Trade t;
        t.trade_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        t.ticker = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        t.created_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            t.yes_price_dollars = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            t.no_price_dollars = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
            t.count_fp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
            t.taker_side = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        results.push_back(t);
    }

    sqlite3_finalize(stmt);
    return results;
}

bool KalshiDb::putTrade(const Trade& trade) {
    if (!db_) return false;

    const char* sql = R"(
        INSERT OR REPLACE INTO kalshi_trades
            (trade_id, ticker, created_time, yes_price_dollars, no_price_dollars,
             count_fp, taker_side, synced_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now'));
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, trade.trade_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, trade.ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, trade.created_time.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, trade.yes_price_dollars.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, trade.no_price_dollars.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, trade.count_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, trade.taker_side.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

SyncStats KalshiDb::putTrades(const std::vector<Trade>& trades) {
    SyncStats stats;
    if (!db_ || trades.empty()) return stats;

    // Begin transaction for performance
    sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

    // Prepare statement once
    const char* check_sql = "SELECT 1 FROM kalshi_trades WHERE trade_id = ?";
    const char* insert_sql = R"(
        INSERT OR REPLACE INTO kalshi_trades
            (trade_id, ticker, created_time, yes_price_dollars, no_price_dollars,
             count_fp, taker_side, synced_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now'));
    )";

    sqlite3_stmt* check_stmt = nullptr;
    sqlite3_stmt* insert_stmt = nullptr;
    sqlite3_prepare_v2(db_, check_sql, -1, &check_stmt, nullptr);
    sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt, nullptr);

    for (const auto& trade : trades) {
        // Check if exists
        sqlite3_reset(check_stmt);
        sqlite3_bind_text(check_stmt, 1, trade.trade_id.c_str(), -1, SQLITE_TRANSIENT);
        bool exists = (sqlite3_step(check_stmt) == SQLITE_ROW);

        // Insert/update
        sqlite3_reset(insert_stmt);
        sqlite3_bind_text(insert_stmt, 1, trade.trade_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 2, trade.ticker.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 3, trade.created_time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 4, trade.yes_price_dollars.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 5, trade.no_price_dollars.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 6, trade.count_fp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 7, trade.taker_side.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(insert_stmt) == SQLITE_DONE) {
            stats.total_count++;
            if (exists) {
                stats.updated_count++;
            } else {
                stats.new_count++;
            }
        }
    }

    sqlite3_finalize(check_stmt);
    sqlite3_finalize(insert_stmt);
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);

    return stats;
}

int KalshiDb::countMarkets(const std::string& series_ticker) {
    if (!db_) return 0;

    const char* sql = "SELECT COUNT(*) FROM kalshi_markets WHERE series_ticker = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, series_ticker.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

int KalshiDb::countTrades(const std::string& series_ticker) {
    if (!db_) return 0;

    const char* sql = R"(
        SELECT COUNT(*) FROM kalshi_trades t
        JOIN kalshi_markets m ON t.ticker = m.ticker
        WHERE m.series_ticker = ?
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, series_ticker.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

int64_t KalshiDb::getLatestTradeTs(const std::string& ticker) {
    if (!db_) return 0;

    // created_time is ISO8601, e.g. "2026-04-25T14:30:00Z"
    // Convert to Unix timestamp using SQLite's strftime
    const char* sql = R"(
        SELECT MAX(strftime('%s', created_time))
        FROM kalshi_trades
        WHERE ticker = ?
    )";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);

    int64_t ts = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        ts = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return ts;
}

std::vector<KalshiDb::SeriesStats> KalshiDb::getSeriesStats() {
    std::vector<SeriesStats> results;
    if (!db_) return results;

    const char* sql = R"(
        SELECT
            m.series_ticker,
            COUNT(DISTINCT m.ticker) as market_count,
            COUNT(DISTINCT t.trade_id) as trade_count,
            MAX(m.synced_at) as last_sync
        FROM kalshi_markets m
        LEFT JOIN kalshi_trades t ON t.ticker = m.ticker
        GROUP BY m.series_ticker
        ORDER BY m.series_ticker;
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SeriesStats s;
        s.series_ticker = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        s.market_count = sqlite3_column_int(stmt, 1);
        s.trade_count = sqlite3_column_int(stmt, 2);
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            s.last_sync = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        }
        results.push_back(s);
    }

    sqlite3_finalize(stmt);
    return results;
}

}  // namespace predibloom::api
