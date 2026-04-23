#include "app.hpp"
#include "../ui/theme.hpp"
#include "../ui/chart.hpp"
#include "raylib.h"
#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace predibloom {

App::App()
    : client_(std::make_unique<api::KalshiClient>()),
      gribstream_(std::make_unique<api::GribStreamClient>(core::Config::load().gribstream_api_token)),
      service_(std::make_unique<core::MarketService>(*client_)),
      comparison_service_(std::make_unique<core::WeatherComparisonService>(*client_, *gribstream_)),
      config_(core::Config::load()) {
    rebuildWidgets();
    fetchMarkets();
}

void App::rebuildWidgets() {
    widgets_.clear();
    int title_width = MeasureText("PREDIBLOOM", 24);
    int tab_x = title_width + 40;

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

    int w = GetScreenWidth();
    int left_w = static_cast<int>(w * LEFT_PANEL_RATIO);
    Rectangle refresh_bounds = {(float)(left_w - 90), (float)(TOOLBAR_HEIGHT + 5), 80.0f, 20.0f};
    widgets_.addButton(ui::Button("refresh", refresh_bounds, "REFRESH", [this]() {
        fetchMarkets();
    }));
}

void App::initControlSocket() {
    control_socket_ = std::make_unique<ControlSocket>();
    control_socket_->init();
    control_socket_->setStateCallback([this]() { return getStateJson(); });
    control_socket_->setButtonListCallback([this]() { return getButtonListJson(); });
}

