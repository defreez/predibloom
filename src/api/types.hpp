#pragma once

#include <string>
#include <vector>
#include <cstdlib>
#include <optional>
#include <nlohmann/json.hpp>

namespace predibloom::api {

struct Event {
    std::string event_ticker;
    std::string series_ticker;
    std::string title;
    std::string category;
    std::string sub_title;
    std::string status;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Event,
    event_ticker, series_ticker, title, category, sub_title, status)

struct Market {
    std::string ticker;
    std::string event_ticker;
    std::string market_type;
    std::string title;
    std::string subtitle;
    std::string status;
    std::string yes_bid_dollars;
    std::string yes_ask_dollars;
    std::string no_bid_dollars;
    std::string no_ask_dollars;
    std::string last_price_dollars;
    std::string volume_fp;
    std::string volume_24h_fp;

    // Settlement data
    std::string result;              // "yes" or "no" if settled
    std::optional<int> floor_strike; // Lower bound (e.g., temperature)
    std::optional<int> cap_strike;   // Upper bound
    std::string close_time;          // Settlement/close time ISO8601

    double yes_bid_cents() const {
        return std::atof(yes_bid_dollars.c_str()) * 100.0;
    }

    double yes_ask_cents() const {
        return std::atof(yes_ask_dollars.c_str()) * 100.0;
    }

    double no_bid_cents() const {
        return std::atof(no_bid_dollars.c_str()) * 100.0;
    }

    double no_ask_cents() const {
        return std::atof(no_ask_dollars.c_str()) * 100.0;
    }

    double last_price_cents() const {
        return std::atof(last_price_dollars.c_str()) * 100.0;
    }
};

inline void from_json(const nlohmann::json& j, Market& m) {
    if (j.contains("ticker")) j.at("ticker").get_to(m.ticker);
    if (j.contains("event_ticker")) j.at("event_ticker").get_to(m.event_ticker);
    if (j.contains("market_type")) j.at("market_type").get_to(m.market_type);
    if (j.contains("title")) j.at("title").get_to(m.title);
    if (j.contains("subtitle")) j.at("subtitle").get_to(m.subtitle);
    if (j.contains("status")) j.at("status").get_to(m.status);
    if (j.contains("yes_bid_dollars")) j.at("yes_bid_dollars").get_to(m.yes_bid_dollars);
    if (j.contains("yes_ask_dollars")) j.at("yes_ask_dollars").get_to(m.yes_ask_dollars);
    if (j.contains("no_bid_dollars")) j.at("no_bid_dollars").get_to(m.no_bid_dollars);
    if (j.contains("no_ask_dollars")) j.at("no_ask_dollars").get_to(m.no_ask_dollars);
    if (j.contains("last_price_dollars")) j.at("last_price_dollars").get_to(m.last_price_dollars);
    if (j.contains("volume_fp")) j.at("volume_fp").get_to(m.volume_fp);
    if (j.contains("volume_24h_fp")) j.at("volume_24h_fp").get_to(m.volume_24h_fp);
    if (j.contains("result")) j.at("result").get_to(m.result);
    if (j.contains("close_time")) j.at("close_time").get_to(m.close_time);

    // Handle nullable integer fields
    if (j.contains("floor_strike") && !j.at("floor_strike").is_null()) {
        m.floor_strike = j.at("floor_strike").get<int>();
    }
    if (j.contains("cap_strike") && !j.at("cap_strike").is_null()) {
        m.cap_strike = j.at("cap_strike").get<int>();
    }
}

struct OrderbookLevel {
    std::string price;
    std::string quantity;

    double price_cents() const {
        return std::atof(price.c_str()) * 100.0;
    }

    int quantity_int() const {
        return std::atoi(quantity.c_str());
    }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OrderbookLevel,
    price, quantity)

struct Orderbook {
    std::string ticker;
    std::vector<OrderbookLevel> yes;
    std::vector<OrderbookLevel> no;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Orderbook,
    ticker, yes, no)

template<typename T>
struct PaginatedResponse {
    std::vector<T> items;
    std::string cursor;

    bool has_more() const {
        return !cursor.empty();
    }
};

struct EventsResponse {
    std::vector<Event> events;
    std::string cursor;

    bool has_more() const {
        return !cursor.empty();
    }
};

inline void from_json(const nlohmann::json& j, EventsResponse& r) {
    if (j.contains("events")) {
        j.at("events").get_to(r.events);
    }
    if (j.contains("cursor") && !j.at("cursor").is_null()) {
        j.at("cursor").get_to(r.cursor);
    }
}

struct MarketsResponse {
    std::vector<Market> markets;
    std::string cursor;

    bool has_more() const {
        return !cursor.empty();
    }
};

inline void from_json(const nlohmann::json& j, MarketsResponse& r) {
    if (j.contains("markets")) {
        j.at("markets").get_to(r.markets);
    }
    if (j.contains("cursor") && !j.at("cursor").is_null()) {
        j.at("cursor").get_to(r.cursor);
    }
}

struct OrderbookResponse {
    Orderbook orderbook;
};

inline void from_json(const nlohmann::json& j, OrderbookResponse& r) {
    if (j.contains("orderbook")) {
        j.at("orderbook").get_to(r.orderbook);
    }
}

struct Trade {
    std::string trade_id;
    std::string ticker;
    std::string created_time;
    std::string yes_price_dollars;
    std::string no_price_dollars;
    std::string count_fp;
    std::string taker_side;

    double yes_price_cents() const {
        return std::atof(yes_price_dollars.c_str()) * 100.0;
    }
};

inline void from_json(const nlohmann::json& j, Trade& t) {
    if (j.contains("trade_id")) j.at("trade_id").get_to(t.trade_id);
    if (j.contains("ticker")) j.at("ticker").get_to(t.ticker);
    if (j.contains("created_time")) j.at("created_time").get_to(t.created_time);
    if (j.contains("yes_price_dollars")) j.at("yes_price_dollars").get_to(t.yes_price_dollars);
    if (j.contains("no_price_dollars")) j.at("no_price_dollars").get_to(t.no_price_dollars);
    if (j.contains("count_fp")) j.at("count_fp").get_to(t.count_fp);
    if (j.contains("taker_side")) j.at("taker_side").get_to(t.taker_side);
}

struct TradesResponse {
    std::vector<Trade> trades;
    std::string cursor;

    bool has_more() const {
        return !cursor.empty();
    }
};

inline void from_json(const nlohmann::json& j, TradesResponse& r) {
    if (j.contains("trades")) j.at("trades").get_to(r.trades);
    if (j.contains("cursor") && !j.at("cursor").is_null()) j.at("cursor").get_to(r.cursor);
}

} // namespace predibloom::api
