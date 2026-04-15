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

class KalshiClient {
public:
    KalshiClient();
    ~KalshiClient();

    KalshiClient(const KalshiClient&) = delete;
    KalshiClient& operator=(const KalshiClient&) = delete;

    Result<EventsResponse> getEvents(const GetEventsParams& params = {});
    Result<MarketsResponse> getMarkets(const GetMarketsParams& params = {});
    Result<Orderbook> getOrderbook(const std::string& ticker, int depth = 0);
    Result<std::vector<Market>> getAllMarkets(const GetMarketsParams& params = {});

private:
    std::string buildQueryString(const GetEventsParams& params);
    std::string buildQueryString(const GetMarketsParams& params);

    std::unique_ptr<httplib::SSLClient> client_;
    RateLimiter rate_limiter_;
};

} // namespace predibloom::api
