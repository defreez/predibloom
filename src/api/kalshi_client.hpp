#pragma once

#include "result.hpp"
#include "types.hpp"
#include "rate_limiter.hpp"
#include <memory>
#include <optional>

namespace httplib {
class SSLClient;
}

namespace predibloom::api {

class KalshiAuth;

struct GetEventsParams {
    std::optional<std::string> status;
    std::optional<std::string> series_ticker;
    std::optional<std::string> cursor;
    std::optional<int> limit;
};

struct GetMarketsParams {
    std::optional<std::string> status;
    std::optional<std::string> event_ticker;
    std::optional<std::string> series_ticker;
    std::optional<std::string> ticker;
    std::optional<std::string> cursor;
    std::optional<int> limit;
};

struct GetTradesParams {
    std::string ticker;
    std::optional<std::string> cursor;
    std::optional<int> limit;
};

struct GetFillsParams {
    std::optional<std::string> ticker;
    std::optional<int64_t> min_ts;
    std::optional<int64_t> max_ts;
    std::optional<int> limit;
    std::optional<std::string> cursor;
};

class KalshiClient {
public:
    KalshiClient();
    ~KalshiClient();

    KalshiClient(const KalshiClient&) = delete;
    KalshiClient& operator=(const KalshiClient&) = delete;

    // Set credentials for authenticated endpoints
    void setAuth(const std::string& api_key_id, const std::string& key_file);

    Result<EventsResponse> getEvents(const GetEventsParams& params = {});
    Result<MarketsResponse> getMarkets(const GetMarketsParams& params = {});
    Result<Orderbook> getOrderbook(const std::string& ticker, int depth = 0);
    Result<std::vector<Market>> getAllMarkets(const GetMarketsParams& params = {});
    Result<TradesResponse> getTrades(const GetTradesParams& params);
    Result<std::vector<Trade>> getAllTrades(const std::string& ticker);

    // Authenticated endpoints
    Result<FillsResponse> getFills(const GetFillsParams& params = {});
    Result<std::vector<Fill>> getAllFills(const GetFillsParams& params = {});

    void setCaching(bool enabled) { caching_ = enabled; }

private:
    std::string buildQueryString(const GetEventsParams& params);
    std::string buildQueryString(const GetMarketsParams& params);

    // Perform an authenticated GET request
    Result<std::string> authGet(const std::string& path);

    std::unique_ptr<httplib::SSLClient> client_;
    std::unique_ptr<KalshiAuth> auth_;
    RateLimiter rate_limiter_;
    bool caching_ = false;
};

} // namespace predibloom::api
