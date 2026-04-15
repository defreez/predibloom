#include "kalshi_client.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sstream>

namespace predibloom::api {

namespace {
constexpr const char* API_HOST = "api.elections.kalshi.com";
constexpr const char* API_BASE = "/trade-api/v2";
}

KalshiClient::KalshiClient()
    : client_(std::make_unique<httplib::SSLClient>(API_HOST, 443))
    , rate_limiter_(10) {
    client_->set_connection_timeout(10);
    client_->set_read_timeout(30);
    client_->set_write_timeout(10);
}

KalshiClient::~KalshiClient() = default;

std::string KalshiClient::buildQueryString(const GetEventsParams& params) {
    std::ostringstream qs;
    bool first = true;

    auto append = [&](const char* key, const std::string& value) {
        qs << (first ? "?" : "&") << key << "=" << value;
        first = false;
    };

    if (params.status) append("status", *params.status);
    if (params.series_ticker) append("series_ticker", *params.series_ticker);
    if (params.cursor) append("cursor", *params.cursor);
    if (params.limit) append("limit", std::to_string(*params.limit));

    return qs.str();
}

std::string KalshiClient::buildQueryString(const GetMarketsParams& params) {
    std::ostringstream qs;
    bool first = true;

    auto append = [&](const char* key, const std::string& value) {
        qs << (first ? "?" : "&") << key << "=" << value;
        first = false;
    };

    if (params.status) append("status", *params.status);
    if (params.event_ticker) append("event_ticker", *params.event_ticker);
    if (params.series_ticker) append("series_ticker", *params.series_ticker);
    if (params.ticker) append("ticker", *params.ticker);
    if (params.cursor) append("cursor", *params.cursor);
    if (params.limit) append("limit", std::to_string(*params.limit));

    return qs.str();
}

Result<EventsResponse> KalshiClient::getEvents(const GetEventsParams& params) {
    rate_limiter_.wait_for_token();

    std::string path = std::string(API_BASE) + "/events" + buildQueryString(params);
    auto res = client_->Get(path);

    if (!res) {
        return Error(ApiError::NetworkError,
            "Network error: " + httplib::to_string(res.error()));
    }

    if (res->status == 429) {
        return Error(ApiError::RateLimitError, "Rate limit exceeded", 429);
    }

    if (res->status != 200) {
        return Error(ApiError::HttpError,
            "HTTP error: " + std::to_string(res->status), res->status);
    }

    try {
        auto json = nlohmann::json::parse(res->body);
        return json.get<EventsResponse>();
    } catch (const nlohmann::json::exception& e) {
        return Error(ApiError::ParseError,
            std::string("JSON parse error: ") + e.what());
    }
}

Result<MarketsResponse> KalshiClient::getMarkets(const GetMarketsParams& params) {
    rate_limiter_.wait_for_token();

    std::string path = std::string(API_BASE) + "/markets" + buildQueryString(params);
    auto res = client_->Get(path);

    if (!res) {
        return Error(ApiError::NetworkError,
            "Network error: " + httplib::to_string(res.error()));
    }

    if (res->status == 429) {
        return Error(ApiError::RateLimitError, "Rate limit exceeded", 429);
    }

    if (res->status != 200) {
        return Error(ApiError::HttpError,
            "HTTP error: " + std::to_string(res->status), res->status);
    }

    try {
        auto json = nlohmann::json::parse(res->body);
        return json.get<MarketsResponse>();
    } catch (const nlohmann::json::exception& e) {
        return Error(ApiError::ParseError,
            std::string("JSON parse error: ") + e.what());
    }
}

Result<Orderbook> KalshiClient::getOrderbook(const std::string& ticker, int depth) {
    rate_limiter_.wait_for_token();

    std::string path = std::string(API_BASE) + "/markets/" + ticker + "/orderbook";
    if (depth > 0) {
        path += "?depth=" + std::to_string(depth);
    }

    auto res = client_->Get(path);

    if (!res) {
        return Error(ApiError::NetworkError,
            "Network error: " + httplib::to_string(res.error()));
    }

    if (res->status == 429) {
        return Error(ApiError::RateLimitError, "Rate limit exceeded", 429);
    }

    if (res->status != 200) {
        return Error(ApiError::HttpError,
            "HTTP error: " + std::to_string(res->status), res->status);
    }

    try {
        auto json = nlohmann::json::parse(res->body);
        auto response = json.get<OrderbookResponse>();
        return response.orderbook;
    } catch (const nlohmann::json::exception& e) {
        return Error(ApiError::ParseError,
            std::string("JSON parse error: ") + e.what());
    }
}

Result<std::vector<Market>> KalshiClient::getAllMarkets(const GetMarketsParams& params) {
    std::vector<Market> all_markets;
    GetMarketsParams current_params = params;

    while (true) {
        auto result = getMarkets(current_params);
        if (!result.ok()) {
            return result.error();
        }

        auto& response = result.value();
        all_markets.insert(all_markets.end(),
            response.markets.begin(), response.markets.end());

        if (!response.has_more()) {
            break;
        }

        current_params.cursor = response.cursor;
    }

    return all_markets;
}

} // namespace predibloom::api
