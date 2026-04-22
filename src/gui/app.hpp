#pragma once

#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <queue>
#include "../api/kalshi_client.hpp"
#include "../api/gribstream_client.hpp"
#include "../core/service.hpp"
#include "../core/config.hpp"
#include "../core/weather_comparison.hpp"
#include "../api/types.hpp"
#include "../ui/widgets.hpp"
#include "raylib.h"

namespace predibloom {

class App {
public:
    App();
    void Update(float dt);
    void Draw() const;

    void initControlSocket();
    void handleControlCommands();

private:
    static constexpr int TOOLBAR_HEIGHT = 44;
    static constexpr int TICKER_HEIGHT = 36;
    static constexpr float LEFT_PANEL_RATIO = 0.35f;
    static constexpr int MARKET_ROW_HEIGHT = 60;
    static constexpr int HEADER_HEIGHT = 30;

    void DrawToolbar(int x, int y, int w, int h) const;
    void DrawLeftPanel(int x, int y, int w, int h) const;
    void DrawRightPanel(int x, int y, int w, int h) const;
    void DrawTickerBar(int x, int y, int w, int h) const;

    void fetchMarkets();
    void fetchOrderbook(const std::string& ticker);

    void executeCommand(const std::string& cmd_json, std::string& response);
    std::string getStateJson() const;

    // Service layer
    std::unique_ptr<api::KalshiClient> client_;
    std::unique_ptr<api::GribStreamClient> gribstream_;
    std::unique_ptr<core::MarketService> service_;
    std::unique_ptr<core::WeatherComparisonService> comparison_service_;
    core::Config config_;

    // Tab state
    int selected_tab_idx_ = 0;

    // Market list state
    std::vector<api::Market> markets_;
    int selected_market_idx_ = -1;
    float scroll_offset_ = 0.0f;

    // Detail panel state
    std::optional<api::Orderbook> selected_orderbook_;
    std::optional<core::ComparisonPoint> selected_comparison_;
    std::vector<api::Trade> selected_trades_;
    std::optional<core::ComparisonSummary> selected_comparison_summary_;

    // UI state
    bool is_loading_ = false;
    std::string error_message_;

    // Control socket
    int control_socket_ = -1;
    int client_socket_ = -1;
    std::string command_buffer_;

    // Pending simulated inputs
    std::queue<std::string> pending_button_clicks_;
    std::queue<float> pending_scrolls_;

    // Widget system
    ui::WidgetManager widgets_;
    void rebuildWidgets();
};

} // namespace predibloom
