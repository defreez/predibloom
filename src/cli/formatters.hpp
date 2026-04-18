#pragma once

#include "../api/types.hpp"
#include "../core/weather_comparison.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace predibloom::cli {

enum class OutputFormat { Table, Json, Csv };

inline OutputFormat parseFormat(const std::string& fmt) {
    if (fmt == "json") return OutputFormat::Json;
    if (fmt == "csv") return OutputFormat::Csv;
    return OutputFormat::Table;
}

// Helper to format cents as price string
inline std::string formatPrice(double cents) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(0) << cents << "c";
    return ss.str();
}

// Truncate string with ellipsis if too long
inline std::string truncate(const std::string& s, size_t max_len) {
    if (s.length() <= max_len) return s;
    if (max_len <= 3) return s.substr(0, max_len);
    return s.substr(0, max_len - 3) + "...";
}

// Print markets as table
inline void printMarketsTable(const std::vector<api::Market>& markets) {
    // Header
    std::cout << std::left
              << std::setw(20) << "TICKER"
              << std::setw(40) << "TITLE"
              << std::setw(10) << "BID"
              << std::setw(10) << "ASK"
              << std::setw(10) << "LAST"
              << std::setw(10) << "STATUS"
              << "\n";

    std::cout << std::string(100, '-') << "\n";

    for (const auto& m : markets) {
        std::cout << std::left
                  << std::setw(20) << truncate(m.ticker, 19)
                  << std::setw(40) << truncate(m.title, 39)
                  << std::setw(10) << formatPrice(m.yes_bid_cents())
                  << std::setw(10) << formatPrice(m.yes_ask_cents())
                  << std::setw(10) << formatPrice(m.last_price_cents())
                  << std::setw(10) << m.status
                  << "\n";
    }
}

// Print markets as JSON
inline void printMarketsJson(const std::vector<api::Market>& markets) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : markets) {
        arr.push_back({
            {"ticker", m.ticker},
            {"event_ticker", m.event_ticker},
            {"title", m.title},
            {"subtitle", m.subtitle},
            {"status", m.status},
            {"yes_bid", m.yes_bid_dollars},
            {"yes_ask", m.yes_ask_dollars},
            {"no_bid", m.no_bid_dollars},
            {"no_ask", m.no_ask_dollars},
            {"last_price", m.last_price_dollars},
            {"volume", m.volume_fp},
            {"volume_24h", m.volume_24h_fp}
        });
    }
    std::cout << arr.dump(2) << "\n";
}

// Print markets as CSV
inline void printMarketsCsv(const std::vector<api::Market>& markets) {
    std::cout << "ticker,event_ticker,title,status,yes_bid,yes_ask,last_price,volume\n";
    for (const auto& m : markets) {
        // Escape quotes in title
        std::string title = m.title;
        size_t pos = 0;
        while ((pos = title.find('"', pos)) != std::string::npos) {
            title.replace(pos, 1, "\"\"");
            pos += 2;
        }
        std::cout << m.ticker << ","
                  << m.event_ticker << ","
                  << "\"" << title << "\","
                  << m.status << ","
                  << m.yes_bid_dollars << ","
                  << m.yes_ask_dollars << ","
                  << m.last_price_dollars << ","
                  << m.volume_fp << "\n";
    }
}

inline void printMarkets(const std::vector<api::Market>& markets, OutputFormat fmt) {
    switch (fmt) {
        case OutputFormat::Json: printMarketsJson(markets); break;
        case OutputFormat::Csv: printMarketsCsv(markets); break;
        case OutputFormat::Table: printMarketsTable(markets); break;
    }
}

// Print events as table
inline void printEventsTable(const std::vector<api::Event>& events) {
    std::cout << std::left
              << std::setw(25) << "EVENT_TICKER"
              << std::setw(50) << "TITLE"
              << std::setw(15) << "STATUS"
              << "\n";

    std::cout << std::string(90, '-') << "\n";

    for (const auto& e : events) {
        std::cout << std::left
                  << std::setw(25) << truncate(e.event_ticker, 24)
                  << std::setw(50) << truncate(e.title, 49)
                  << std::setw(15) << e.status
                  << "\n";
    }
}

inline void printEventsJson(const std::vector<api::Event>& events) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : events) {
        arr.push_back({
            {"event_ticker", e.event_ticker},
            {"series_ticker", e.series_ticker},
            {"title", e.title},
            {"category", e.category},
            {"sub_title", e.sub_title},
            {"status", e.status}
        });
    }
    std::cout << arr.dump(2) << "\n";
}

inline void printEventsCsv(const std::vector<api::Event>& events) {
    std::cout << "event_ticker,series_ticker,title,category,status\n";
    for (const auto& e : events) {
        std::string title = e.title;
        size_t pos = 0;
        while ((pos = title.find('"', pos)) != std::string::npos) {
            title.replace(pos, 1, "\"\"");
            pos += 2;
        }
        std::cout << e.event_ticker << ","
                  << e.series_ticker << ","
                  << "\"" << title << "\","
                  << e.category << ","
                  << e.status << "\n";
    }
}

