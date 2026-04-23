#pragma once

#include <string>
#include <queue>
#include <functional>

namespace predibloom {

// Forward declaration
class App;

class ControlSocket {
public:
    ControlSocket();
    ~ControlSocket();

    void init();
    void poll();

    // Getters for pending actions
    bool hasPendingScroll() const { return !pending_scrolls_.empty(); }
    float popPendingScroll() { float s = pending_scrolls_.front(); pending_scrolls_.pop(); return s; }

    bool hasPendingButtonClick() const { return !pending_button_clicks_.empty(); }
    std::string popPendingButtonClick() { std::string b = pending_button_clicks_.front(); pending_button_clicks_.pop(); return b; }

    bool hasPendingMarketSelect() const { return !pending_market_selects_.empty(); }
    int popPendingMarketSelect() { int m = pending_market_selects_.front(); pending_market_selects_.pop(); return m; }

    // Callback for getting state
    using StateCallback = std::function<std::string()>;
    using ButtonListCallback = std::function<std::string()>;

    void setStateCallback(StateCallback cb) { state_callback_ = cb; }
    void setButtonListCallback(ButtonListCallback cb) { button_list_callback_ = cb; }

private:
    void handleCommands();
    void executeCommand(const std::string& cmd_json, std::string& response);

    int control_socket_ = -1;
    int client_socket_ = -1;
    std::string command_buffer_;

    std::queue<float> pending_scrolls_;
    std::queue<std::string> pending_button_clicks_;
    std::queue<int> pending_market_selects_;

    StateCallback state_callback_;
    ButtonListCallback button_list_callback_;
};

}  // namespace predibloom
