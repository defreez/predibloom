#pragma once

#include <string>
#include <vector>
#include <cstdlib>
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

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Market,
    ticker, event_ticker, market_type, title, subtitle, status,
    yes_bid_dollars, yes_ask_dollars, no_bid_dollars, no_ask_dollars,
    last_price_dollars, volume_fp, volume_24h_fp)

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

} // namespace predibloom::api
