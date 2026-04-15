#include "service.hpp"

namespace predibloom::core {

MarketService::MarketService(api::KalshiClient& client)
    : client_(client) {}

api::Result<std::vector<api::Market>> MarketService::listMarkets(const MarketFilter& filter) {
    api::GetMarketsParams params;
    params.status = filter.status;
    params.event_ticker = filter.event_ticker;
    params.series_ticker = filter.series_ticker;
    params.ticker = filter.ticker;
    params.limit = filter.limit;

    auto result = client_.getMarkets(params);
    if (!result.ok()) {
        return result.error();
    }
    return result.value().markets;
}

api::Result<std::vector<api::Event>> MarketService::listEvents(const EventFilter& filter) {
    api::GetEventsParams params;
    params.status = filter.status;
    params.series_ticker = filter.series_ticker;
    params.limit = filter.limit;

    auto result = client_.getEvents(params);
    if (!result.ok()) {
        return result.error();
    }
    return result.value().events;
}

api::Result<api::Orderbook> MarketService::getOrderbook(const std::string& ticker, int depth) {
    return client_.getOrderbook(ticker, depth);
}

api::Result<api::Market> MarketService::getMarket(const std::string& ticker) {
    api::GetMarketsParams params;
    params.ticker = ticker;
    params.limit = 1;

    auto result = client_.getMarkets(params);
    if (!result.ok()) {
        return result.error();
    }

    if (result.value().markets.empty()) {
        return api::Error(api::ApiError::HttpError, "Market not found: " + ticker, 404);
    }

    return result.value().markets[0];
}

} // namespace predibloom::core
