#include "app.hpp"
#include "../ui/theme.hpp"
#include "raylib.h"

namespace predibloom {

void App::Update(float dt) {
    // Placeholder for future input handling
    (void)dt;
}

void App::Draw() const {
    auto& t = ui::theme();
    int w = GetScreenWidth();
    int h = GetScreenHeight();

    // Calculate layout
    int content_y = TOOLBAR_HEIGHT;
    int content_h = h - TOOLBAR_HEIGHT - TICKER_HEIGHT;
    int left_w = static_cast<int>(w * LEFT_PANEL_RATIO);
    int right_w = w - left_w;

    // Draw panels
    DrawToolbar(0, 0, w, TOOLBAR_HEIGHT);
    DrawLeftPanel(0, content_y, left_w, content_h);
    DrawRightPanel(left_w, content_y, right_w, content_h);
    DrawTickerBar(0, h - TICKER_HEIGHT, w, TICKER_HEIGHT);
}

void App::DrawToolbar(int x, int y, int w, int h) const {
    auto& t = ui::theme();
    DrawRectangle(x, y, w, h, t.bg_panel);
    DrawLine(x, y + h - 1, x + w, y + h - 1, t.border);
    DrawText("PREDIBLOOM", x + 10, y + 8, t.font_header, t.accent);
}

void App::DrawLeftPanel(int x, int y, int w, int h) const {
    auto& t = ui::theme();
    DrawRectangle(x, y, w, h, t.bg_dark);
    DrawLine(x + w - 1, y, x + w - 1, y + h, t.border);
    DrawText("MARKETS", x + 10, y + 10, t.font_body, t.text_dim);
}

void App::DrawRightPanel(int x, int y, int w, int h) const {
    auto& t = ui::theme();
    DrawRectangle(x, y, w, h, t.bg_dark);
    DrawText("DETAIL", x + 10, y + 10, t.font_body, t.text_dim);
}

void App::DrawTickerBar(int x, int y, int w, int h) const {
    auto& t = ui::theme();
    DrawRectangle(x, y, w, h, t.bg_panel);
    DrawLine(x, y, x + w, y, t.border);
    DrawText("KXBIDEN:45¢ ▲2  |  KXTRUMP:52¢ ▼1  |  KXFED:78¢ ▲5",
             x + 10, y + 6, t.font_small, t.text);
}

} // namespace predibloom