inline void printEvents(const std::vector<api::Event>& events, OutputFormat fmt) {
    switch (fmt) {
        case OutputFormat::Json: printEventsJson(events); break;
        case OutputFormat::Csv: printEventsCsv(events); break;
        case OutputFormat::Table: printEventsTable(events); break;
    }
}

// Print orderbook as table
inline void printOrderbookTable(const api::Orderbook& ob) {
    std::cout << "Orderbook for " << ob.ticker << "\n\n";

    // Find max depth
    size_t max_depth = std::max(ob.yes.size(), ob.no.size());

    std::cout << std::left
              << std::setw(20) << "YES BIDS"
              << std::setw(20) << "NO BIDS"
              << "\n";
    std::cout << std::string(40, '-') << "\n";

    for (size_t i = 0; i < max_depth; ++i) {
        std::string yes_str = "";
        std::string no_str = "";

        if (i < ob.yes.size()) {
            yes_str = formatPrice(ob.yes[i].price_cents()) + " x " +
                      std::to_string(ob.yes[i].quantity_int());
        }
        if (i < ob.no.size()) {
            no_str = formatPrice(ob.no[i].price_cents()) + " x " +
                     std::to_string(ob.no[i].quantity_int());
        }

        std::cout << std::left
                  << std::setw(20) << yes_str
                  << std::setw(20) << no_str
                  << "\n";
    }
}

inline void printOrderbookJson(const api::Orderbook& ob) {
    nlohmann::json j;
    j["ticker"] = ob.ticker;

    j["yes"] = nlohmann::json::array();
    for (const auto& level : ob.yes) {
        j["yes"].push_back({{"price", level.price}, {"quantity", level.quantity}});
    }

    j["no"] = nlohmann::json::array();
    for (const auto& level : ob.no) {
        j["no"].push_back({{"price", level.price}, {"quantity", level.quantity}});
    }

    std::cout << j.dump(2) << "\n";
}

inline void printOrderbookCsv(const api::Orderbook& ob) {
    std::cout << "side,price,quantity\n";
    for (const auto& level : ob.yes) {
        std::cout << "yes," << level.price << "," << level.quantity << "\n";
    }
    for (const auto& level : ob.no) {
        std::cout << "no," << level.price << "," << level.quantity << "\n";
    }
}

inline void printOrderbook(const api::Orderbook& ob, OutputFormat fmt) {
    switch (fmt) {
        case OutputFormat::Json: printOrderbookJson(ob); break;
        case OutputFormat::Csv: printOrderbookCsv(ob); break;
        case OutputFormat::Table: printOrderbookTable(ob); break;
    }
}

// Print single market detail
inline void printMarketDetail(const api::Market& m, OutputFormat fmt) {
    if (fmt == OutputFormat::Json) {
        nlohmann::json j = {
            {"ticker", m.ticker},
            {"event_ticker", m.event_ticker},
            {"title", m.title},
            {"subtitle", m.subtitle},
            {"status", m.status},
            {"yes_bid", m.yes_bid_dollars},
            {"yes_ask", m.yes_ask_dollars},
            {"no_bid", m.no_bid_dollars},
            {"no_ask", m.no_ask_dollars},
            {"last_price", m.last_price_dollars},
            {"volume", m.volume_fp},
            {"volume_24h", m.volume_24h_fp}
        };
        std::cout << j.dump(2) << "\n";
        return;
    }

    std::cout << "Market: " << m.ticker << "\n";
    std::cout << std::string(50, '-') << "\n";
    std::cout << "Title:      " << m.title << "\n";
    std::cout << "Subtitle:   " << m.subtitle << "\n";
    std::cout << "Event:      " << m.event_ticker << "\n";
    std::cout << "Status:     " << m.status << "\n";
    std::cout << "\n";
    std::cout << "YES:        " << formatPrice(m.yes_bid_cents())
              << " bid / " << formatPrice(m.yes_ask_cents()) << " ask\n";
    std::cout << "NO:         " << formatPrice(m.no_bid_cents())
              << " bid / " << formatPrice(m.no_ask_cents()) << " ask\n";
    std::cout << "Last:       " << formatPrice(m.last_price_cents()) << "\n";
    std::cout << "Volume:     " << m.volume_fp << " (24h: " << m.volume_24h_fp << ")\n";
}

// Helper to format temperature
inline std::string formatTemp(std::optional<double> temp) {
    if (!temp.has_value()) return "-";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << temp.value() << "F";
    return ss.str();
}

