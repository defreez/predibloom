#pragma once

#include "../api/kalshi_client.hpp"
#include "../api/result.hpp"
#include "../api/types.hpp"
#include <optional>
#include <string>
#include <vector>

namespace predibloom::core {

struct MarketFilter {
    std::optional<std::string> status;
    std::optional<std::string> event_ticker;
    std::optional<std::string> series_ticker;
    std::optional<std::string> ticker;
    std::optional<int> limit;
};

struct EventFilter {
    std::optional<std::string> status;
    std::optional<std::string> series_ticker;
    std::optional<int> limit;
};

class MarketService {
public:
    explicit MarketService(api::KalshiClient& client);

    // Both CLI and GUI use these exact methods
    api::Result<std::vector<api::Market>> listMarkets(const MarketFilter& filter = {});
    api::Result<std::vector<api::Event>> listEvents(const EventFilter& filter = {});
    api::Result<api::Orderbook> getOrderbook(const std::string& ticker, int depth = 0);
    api::Result<api::Market> getMarket(const std::string& ticker);

private:
    api::KalshiClient& client_;
};

} // namespace predibloom::core
