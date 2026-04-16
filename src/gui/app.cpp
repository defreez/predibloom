#include "app.hpp"
#include "../ui/theme.hpp"
#include "raylib.h"
#include <algorithm>
#include <cstdio>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <nlohmann/json.hpp>

namespace predibloom {

App::App()
    : client_(std::make_unique<api::KalshiClient>()),
      openmeteo_(std::make_unique<api::OpenMeteoClient>()),
      service_(std::make_unique<core::MarketService>(*client_)),
      comparison_service_(std::make_unique<core::WeatherComparisonService>(*client_, *openmeteo_)),
      config_(core::Config::load()) {
    rebuildWidgets();
    fetchMarkets();
}

void App::rebuildWidgets() {
    widgets_.clear();

    int title_width = MeasureText("PREDIBLOOM", 24);
    int tab_x = title_width + 40;

    // Create tab buttons
    for (size_t i = 0; i < config_.tabs.size(); i++) {
        const auto& tab = config_.tabs[i];
        int text_width = MeasureText(tab.name.c_str(), 18);
        int tab_width = text_width + 20;

        std::string tab_id = "tab_" + tab.name;
        Rectangle bounds = {(float)tab_x, 0, (float)tab_width, (float)TOOLBAR_HEIGHT};

        ui::Button btn(tab_id, bounds, tab.name, [this, i]() {
            if ((int)i != selected_tab_idx_) {
                selected_tab_idx_ = (int)i;
                fetchMarkets();
                rebuildWidgets();
            }
        });
        btn.is_tab = true;
        btn.is_selected = (i == (size_t)selected_tab_idx_);
        widgets_.addButton(btn);

        tab_x += tab_width + 10;
    }

    // Refresh button
    int w = GetScreenWidth();
    int left_w = static_cast<int>(w * LEFT_PANEL_RATIO);
    Rectangle refresh_bounds = {(float)(left_w - 90), (float)(TOOLBAR_HEIGHT + 5), 80.0f, 20.0f};
    widgets_.addButton(ui::Button("refresh", refresh_bounds, "REFRESH", [this]() {
        fetchMarkets();
    }));
}

void App::initControlSocket() {
    const char* socket_path = "/tmp/predibloom.sock";

    // Remove existing socket if it exists
    unlink(socket_path);

    // Create Unix domain socket
    control_socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (control_socket_ < 0) {
        fprintf(stderr, "Failed to create control socket\n");
        return;
    }

    // Set non-blocking
    int flags = fcntl(control_socket_, F_GETFL, 0);
    fcntl(control_socket_, F_SETFL, flags | O_NONBLOCK);

    // Bind to socket path
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(control_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to bind control socket\n");
        close(control_socket_);
        control_socket_ = -1;
        return;
    }

    // Listen for connections
    if (listen(control_socket_, 5) < 0) {
        fprintf(stderr, "Failed to listen on control socket\n");
        close(control_socket_);
        control_socket_ = -1;
        return;
    }

    printf("Control socket listening on %s\n", socket_path);
}

void App::handleControlCommands() {
    if (control_socket_ < 0) return;

    // Accept new client if we don't have one
    if (client_socket_ < 0) {
        client_socket_ = accept(control_socket_, nullptr, nullptr);
        if (client_socket_ >= 0) {
            // Set client socket to non-blocking
            int flags = fcntl(client_socket_, F_GETFL, 0);
            fcntl(client_socket_, F_SETFL, flags | O_NONBLOCK);
        }
    }

    // Read commands from client
    if (client_socket_ >= 0) {
        char buffer[4096];
        ssize_t bytes_read = recv(client_socket_, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            command_buffer_ += buffer;

            // Process complete commands (newline-delimited)
            size_t newline_pos;
            while ((newline_pos = command_buffer_.find('\n')) != std::string::npos) {
                std::string cmd_json = command_buffer_.substr(0, newline_pos);
                command_buffer_.erase(0, newline_pos + 1);

                // Execute command and send response
                std::string response;
                executeCommand(cmd_json, response);

                response += "\n";
                send(client_socket_, response.c_str(), response.length(), 0);
            }
        } else if (bytes_read == 0 || (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Client disconnected
            close(client_socket_);
            client_socket_ = -1;
            command_buffer_.clear();
        }
    }
}

void App::executeCommand(const std::string& cmd_json, std::string& response) {
    try {
        auto cmd = nlohmann::json::parse(cmd_json);
        std::string command = cmd["cmd"];

        nlohmann::json result;
        result["status"] = "ok";

        if (command == "screenshot") {
            std::string path = cmd.value("path", ".output/screenshot.png");
            TakeScreenshot(path.c_str());
            result["path"] = path;
        }
        else if (command == "click_button") {
            std::string button_id = cmd["button_id"];
            pending_button_clicks_.push(button_id);
        }
        else if (command == "list_buttons") {
            nlohmann::json buttons = nlohmann::json::array();
            for (const auto& btn : widgets_.buttons()) {
                buttons.push_back({
                    {"id", btn.id},
                    {"label", btn.label},
                    {"is_tab", btn.is_tab},
                    {"is_selected", btn.is_selected}
                });
            }
            result["buttons"] = buttons;
        }
        else if (command == "scroll") {
            float delta = cmd["delta"];
            pending_scrolls_.push(delta);
        }
        else if (command == "get_state") {
            result["state"] = nlohmann::json::parse(getStateJson());
        }
        else if (command == "quit") {
            // Close window (will exit main loop)
            result["status"] = "ok";
            response = result.dump();
            return;
        }
        else {
            result["status"] = "error";
            result["message"] = "Unknown command: " + command;
        }

        response = result.dump();
    } catch (const std::exception& e) {
        nlohmann::json error;
        error["status"] = "error";
        error["message"] = e.what();
        response = error.dump();
    }
}

std::string App::getStateJson() const {
    nlohmann::json state;
    state["num_markets"] = markets_.size();
    state["selected_market_idx"] = selected_market_idx_;
    state["selected_tab_idx"] = selected_tab_idx_;
    state["scroll_offset"] = scroll_offset_;
    state["is_loading"] = is_loading_;
    state["has_error"] = !error_message_.empty();

    if (selected_tab_idx_ >= 0 && selected_tab_idx_ < (int)config_.tabs.size()) {
        state["selected_tab_name"] = config_.tabs[selected_tab_idx_].name;
    }

    if (selected_market_idx_ >= 0 && selected_market_idx_ < (int)markets_.size()) {
        state["selected_market_ticker"] = markets_[selected_market_idx_].ticker;
    }

    return state.dump();
}

void App::fetchMarkets() {
    is_loading_ = true;
    error_message_.clear();
    markets_.clear();

    if (config_.tabs.empty() || selected_tab_idx_ >= (int)config_.tabs.size()) {
        is_loading_ = false;
        return;
    }

    const auto& tab = config_.tabs[selected_tab_idx_];

    // Fetch markets for each series in the tab
    for (const auto& series : tab.series) {
        core::MarketFilter filter;
        filter.status = "open";
        filter.limit = 50;
        filter.series_ticker = series.series_ticker;

        auto result = service_->listMarkets(filter);
        if (result.ok()) {
            auto& new_markets = result.value();
            markets_.insert(markets_.end(), new_markets.begin(), new_markets.end());
        }
    }

    is_loading_ = false;
    selected_market_idx_ = -1;
    scroll_offset_ = 0.0f;
    selected_orderbook_.reset();
}

void App::fetchOrderbook(const std::string& ticker) {
    selected_orderbook_.reset();
    selected_comparison_.reset();

    auto result = service_->getOrderbook(ticker, 10);
    if (result.ok()) {
        selected_orderbook_ = std::move(result.value());
    }

    // Fetch weather comparison for KXHIGHNY markets
    if (ticker.find("KXHIGHNY") == 0 && selected_market_idx_ >= 0) {
        auto comp_result = comparison_service_->getPoint(markets_[selected_market_idx_]);
        if (comp_result.ok()) {
            selected_comparison_ = std::move(comp_result.value());
        }
    }
}

void App::Update(float dt) {
    (void)dt;

    int w = GetScreenWidth();
    int h = GetScreenHeight();
    int content_y = TOOLBAR_HEIGHT;
    int content_h = h - TOOLBAR_HEIGHT - TICKER_HEIGHT;
    int left_w = static_cast<int>(w * LEFT_PANEL_RATIO);

    Rectangle left_panel = {0.0f, (float)content_y, (float)left_w, (float)content_h};
    Vector2 mouse = GetMousePosition();

    // Process pending simulated scrolls
    if (!pending_scrolls_.empty()) {
        float scroll_delta = pending_scrolls_.front();
        pending_scrolls_.pop();

        scroll_offset_ -= scroll_delta * MARKET_ROW_HEIGHT;

        // Clamp scroll bounds
        float max_scroll = std::max(0.0f,
            (float)(markets_.size() * MARKET_ROW_HEIGHT - content_h + HEADER_HEIGHT));
        scroll_offset_ = std::max(0.0f, std::min(scroll_offset_, max_scroll));
    }

    // Process pending button clicks (from MCP)
    while (!pending_button_clicks_.empty()) {
        std::string button_id = pending_button_clicks_.front();
        pending_button_clicks_.pop();
        widgets_.clickButton(button_id);
    }

    // Handle real scrolling in left panel
    if (CheckCollisionPointRec(mouse, left_panel)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            scroll_offset_ -= wheel * MARKET_ROW_HEIGHT;

            // Clamp scroll bounds
            float max_scroll = std::max(0.0f,
                (float)(markets_.size() * MARKET_ROW_HEIGHT - content_h + HEADER_HEIGHT));
            scroll_offset_ = std::max(0.0f, std::min(scroll_offset_, max_scroll));
        }
    }

    // Handle real mouse clicks
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        // First check widgets (tabs, refresh button)
        if (!widgets_.handleClick(mouse)) {
            // Not a widget click, check market selection
            if (CheckCollisionPointRec(mouse, left_panel)) {
                int list_y = content_y + HEADER_HEIGHT;
                if (mouse.y >= list_y) {
                    int clicked_idx = static_cast<int>((mouse.y - list_y + scroll_offset_)
                                                       / MARKET_ROW_HEIGHT);
                    if (clicked_idx >= 0 && clicked_idx < (int)markets_.size()) {
                        selected_market_idx_ = clicked_idx;
                        fetchOrderbook(markets_[clicked_idx].ticker);
                    }
                }
            }
        }
    }
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

