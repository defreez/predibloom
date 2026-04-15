#include "raylib.h"
#include "app.hpp"
#include "../ui/theme.hpp"
#include "../api/kalshi_client.hpp"
#include <cstdio>

int main() {
    // Test API client before opening window
    printf("Testing Kalshi API client...\n");
    predibloom::api::KalshiClient client;
    auto result = client.getMarkets({.status = "open", .limit = 5});
    if (result.ok()) {
        printf("Fetched %zu markets:\n", result.value().markets.size());
        for (const auto& m : result.value().markets) {
            printf("  %s: %.0fc bid / %.0fc ask\n",
                   m.ticker.c_str(), m.yes_bid_cents(), m.yes_ask_cents());
        }
    } else {
        printf("API error: %s\n", result.error().message.c_str());
    }
    printf("\n");

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Predibloom");
    MaximizeWindow();
    SetTargetFPS(60);

    predibloom::App app;

    while (!WindowShouldClose()) {
        app.Update(GetFrameTime());

        BeginDrawing();
        ClearBackground(ui::theme().bg_dark);
        app.Draw();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
