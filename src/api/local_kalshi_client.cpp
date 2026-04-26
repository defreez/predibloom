#include "local_kalshi_client.hpp"

#include <iostream>

namespace predibloom::api {

LocalKalshiClient::LocalKalshiClient() {
    try {
        db_ = std::make_unique<KalshiDb>();
    } catch (const std::exception&) {
        // Database open failed; leave db_ null.
    }
}

LocalKalshiClient::LocalKalshiClient(const std::string& db_path) {
    try {
        db_ = std::make_unique<KalshiDb>(db_path);
    } catch (const std::exception&) {
        // Database open failed; leave db_ null.
    }
}

LocalKalshiClient::~LocalKalshiClient() = default;

bool LocalKalshiClient::is_open() const {
    return db_ && db_->is_open();
}

Result<std::vector<Market>> LocalKalshiClient::getAllMarkets(const GetMarketsParams& params) {
    if (!db_ || !db_->is_open()) {
        return Error(ApiError::NetworkError, "Local Kalshi database not available");
    }

    if (!params.series_ticker.has_value()) {
        return Error(ApiError::HttpError,
            "LocalKalshiClient requires series_ticker filter. "
            "Run 'kalshi sync --series <ticker>' first.");
    }

    auto markets = db_->getMarkets(params.series_ticker.value());
    if (markets.empty()) {
        return Error(ApiError::HttpError,
            "No markets found for series " + params.series_ticker.value() + ". "
            "Run 'kalshi sync --series " + params.series_ticker.value() + "' first.");
    }

    return markets;
}

Result<std::vector<Trade>> LocalKalshiClient::getAllTrades(const std::string& ticker) {
    if (!db_ || !db_->is_open()) {
        return Error(ApiError::NetworkError, "Local Kalshi database not available");
    }

    auto trades = db_->getTrades(ticker);
    // Empty trades is valid - some markets have no trades
    return trades;
}

Result<SyncStats> LocalKalshiClient::syncMarkets(KalshiClient& remote,
                                                   const std::string& series_ticker) {
    if (!db_ || !db_->is_open()) {
        return Error(ApiError::NetworkError, "Local Kalshi database not available");
    }

    // Fetch all markets from remote
    GetMarketsParams params;
    params.series_ticker = series_ticker;
    auto result = remote.getAllMarkets(params);

    if (!result.ok()) {
        return Error(result.error().type, result.error().message);
    }

    // Store in local database
    auto stats = db_->putMarkets(result.value(), series_ticker);
    return stats;
}

Result<SyncStats> LocalKalshiClient::syncTrades(KalshiClient& remote,
                                                  const std::string& ticker) {
    if (!db_ || !db_->is_open()) {
        return Error(ApiError::NetworkError, "Local Kalshi database not available");
    }

    // Get latest trade timestamp for incremental sync
    int64_t latest_ts = db_->getLatestTradeTs(ticker);

    // Fetch only new trades from remote
    auto result = remote.getTradesAfter(ticker, latest_ts);

    if (!result.ok()) {
        return Error(result.error().type, result.error().message);
    }

    // Store in local database
    auto stats = db_->putTrades(result.value());
    return stats;
}

Result<SyncStats> LocalKalshiClient::syncAllTrades(KalshiClient& remote,
                                                     const std::string& series_ticker) {
    if (!db_ || !db_->is_open()) {
        return Error(ApiError::NetworkError, "Local Kalshi database not available");
    }

    // Get all markets for the series
    auto markets = db_->getMarkets(series_ticker);
    if (markets.empty()) {
        return Error(ApiError::HttpError,
            "No markets found for series " + series_ticker + ". Sync markets first.");
    }

    SyncStats total_stats;
    for (const auto& market : markets) {
        auto result = syncTrades(remote, market.ticker);
        if (result.ok()) {
            total_stats.new_count += result.value().new_count;
            total_stats.updated_count += result.value().updated_count;
            total_stats.total_count += result.value().total_count;
        }
        // Continue even if individual market fails
    }

    return total_stats;
}

std::vector<KalshiDb::SeriesStats> LocalKalshiClient::getSeriesStats() {
    if (!db_ || !db_->is_open()) {
        return {};
    }
    return db_->getSeriesStats();
}

}  // namespace predibloom::api
