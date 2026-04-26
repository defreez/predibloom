#pragma once

#include "../api/types.hpp"
#include <cmath>
#include <optional>
#include <string>
#include <cctype>

namespace predibloom::cli {

// Bracket bounds: floor is inclusive, cap is exclusive
// e.g., "58 or above" -> floor=58, cap=nullopt (wins if actual >= 58)
// e.g., "77 or below" -> floor=nullopt, cap=78 (wins if actual < 78, i.e., <= 77)
// e.g., "70 to 71" -> floor=70, cap=72 (wins if actual >= 70 && actual < 72)
struct Bracket {
    const api::Market* market = nullptr;
    std::optional<int> floor;  // inclusive lower bound
    std::optional<int> cap;    // exclusive upper bound

    // Generate display string from floor/cap - single source of truth
    std::string displayString() const {
        if (floor && cap) {
            // Range like "70-71" (cap is exclusive, so display cap-1)
            return std::to_string(*floor) + "-" + std::to_string(*cap - 1);
        } else if (floor && !cap) {
            // "58+" means 58 or above
            return std::to_string(*floor) + "+";
        } else if (!floor && cap) {
            // "77-" means 77 or below (cap is exclusive, so display cap-1)
            return std::to_string(*cap - 1) + "-";
        }
        return "?";
    }

    // Check if temperature falls in this bracket
    // Uses bracket boundaries (cap is exclusive internally, so boundary is cap-1)
    // Rounds temp to nearest integer since Kalshi settles on integer temps
    bool contains(double temp) const {
        double rounded = std::round(temp);
        if (floor && cap) {
            return rounded >= *floor && rounded <= (*cap - 1);
        } else if (floor && !cap) {
            return rounded >= *floor;
        } else if (!floor && cap) {
            return rounded <= (*cap - 1);
        }
        return false;
    }

    // Distance from nearest edge (for margin calculation)
    // Positive = inside bracket, negative = outside bracket
    // Cap is exclusive internally, so boundary is (cap - 1)
    // Uses rounded temp since Kalshi settles on integer temps
    double marginFrom(double temp) const {
        double rounded = std::round(temp);
        double dist_from_floor = floor ? (rounded - *floor) : 999.0;
        double dist_from_cap = cap ? ((*cap - 1) - rounded) : 999.0;
        return std::min(dist_from_floor, dist_from_cap);
    }
};

// Parse bracket from market text (subtitle or title)
inline Bracket parseBracket(const api::Market& market) {
    Bracket b;
    b.market = &market;

    std::string text = market.subtitle;
    bool use_title = text.empty();
    if (use_title) {
        text = market.title;
    }

    if (use_title) {
        // Parse from title: "...be >57°...", "...be <78°...", "...be 71-72°..."
        size_t be_pos = text.find("be ");
        if (be_pos != std::string::npos) {
            std::string after_be = text.substr(be_pos + 3);
            size_t start = after_be.find_first_not_of(' ');
            if (start != std::string::npos) after_be = after_be.substr(start);

            if (!after_be.empty() && after_be[0] == '>') {
                // ">57" means strictly greater than 57, i.e., >= 58
                int temp = std::stoi(after_be.substr(1));
                b.floor = temp + 1;
            } else if (!after_be.empty() && after_be[0] == '<') {
                // "<78" means strictly less than 78, i.e., <= 77
                int temp = std::stoi(after_be.substr(1));
                b.cap = temp;
            } else if (!after_be.empty() && std::isdigit(after_be[0])) {
                size_t dash = after_be.find('-');
                if (dash != std::string::npos) {
                    int low = std::stoi(after_be.substr(0, dash));
                    int high = std::stoi(after_be.substr(dash + 1));
                    b.floor = low;
                    b.cap = high + 1;  // exclusive
                }
            }
        }
    } else {
        // Parse from subtitle (e.g., "58° or above", "77° or below", "70° to 71°")
        size_t or_above = text.find(" or above");
        size_t or_below = text.find(" or below");
        size_t to_pos = text.find("° to ");

        if (or_above != std::string::npos) {
            int temp = std::stoi(text.substr(0, or_above));
            b.floor = temp;
        } else if (or_below != std::string::npos) {
            int temp = std::stoi(text.substr(0, or_below));
            b.cap = temp + 1;  // exclusive
        } else if (to_pos != std::string::npos) {
            int low = std::stoi(text.substr(0, to_pos));
            int high = std::stoi(text.substr(to_pos + 5));
            b.floor = low;
            b.cap = high + 1;  // exclusive
        }
    }

    return b;
}

}  // namespace predibloom::cli
