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

inline void from_json(const nlohmann::json& j, Orderbook& ob) {
    if (j.contains("ticker")) j.at("ticker").get_to(ob.ticker);

    // API returns orderbook_fp with yes_dollars/no_dollars as arrays of [price, qty]
    if (j.contains("orderbook_fp")) {
        const auto& fp = j.at("orderbook_fp");
        if (fp.contains("yes_dollars")) {
            for (const auto& level : fp.at("yes_dollars")) {
                if (level.is_array() && level.size() >= 2) {
                    OrderbookLevel l;
                    l.price = level[0].get<std::string>();
                    l.quantity = level[1].get<std::string>();
                    ob.yes.push_back(l);
                }
            }
        }
        if (fp.contains("no_dollars")) {
            for (const auto& level : fp.at("no_dollars")) {
                if (level.is_array() && level.size() >= 2) {
                    OrderbookLevel l;
                    l.price = level[0].get<std::string>();
                    l.quantity = level[1].get<std::string>();
                    ob.no.push_back(l);
                }
            }
        }
    }
}

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
    // The orderbook endpoint returns orderbook_fp directly at top level
    r.orderbook = j.get<Orderbook>();
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

struct Fill {
    std::string fill_id;
    std::string trade_id;
    std::string order_id;
    std::string ticker;
    std::string side;           // "yes" or "no"
    std::string action;         // "buy" or "sell"
    std::string count_fp;
    std::string yes_price_dollars;
    std::string no_price_dollars;
    bool is_taker = false;
    std::string created_time;

    double yes_price_cents() const {
        return std::atof(yes_price_dollars.c_str()) * 100.0;
    }

    double no_price_cents() const {
        return std::atof(no_price_dollars.c_str()) * 100.0;
    }

    int count() const {
        return std::atoi(count_fp.c_str());
    }
};

inline void from_json(const nlohmann::json& j, Fill& f) {
    if (j.contains("fill_id")) j.at("fill_id").get_to(f.fill_id);
    if (j.contains("trade_id")) j.at("trade_id").get_to(f.trade_id);
    if (j.contains("order_id")) j.at("order_id").get_to(f.order_id);
    if (j.contains("ticker")) j.at("ticker").get_to(f.ticker);
    if (j.contains("side")) j.at("side").get_to(f.side);
    if (j.contains("action")) j.at("action").get_to(f.action);
    if (j.contains("count_fp")) j.at("count_fp").get_to(f.count_fp);
    if (j.contains("yes_price_dollars")) j.at("yes_price_dollars").get_to(f.yes_price_dollars);
    if (j.contains("no_price_dollars")) j.at("no_price_dollars").get_to(f.no_price_dollars);
    if (j.contains("is_taker") && !j.at("is_taker").is_null()) f.is_taker = j.at("is_taker").get<bool>();
    if (j.contains("created_time")) j.at("created_time").get_to(f.created_time);
}

struct FillsResponse {
    std::vector<Fill> fills;
    std::string cursor;

    bool has_more() const {
        return !cursor.empty();
    }
};

inline void from_json(const nlohmann::json& j, FillsResponse& r) {
    if (j.contains("fills")) j.at("fills").get_to(r.fills);
    if (j.contains("cursor") && !j.at("cursor").is_null()) j.at("cursor").get_to(r.cursor);
}

// --- Portfolio types ---

struct Position {
    std::string ticker;
    std::string position_fp;
    std::string market_exposure_dollars;
    std::string realized_pnl_dollars;
    std::string fees_paid_dollars;
    std::string total_traded_dollars;

    int position() const { return std::atoi(position_fp.c_str()); }
    double exposure_cents() const { return std::atof(market_exposure_dollars.c_str()) * 100.0; }
    double realized_pnl_cents() const { return std::atof(realized_pnl_dollars.c_str()) * 100.0; }
    double fees_cents() const { return std::atof(fees_paid_dollars.c_str()) * 100.0; }
};

inline void from_json(const nlohmann::json& j, Position& p) {
    if (j.contains("ticker")) j.at("ticker").get_to(p.ticker);
    if (j.contains("position_fp")) j.at("position_fp").get_to(p.position_fp);
    if (j.contains("market_exposure_dollars")) j.at("market_exposure_dollars").get_to(p.market_exposure_dollars);
    if (j.contains("realized_pnl_dollars")) j.at("realized_pnl_dollars").get_to(p.realized_pnl_dollars);
    if (j.contains("fees_paid_dollars")) j.at("fees_paid_dollars").get_to(p.fees_paid_dollars);
    if (j.contains("total_traded_dollars")) j.at("total_traded_dollars").get_to(p.total_traded_dollars);
}

struct PositionsResponse {
    std::vector<Position> market_positions;
    std::string cursor;
    bool has_more() const { return !cursor.empty(); }
};

inline void from_json(const nlohmann::json& j, PositionsResponse& r) {
    if (j.contains("market_positions")) j.at("market_positions").get_to(r.market_positions);
    if (j.contains("cursor") && !j.at("cursor").is_null()) j.at("cursor").get_to(r.cursor);
}

struct Balance {
    int64_t balance = 0;
    int64_t portfolio_value = 0;
};

inline void from_json(const nlohmann::json& j, Balance& b) {
    if (j.contains("balance")) j.at("balance").get_to(b.balance);
    if (j.contains("portfolio_value")) j.at("portfolio_value").get_to(b.portfolio_value);
}

struct Settlement {
    std::string ticker;
    std::string event_ticker;
    std::string market_result;
    int revenue = 0;
    std::string settled_time;
    std::string yes_count_fp;
    std::string no_count_fp;
    std::string yes_total_cost_dollars;
    std::string no_total_cost_dollars;

    int yes_count() const { return std::atoi(yes_count_fp.c_str()); }
    int no_count() const { return std::atoi(no_count_fp.c_str()); }
    double revenue_dollars() const { return revenue / 100.0; }
    double yes_cost_cents() const { return std::atof(yes_total_cost_dollars.c_str()) * 100.0; }
    double no_cost_cents() const { return std::atof(no_total_cost_dollars.c_str()) * 100.0; }
};

inline void from_json(const nlohmann::json& j, Settlement& s) {
    if (j.contains("ticker")) j.at("ticker").get_to(s.ticker);
    if (j.contains("event_ticker")) j.at("event_ticker").get_to(s.event_ticker);
    if (j.contains("market_result")) j.at("market_result").get_to(s.market_result);
    if (j.contains("revenue") && !j.at("revenue").is_null()) j.at("revenue").get_to(s.revenue);
    if (j.contains("settled_time")) j.at("settled_time").get_to(s.settled_time);
    if (j.contains("yes_count_fp")) j.at("yes_count_fp").get_to(s.yes_count_fp);
    if (j.contains("no_count_fp")) j.at("no_count_fp").get_to(s.no_count_fp);
    if (j.contains("yes_total_cost_dollars")) j.at("yes_total_cost_dollars").get_to(s.yes_total_cost_dollars);
    if (j.contains("no_total_cost_dollars")) j.at("no_total_cost_dollars").get_to(s.no_total_cost_dollars);
}

struct SettlementsResponse {
    std::vector<Settlement> settlements;
    std::string cursor;
    bool has_more() const { return !cursor.empty(); }
};

inline void from_json(const nlohmann::json& j, SettlementsResponse& r) {
    if (j.contains("settlements")) j.at("settlements").get_to(r.settlements);
    if (j.contains("cursor") && !j.at("cursor").is_null()) j.at("cursor").get_to(r.cursor);
}

} // namespace predibloom::api