void App::handleControlCommands() {
    if (!control_socket_) return;
    control_socket_->poll();
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

std::string App::getButtonListJson() const {
    nlohmann::json buttons = nlohmann::json::array();
    for (const auto& btn : widgets_.buttons()) {
        buttons.push_back({
            {"id", btn.id},
            {"label", btn.label},
            {"is_tab", btn.is_tab},
            {"is_selected", btn.is_selected}
        });
    }
    return buttons.dump();
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
    selected_trades_.clear();
    selected_comparison_summary_.reset();
}

void App::fetchOrderbook(const std::string& ticker) {
    selected_orderbook_.reset();
    selected_comparison_.reset();
    selected_trades_.clear();
    selected_comparison_summary_.reset();

    auto result = service_->getOrderbook(ticker, 10);
    if (result.ok()) {
        selected_orderbook_ = std::move(result.value());
    }

    client_->setCaching(true);
    auto trades_result = client_->getAllTrades(ticker);
    if (trades_result.ok()) {
        selected_trades_ = std::move(trades_result.value());
    }
    client_->setCaching(false);

    if (selected_market_idx_ >= 0) {
        const auto& market = markets_[selected_market_idx_];
        size_t dash = market.event_ticker.rfind('-');
        if (dash != std::string::npos) {
            std::string series_ticker = market.event_ticker.substr(0, dash);
            auto* series_config = config_.findSeries(series_ticker);
            if (series_config && series_config->latitude != 0) {
                comparison_service_->setLocation(
                    series_config->latitude, series_config->longitude,
                    series_config->isLowTemp(),
                    series_config->entry_day_offset,
                    series_config->effectiveEntryHour());
                auto comp_result = comparison_service_->getPoint(market);
                if (comp_result.ok()) {
                    selected_comparison_ = std::move(comp_result.value());
                }

                std::string market_date = core::parseDateFromEventTicker(market.event_ticker);
                if (!market_date.empty()) {
                    int year = std::stoi(market_date.substr(0, 4));
                    int month = std::stoi(market_date.substr(5, 2));
                    int day = std::stoi(market_date.substr(8, 2));
                    day -= 30;
                    while (day <= 0) {
                        month--;
                        if (month <= 0) { month = 12; year--; }
                        int days_in_month[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
                        if (month == 2 && year % 4 == 0) days_in_month[2] = 29;
                        day += days_in_month[month];
                    }
                    char start_buf[16];
                    snprintf(start_buf, sizeof(start_buf), "%04d-%02d-%02d", year, month, day);

                    client_->setCaching(true);
                    gribstream_->setCaching(true);
                    auto summary_result = comparison_service_->analyze(
                        series_ticker, std::string(start_buf), market_date);
                    if (summary_result.ok()) {
                        selected_comparison_summary_ = std::move(summary_result.value());
                    }
                    client_->setCaching(false);
                    gribstream_->setCaching(false);
                }
            }
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

    // Process pending simulated inputs from control socket
    if (control_socket_) {
        while (control_socket_->hasPendingScroll()) {
            float scroll_delta = control_socket_->popPendingScroll();
            scroll_offset_ -= scroll_delta * MARKET_ROW_HEIGHT;
            float max_scroll = std::max(0.0f, (float)(markets_.size() * MARKET_ROW_HEIGHT - content_h + HEADER_HEIGHT));
            scroll_offset_ = std::max(0.0f, std::min(scroll_offset_, max_scroll));
        }
        while (control_socket_->hasPendingButtonClick()) {
            std::string button_id = control_socket_->popPendingButtonClick();
            widgets_.clickButton(button_id);
        }
        while (control_socket_->hasPendingMarketSelect()) {
            int idx = control_socket_->popPendingMarketSelect();
            if (idx >= 0 && idx < (int)markets_.size()) {
                selected_market_idx_ = idx;
                fetchOrderbook(markets_[idx].ticker);
            }
        }
    }

    // Handle real scrolling in left panel
    if (CheckCollisionPointRec(mouse, left_panel)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            scroll_offset_ -= wheel * MARKET_ROW_HEIGHT;
            float max_scroll = std::max(0.0f, (float)(markets_.size() * MARKET_ROW_HEIGHT - content_h + HEADER_HEIGHT));
            scroll_offset_ = std::max(0.0f, std::min(scroll_offset_, max_scroll));
        }
    }

    // Handle real mouse clicks
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (!widgets_.handleClick(mouse)) {
            if (CheckCollisionPointRec(mouse, left_panel)) {
                int list_y = content_y + HEADER_HEIGHT;
                if (mouse.y >= list_y) {
                    int clicked_idx = static_cast<int>((mouse.y - list_y + scroll_offset_) / MARKET_ROW_HEIGHT);
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
    int content_y = TOOLBAR_HEIGHT;
    int content_h = h - TOOLBAR_HEIGHT - TICKER_HEIGHT;
    int left_w = static_cast<int>(w * LEFT_PANEL_RATIO);
    int right_w = w - left_w;

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
    for (const auto& btn : widgets_.buttons()) {
        if (btn.is_tab) btn.draw();
    }
}

void App::DrawLeftPanel(int x, int y, int w, int h) const {
    auto& t = ui::theme();
    DrawRectangle(x, y, w, h, t.bg_dark);
    DrawRectangle(x, y, w, HEADER_HEIGHT, t.bg_panel);
    DrawText("MARKETS", x + 10, y + 5, t.font_small, t.text);

    for (const auto& btn : widgets_.buttons()) {
        if (btn.id == "refresh") btn.draw();
    }

    if (is_loading_) {
        DrawText("Loading markets...", x + 10, y + HEADER_HEIGHT + 10, t.font_body, t.text_dim);
        DrawLine(x + w - 1, y, x + w - 1, y + h, t.border);
        return;
    }

    if (!error_message_.empty()) {
        DrawText(error_message_.c_str(), x + 10, y + HEADER_HEIGHT + 10, t.font_small, t.negative);
        DrawLine(x + w - 1, y, x + w - 1, y + h, t.border);
        return;
    }

    int list_y = y + HEADER_HEIGHT;
    int list_h = h - HEADER_HEIGHT;
    int first_visible = (int)(scroll_offset_ / MARKET_ROW_HEIGHT);
    int visible_count = (list_h / MARKET_ROW_HEIGHT) + 2;
    int last_visible = std::min(first_visible + visible_count, (int)markets_.size());

    BeginScissorMode(x, list_y, w, list_h);
    for (int i = first_visible; i < last_visible; i++) {
        const auto& market = markets_[i];
        int row_y = list_y + (i * MARKET_ROW_HEIGHT) - (int)scroll_offset_;
        Color bg = (i == selected_market_idx_) ? t.bg_selected : t.bg_dark;
        DrawRectangle(x, row_y, w, MARKET_ROW_HEIGHT, bg);
        DrawText(market.ticker.c_str(), x + 10, row_y + 8, t.font_body, t.text);
        char price_text[128];
        snprintf(price_text, sizeof(price_text), "%.0f¢ / %.0f¢", market.yes_bid_cents(), market.yes_ask_cents());
        DrawText(price_text, x + 10, row_y + 35, t.font_small, t.text_dim);
        DrawLine(x, row_y + MARKET_ROW_HEIGHT - 1, x + w, row_y + MARKET_ROW_HEIGHT - 1, t.border);
    }
    EndScissorMode();
    DrawLine(x + w - 1, y, x + w - 1, y + h, t.border);
}

void App::DrawRightPanel(int x, int y, int w, int h) const {
    auto& t = ui::theme();
    DrawRectangle(x, y, w, h, t.bg_dark);

    if (selected_market_idx_ < 0 || selected_market_idx_ >= (int)markets_.size()) {
        DrawText("Select a market to view details", x + 10, y + 10, t.font_body, t.text_dim);
        return;
    }

    const auto& market = markets_[selected_market_idx_];
    int cursor_y = y + 10;

    DrawText(market.title.c_str(), x + 10, cursor_y, t.font_header, t.text);
    cursor_y += 35;

    if (!market.subtitle.empty()) {
        DrawText(market.subtitle.c_str(), x + 10, cursor_y, t.font_small, t.text_dim);
        cursor_y += 25;
    }
    cursor_y += 10;

    DrawText("CURRENT PRICES", x + 10, cursor_y, t.font_body, t.accent);
    cursor_y += 30;

    char price_text[256];
    snprintf(price_text, sizeof(price_text), "YES: %.0f¢ / %.0f¢ (bid/ask)", market.yes_bid_cents(), market.yes_ask_cents());
    DrawText(price_text, x + 20, cursor_y, t.font_small, t.positive);
    cursor_y += 25;

    snprintf(price_text, sizeof(price_text), "NO:  %.0f¢ / %.0f¢ (bid/ask)", market.no_bid_cents(), market.no_ask_cents());
    DrawText(price_text, x + 20, cursor_y, t.font_small, t.negative);
    cursor_y += 40;

    if (selected_comparison_.has_value()) {
        DrawText("WEATHER DATA", x + 10, cursor_y, t.font_body, t.accent);
        cursor_y += 30;
        const auto& comp = selected_comparison_.value();
        char weather_text[128];

        if (comp.forecast_temp.has_value()) {
            snprintf(weather_text, sizeof(weather_text), "Forecast (%s): %.1fF", comp.date.c_str(), comp.forecast_temp.value());
            DrawText(weather_text, x + 20, cursor_y, t.font_small, t.text);
        } else {
            snprintf(weather_text, sizeof(weather_text), "Forecast (%s): N/A", comp.date.c_str());
            DrawText(weather_text, x + 20, cursor_y, t.font_small, t.text_dim);
        }
        cursor_y += 25;

        if (comp.actual_temp.has_value()) {
            snprintf(weather_text, sizeof(weather_text), "Actual Temp: %.1fF", comp.actual_temp.value());
            DrawText(weather_text, x + 20, cursor_y, t.font_small, t.text);
        } else {
            DrawText("Actual Temp: Pending", x + 20, cursor_y, t.font_small, t.text_dim);
        }
        cursor_y += 25;

        if (!comp.settlement.empty()) {
            const char* checkmark = (comp.settlement == "yes") ? " Y" : " N";
            snprintf(weather_text, sizeof(weather_text), "Settlement: %s%s", comp.settlement.c_str(), checkmark);
            Color settlement_color = (comp.settlement == "yes") ? t.positive : t.negative;
            DrawText(weather_text, x + 20, cursor_y, t.font_small, settlement_color);
        } else {
            DrawText("Settlement: Pending", x + 20, cursor_y, t.font_small, t.text_dim);
        }
        cursor_y += 40;
    }

    if (!selected_trades_.empty()) {
        DrawText("PRICE HISTORY", x + 10, cursor_y, t.font_body, t.accent);
        cursor_y += 25;

        ui::ChartSeries price_series;
        price_series.color = t.accent;
        price_series.label = "YES";
        for (size_t i = 0; i < selected_trades_.size(); i++) {
            float price = (float)selected_trades_[i].yes_price_cents();
            price_series.points.push_back({(float)i, price});
        }

        ui::ChartOptions price_opts;
        price_opts.y_min = 0;
        price_opts.y_max = 100;
        if (!selected_trades_.empty()) {
            const auto& first_time = selected_trades_.back().created_time;
            const auto& last_time = selected_trades_.front().created_time;
            if (first_time.size() >= 10) price_opts.x_label_start = first_time.substr(5, 5);
            if (last_time.size() >= 10) price_opts.x_label_end = last_time.substr(5, 5);
        }

        int chart_h = std::min(140, (y + h - cursor_y - 10) / 2);
        if (chart_h > 60) {
            Rectangle chart_bounds = {(float)(x + 10), (float)cursor_y, (float)(w - 20), (float)chart_h};
            ui::DrawLineChart(chart_bounds, {price_series}, price_opts);
            cursor_y += chart_h + 15;
        }
    }

    if (selected_comparison_summary_.has_value() && !selected_comparison_summary_->points.empty()) {
        DrawText("TEMPERATURE", x + 10, cursor_y, t.font_body, t.accent);
        cursor_y += 25;

        ui::ChartSeries forecast_series, actual_series;
        forecast_series.color = t.accent;
        forecast_series.label = "Forecast";
        actual_series.color = t.positive;
        actual_series.label = "Actual";

        const auto& points = selected_comparison_summary_->points;
        for (size_t i = 0; i < points.size(); i++) {
            if (points[i].forecast_temp.has_value()) {
                forecast_series.points.push_back({(float)i, (float)points[i].forecast_temp.value()});
            }
            if (points[i].actual_temp.has_value()) {
                actual_series.points.push_back({(float)i, (float)points[i].actual_temp.value()});
            }
        }

        ui::ChartOptions temp_opts;
        if (!points.empty()) {
            const auto& first_date = points.front().date;
            const auto& last_date = points.back().date;
            if (first_date.size() >= 10) temp_opts.x_label_start = first_date.substr(5, 5);
            if (last_date.size() >= 10) temp_opts.x_label_end = last_date.substr(5, 5);
        }

        std::vector<ui::ChartSeries> temp_series;
        if (!forecast_series.points.empty()) temp_series.push_back(forecast_series);
        if (!actual_series.points.empty()) temp_series.push_back(actual_series);

        int chart_h = std::min(140, y + h - cursor_y - 10);
        if (chart_h > 60 && !temp_series.empty()) {
            Rectangle chart_bounds = {(float)(x + 10), (float)cursor_y, (float)(w - 20), (float)chart_h};
            ui::DrawLineChart(chart_bounds, temp_series, temp_opts);
        }
    }
}

void App::DrawTickerBar(int x, int y, int w, int h) const {
    auto& t = ui::theme();
    DrawRectangle(x, y, w, h, t.bg_panel);
    DrawLine(x, y, x + w, y, t.border);
    DrawText("KXBIDEN:45¢ ▲2  |  KXTRUMP:52¢ ▼1  |  KXFED:78¢ ▲5", x + 10, y + 6, t.font_small, t.text);
}

} // namespace predibloom