    // Draw tab buttons via widget system
    for (const auto& btn : widgets_.buttons()) {
        if (btn.is_tab) {
            btn.draw();
        }
    }
}

void App::DrawLeftPanel(int x, int y, int w, int h) const {
    auto& t = ui::theme();

    // Background
    DrawRectangle(x, y, w, h, t.bg_dark);

    // Header with refresh button
    DrawRectangle(x, y, w, HEADER_HEIGHT, t.bg_panel);
    DrawText("MARKETS", x + 10, y + 5, t.font_small, t.text);

    // Draw refresh button via widget system
    for (const auto& btn : widgets_.buttons()) {
        if (btn.id == "refresh") {
            btn.draw();
        }
    }

    // Loading/error states
    if (is_loading_) {
        DrawText("Loading markets...", x + 10, y + HEADER_HEIGHT + 10,
                 t.font_body, t.text_dim);
        DrawLine(x + w - 1, y, x + w - 1, y + h, t.border);
        return;
    }

    if (!error_message_.empty()) {
        DrawText(error_message_.c_str(), x + 10, y + HEADER_HEIGHT + 10,
                 t.font_small, t.negative);
        DrawLine(x + w - 1, y, x + w - 1, y + h, t.border);
        return;
    }

    // Market list
    int list_y = y + HEADER_HEIGHT;
    int list_h = h - HEADER_HEIGHT;

    // Calculate visible range
    int first_visible = (int)(scroll_offset_ / MARKET_ROW_HEIGHT);
    int visible_count = (list_h / MARKET_ROW_HEIGHT) + 2;
    int last_visible = std::min(first_visible + visible_count, (int)markets_.size());

    // Clip rendering to list area
    BeginScissorMode(x, list_y, w, list_h);

    for (int i = first_visible; i < last_visible; i++) {
        const auto& market = markets_[i];
        int row_y = list_y + (i * MARKET_ROW_HEIGHT) - (int)scroll_offset_;

        // Background (highlight if selected)
        Color bg = (i == selected_market_idx_) ? t.bg_selected : t.bg_dark;
        DrawRectangle(x, row_y, w, MARKET_ROW_HEIGHT, bg);

        // Ticker
        DrawText(market.ticker.c_str(), x + 10, row_y + 8,
                 t.font_body, t.text);

        // Prices (yes bid/ask)
        char price_text[128];
        snprintf(price_text, sizeof(price_text), "%.0f¢ / %.0f¢",
                 market.yes_bid_cents(), market.yes_ask_cents());
        DrawText(price_text, x + 10, row_y + 35, t.font_small, t.text_dim);

        // Bottom border
        DrawLine(x, row_y + MARKET_ROW_HEIGHT - 1,
                 x + w, row_y + MARKET_ROW_HEIGHT - 1, t.border);
    }

    EndScissorMode();

    // Right border
    DrawLine(x + w - 1, y, x + w - 1, y + h, t.border);
}

