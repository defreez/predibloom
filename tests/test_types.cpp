#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "api/types.hpp"

using namespace predibloom::api;

// --- Market price conversions ---

TEST(MarketPriceTest, YesBidCents) {
    Market m;
    m.yes_bid_dollars = "0.52";
    EXPECT_DOUBLE_EQ(m.yes_bid_cents(), 52.0);
}

TEST(MarketPriceTest, YesAskCents) {
    Market m;
    m.yes_ask_dollars = "0.54";
    EXPECT_DOUBLE_EQ(m.yes_ask_cents(), 54.0);
}

TEST(MarketPriceTest, NoBidCents) {
    Market m;
    m.no_bid_dollars = "0.46";
    EXPECT_DOUBLE_EQ(m.no_bid_cents(), 46.0);
}

TEST(MarketPriceTest, NoAskCents) {
    Market m;
    m.no_ask_dollars = "0.48";
    EXPECT_DOUBLE_EQ(m.no_ask_cents(), 48.0);
}

TEST(MarketPriceTest, LastPriceCents) {
    Market m;
    m.last_price_dollars = "0.50";
    EXPECT_DOUBLE_EQ(m.last_price_cents(), 50.0);
}

TEST(MarketPriceTest, ZeroPrice) {
    Market m;
    m.yes_bid_dollars = "0.00";
    EXPECT_DOUBLE_EQ(m.yes_bid_cents(), 0.0);
}

TEST(MarketPriceTest, MaxPrice) {
    Market m;
    m.yes_bid_dollars = "1.00";
    EXPECT_DOUBLE_EQ(m.yes_bid_cents(), 100.0);
}

TEST(MarketPriceTest, EmptyStringReturnsZero) {
    Market m;
    m.yes_bid_dollars = "";
    EXPECT_DOUBLE_EQ(m.yes_bid_cents(), 0.0);
}

// --- OrderbookLevel ---

TEST(OrderbookLevelTest, PriceCents) {
    OrderbookLevel level;
    level.price = "0.55";
    EXPECT_DOUBLE_EQ(level.price_cents(), 55.0);
}

TEST(OrderbookLevelTest, QuantityInt) {
    OrderbookLevel level;
    level.quantity = "150";
    EXPECT_EQ(level.quantity_int(), 150);
}

// --- PaginatedResponse ---

TEST(PaginatedResponseTest, HasMore_WithCursor) {
    EventsResponse r;
    r.cursor = "abc123";
    EXPECT_TRUE(r.has_more());
}

TEST(PaginatedResponseTest, HasMore_EmptyCursor) {
    EventsResponse r;
    r.cursor = "";
    EXPECT_FALSE(r.has_more());
}

// --- JSON deserialization ---

TEST(MarketJsonTest, FullMarket) {
    nlohmann::json j = {
        {"ticker", "KXHIGHNY-26APR18-T85"},
        {"event_ticker", "KXHIGHNY-26APR18"},
        {"market_type", "binary"},
        {"title", "High temp 85+"},
        {"subtitle", "NYC"},
        {"status", "active"},
        {"yes_bid_dollars", "0.52"},
        {"yes_ask_dollars", "0.54"},
        {"no_bid_dollars", "0.46"},
        {"no_ask_dollars", "0.48"},
        {"last_price_dollars", "0.53"},
        {"volume_fp", "1000"},
        {"volume_24h_fp", "500"},
        {"result", ""},
        {"floor_strike", 85},
        {"cap_strike", 89}
    };

    Market m = j.get<Market>();
    EXPECT_EQ(m.ticker, "KXHIGHNY-26APR18-T85");
    EXPECT_EQ(m.event_ticker, "KXHIGHNY-26APR18");
    EXPECT_EQ(m.status, "active");
    EXPECT_DOUBLE_EQ(m.yes_bid_cents(), 52.0);
    EXPECT_TRUE(m.floor_strike.has_value());
    EXPECT_EQ(m.floor_strike.value(), 85);
    EXPECT_TRUE(m.cap_strike.has_value());
    EXPECT_EQ(m.cap_strike.value(), 89);
}

TEST(MarketJsonTest, NullStrikes) {
    nlohmann::json j = {
        {"ticker", "TEST"},
        {"floor_strike", nullptr},
        {"cap_strike", nullptr}
    };

    Market m = j.get<Market>();
    EXPECT_FALSE(m.floor_strike.has_value());
    EXPECT_FALSE(m.cap_strike.has_value());
}

TEST(MarketJsonTest, MissingFields_DefaultsEmpty) {
    nlohmann::json j = {{"ticker", "TEST"}};
    Market m = j.get<Market>();
    EXPECT_EQ(m.ticker, "TEST");
    EXPECT_EQ(m.event_ticker, "");
    EXPECT_EQ(m.status, "");
    EXPECT_FALSE(m.floor_strike.has_value());
}

TEST(MarketJsonTest, MarketsResponse) {
    nlohmann::json j = {
        {"markets", {{{"ticker", "A"}}, {{"ticker", "B"}}}},
        {"cursor", "next_page"}
    };

    MarketsResponse r = j.get<MarketsResponse>();
    EXPECT_EQ(r.markets.size(), 2u);
    EXPECT_EQ(r.markets[0].ticker, "A");
    EXPECT_TRUE(r.has_more());
}

TEST(TradeJsonTest, TradeDeserialization) {
    nlohmann::json j = {
        {"trade_id", "t123"},
        {"ticker", "KXHIGHNY-26APR18-T85"},
        {"yes_price_dollars", "0.60"},
        {"no_price_dollars", "0.40"},
        {"count_fp", "5"},
        {"taker_side", "yes"}
    };

    Trade t = j.get<Trade>();
    EXPECT_EQ(t.trade_id, "t123");
    EXPECT_DOUBLE_EQ(t.yes_price_cents(), 60.0);
}
