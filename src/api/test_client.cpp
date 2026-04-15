#include "kalshi_client.hpp"
#include <cstdio>

int main() {
    predibloom::api::KalshiClient client;

    printf("=== Testing getMarkets (open, limit 5) ===\n");
    auto result = client.getMarkets({.status = "open", .limit = 5});
    if (result.ok()) {
        printf("Fetched %zu markets:\n", result.value().markets.size());
        for (const auto& m : result.value().markets) {
            printf("  %s: %.0fc bid / %.0fc ask\n",
                   m.ticker.c_str(), m.yes_bid_cents(), m.yes_ask_cents());
        }
    } else {
        printf("Error: %s\n", result.error().message.c_str());
        return 1;
    }

    printf("\n=== Testing getEvents (limit 3) ===\n");
    auto events = client.getEvents({.limit = 3});
    if (events.ok()) {
        printf("Fetched %zu events:\n", events.value().events.size());
        for (const auto& e : events.value().events) {
            printf("  %s: %s\n", e.event_ticker.c_str(), e.title.c_str());
        }
    } else {
        printf("Error: %s\n", events.error().message.c_str());
    }

    return 0;
}