void App::DrawRightPanel(int x, int y, int w, int h) const {
    auto& t = ui::theme();
    DrawRectangle(x, y, w, h, t.bg_dark);

    if (selected_market_idx_ < 0 || selected_market_idx_ >= (int)markets_.size()) {
        DrawText("Select a market to view details", x + 10, y + 10,
                 t.font_body, t.text_dim);
        return;
    }

    const auto& market = markets_[selected_market_idx_];
    int cursor_y = y + 10;

    // Title
    DrawText(market.title.c_str(), x + 10, cursor_y, t.font_header, t.text);
    cursor_y += 35;

    // Subtitle
    if (!market.subtitle.empty()) {
        DrawText(market.subtitle.c_str(), x + 10, cursor_y,
                 t.font_small, t.text_dim);
        cursor_y += 25;
    }

    cursor_y += 10;

    // Current prices
    DrawText("CURRENT PRICES", x + 10, cursor_y, t.font_body, t.accent);
    cursor_y += 30;

    char price_text[256];
    snprintf(price_text, sizeof(price_text), "YES: %.0f¢ / %.0f¢ (bid/ask)",
             market.yes_bid_cents(), market.yes_ask_cents());
    DrawText(price_text, x + 20, cursor_y, t.font_small, t.positive);
    cursor_y += 25;

    snprintf(price_text, sizeof(price_text), "NO:  %.0f¢ / %.0f¢ (bid/ask)",
             market.no_bid_cents(), market.no_ask_cents());
    DrawText(price_text, x + 20, cursor_y, t.font_small, t.negative);
    cursor_y += 40;

    // Weather comparison (for KXHIGHNY markets)
    if (selected_comparison_.has_value()) {
        DrawText("WEATHER DATA", x + 10, cursor_y, t.font_body, t.accent);
        cursor_y += 30;

        const auto& comp = selected_comparison_.value();
        char weather_text[128];

        // Forecast
        if (comp.forecast_high.has_value()) {
            snprintf(weather_text, sizeof(weather_text), "Forecast (%s): %.1fF",
                     comp.date.c_str(), comp.forecast_high.value());
            DrawText(weather_text, x + 20, cursor_y, t.font_small, t.text);
        } else {
            snprintf(weather_text, sizeof(weather_text), "Forecast (%s): N/A",
                     comp.date.c_str());
            DrawText(weather_text, x + 20, cursor_y, t.font_small, t.text_dim);
        }
        cursor_y += 25;

        // Actual
        if (comp.actual_high.has_value()) {
            snprintf(weather_text, sizeof(weather_text), "Actual High: %.1fF",
                     comp.actual_high.value());
            DrawText(weather_text, x + 20, cursor_y, t.font_small, t.text);
        } else {
            DrawText("Actual High: Pending", x + 20, cursor_y, t.font_small, t.text_dim);
        }
        cursor_y += 25;

        // Settlement
        if (!comp.settlement.empty()) {
            const char* checkmark = (comp.settlement == "yes") ? " Y" : " N";
            snprintf(weather_text, sizeof(weather_text), "Settlement: %s%s",
                     comp.settlement.c_str(), checkmark);
            Color settlement_color = (comp.settlement == "yes") ? t.positive : t.negative;
            DrawText(weather_text, x + 20, cursor_y, t.font_small, settlement_color);
        } else {
            DrawText("Settlement: Pending", x + 20, cursor_y, t.font_small, t.text_dim);
        }
        cursor_y += 40;
    }

    // Orderbook
    DrawText("ORDERBOOK", x + 10, cursor_y, t.font_body, t.accent);
    cursor_y += 30;

    if (!selected_orderbook_.has_value()) {
        DrawText("Loading orderbook...", x + 10, cursor_y,
                 t.font_small, t.text_dim);
        return;
    }

    // Orderbook table headers
    DrawText("YES", x + 20, cursor_y, t.font_small, t.positive);
    DrawText("Price", x + 100, cursor_y, t.font_small, t.text_dim);
    DrawText("Qty", x + 180, cursor_y, t.font_small, t.text_dim);

    DrawText("NO", x + 280, cursor_y, t.font_small, t.negative);
    DrawText("Price", x + 360, cursor_y, t.font_small, t.text_dim);
    DrawText("Qty", x + 440, cursor_y, t.font_small, t.text_dim);
    cursor_y += 25;

    // Orderbook levels (top 10)
    const auto& ob = selected_orderbook_.value();
    size_t max_levels = std::max(ob.yes.size(), ob.no.size());
    max_levels = std::min(max_levels, (size_t)10);

    for (size_t i = 0; i < max_levels; i++) {
        char level_text[64];

        // YES side
        if (i < ob.yes.size()) {
            snprintf(level_text, sizeof(level_text), "%.0f¢",
                     ob.yes[i].price_cents());
            DrawText(level_text, x + 100, cursor_y, t.font_small, t.text);

            snprintf(level_text, sizeof(level_text), "%d",
                     ob.yes[i].quantity_int());
            DrawText(level_text, x + 180, cursor_y, t.font_small, t.text);
        }

        // NO side
        if (i < ob.no.size()) {
            snprintf(level_text, sizeof(level_text), "%.0f¢",
                     ob.no[i].price_cents());
            DrawText(level_text, x + 360, cursor_y, t.font_small, t.text);

            snprintf(level_text, sizeof(level_text), "%d",
                     ob.no[i].quantity_int());
            DrawText(level_text, x + 440, cursor_y, t.font_small, t.text);
        }

        cursor_y += 22;
    }
}

void App::DrawTickerBar(int x, int y, int w, int h) const {
    auto& t = ui::theme();
    DrawRectangle(x, y, w, h, t.bg_panel);
    DrawLine(x, y, x + w, y, t.border);
    DrawText("KXBIDEN:45¢ ▲2  |  KXTRUMP:52¢ ▼1  |  KXFED:78¢ ▲5",
             x + 10, y + 6, t.font_small, t.text);
}

} // namespace predibloom