// Helper to format strike range
inline std::string formatStrikeRange(std::optional<int> floor, std::optional<int> cap) {
    if (floor.has_value() && !cap.has_value()) {
        // floor+ (floor or higher)
        return std::to_string(floor.value()) + "+";
    }
    if (!floor.has_value() && cap.has_value()) {
        // cap- (cap or lower)
        return "<" + std::to_string(cap.value());
    }
    if (floor.has_value() && cap.has_value()) {
        // Between floor and cap
        return std::to_string(floor.value()) + "-" + std::to_string(cap.value());
    }
    return "-";
}

// Print comparison summary as table
inline void printComparisonTable(const core::ComparisonSummary& summary) {
    std::cout << "Weather Comparison: " << summary.series_ticker << "\n";
    std::cout << std::string(100, '=') << "\n\n";

    std::cout << "Summary:\n";
    std::cout << "  Total markets:    " << summary.total_markets << "\n";
    std::cout << "  Settled:          " << summary.settled_markets << "\n";
    std::cout << "  With forecast:    " << summary.markets_with_forecast << "\n";
    std::cout << "  With actual:      " << summary.markets_with_actual << "\n";

    if (summary.forecast_mae > 0) {
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "  Forecast MAE:     " << summary.forecast_mae << "F\n";
    }
    if (summary.market_accuracy > 0) {
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "  Market accuracy:  " << summary.market_accuracy << "%\n";
    }

    std::cout << "\n";
    std::cout << std::left
              << std::setw(12) << "DATE"
              << std::setw(24) << "TICKER"
              << std::setw(10) << "STRIKE"
              << std::setw(10) << "PRICE"
              << std::setw(12) << "FORECAST"
              << std::setw(12) << "ACTUAL"
              << std::setw(10) << "SETTLED"
              << "\n";
    std::cout << std::string(90, '-') << "\n";

    for (const auto& pt : summary.points) {
        std::cout << std::left
                  << std::setw(12) << pt.date
                  << std::setw(24) << truncate(pt.market_ticker, 23)
                  << std::setw(10) << formatStrikeRange(pt.floor_strike, pt.cap_strike)
                  << std::setw(10) << formatPrice(pt.kalshi_price)
                  << std::setw(12) << formatTemp(pt.forecast_temp)
                  << std::setw(12) << formatTemp(pt.actual_temp)
                  << std::setw(10) << (pt.settlement.empty() ? "-" : pt.settlement)
                  << "\n";
    }
}

// Print comparison as JSON
inline void printComparisonJson(const core::ComparisonSummary& summary) {
    nlohmann::json j;
    j["series_ticker"] = summary.series_ticker;
    j["total_markets"] = summary.total_markets;
    j["settled_markets"] = summary.settled_markets;
    j["markets_with_forecast"] = summary.markets_with_forecast;
    j["markets_with_actual"] = summary.markets_with_actual;
    j["forecast_mae"] = summary.forecast_mae;
    j["market_accuracy"] = summary.market_accuracy;

    j["points"] = nlohmann::json::array();
    for (const auto& pt : summary.points) {
        nlohmann::json point;
        point["date"] = pt.date;
        point["market_ticker"] = pt.market_ticker;
        point["kalshi_price"] = pt.kalshi_price;

        if (pt.forecast_temp.has_value()) {
            point["forecast_temp"] = pt.forecast_temp.value();
        } else {
            point["forecast_temp"] = nullptr;
        }

        if (pt.actual_temp.has_value()) {
            point["actual_temp"] = pt.actual_temp.value();
        } else {
            point["actual_temp"] = nullptr;
        }

        point["settlement"] = pt.settlement;

        if (pt.floor_strike.has_value()) {
            point["floor_strike"] = pt.floor_strike.value();
        }
        if (pt.cap_strike.has_value()) {
            point["cap_strike"] = pt.cap_strike.value();
        }

        j["points"].push_back(point);
    }

    std::cout << j.dump(2) << "\n";
}

// Print comparison as CSV
inline void printComparisonCsv(const core::ComparisonSummary& summary) {
    std::cout << "date,ticker,floor_strike,cap_strike,kalshi_price,forecast_temp,actual_temp,settlement\n";
    for (const auto& pt : summary.points) {
        std::cout << pt.date << ","
                  << pt.market_ticker << ","
                  << (pt.floor_strike.has_value() ? std::to_string(pt.floor_strike.value()) : "") << ","
                  << (pt.cap_strike.has_value() ? std::to_string(pt.cap_strike.value()) : "") << ","
                  << pt.kalshi_price << ","
                  << (pt.forecast_temp.has_value() ? std::to_string(pt.forecast_temp.value()) : "") << ","
                  << (pt.actual_temp.has_value() ? std::to_string(pt.actual_temp.value()) : "") << ","
                  << pt.settlement << "\n";
    }
}

inline void printComparison(const core::ComparisonSummary& summary, OutputFormat fmt) {
    switch (fmt) {
        case OutputFormat::Json: printComparisonJson(summary); break;
        case OutputFormat::Csv: printComparisonCsv(summary); break;
        case OutputFormat::Table: printComparisonTable(summary); break;
    }
}

} // namespace predibloom::cli
