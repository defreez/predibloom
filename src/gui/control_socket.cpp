#include "control_socket.hpp"
#include "raylib.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace predibloom {

ControlSocket::ControlSocket() = default;

ControlSocket::~ControlSocket() {
    if (client_socket_ >= 0) close(client_socket_);
    if (control_socket_ >= 0) {
        close(control_socket_);
        unlink("/tmp/predibloom.sock");
    }
}

void ControlSocket::init() {
    const char* socket_path = "/tmp/predibloom.sock";
    unlink(socket_path);

    control_socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (control_socket_ < 0) {
        fprintf(stderr, "Failed to create control socket\n");
        return;
    }

    int flags = fcntl(control_socket_, F_GETFL, 0);
    fcntl(control_socket_, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(control_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to bind control socket\n");
        close(control_socket_);
        control_socket_ = -1;
        return;
    }

    if (listen(control_socket_, 5) < 0) {
        fprintf(stderr, "Failed to listen on control socket\n");
        close(control_socket_);
        control_socket_ = -1;
        return;
    }

    printf("Control socket listening on %s\n", socket_path);
}

void ControlSocket::poll() {
    if (control_socket_ < 0) return;
    handleCommands();
}

void ControlSocket::handleCommands() {
    if (client_socket_ < 0) {
        client_socket_ = accept(control_socket_, nullptr, nullptr);
        if (client_socket_ >= 0) {
            int flags = fcntl(client_socket_, F_GETFL, 0);
            fcntl(client_socket_, F_SETFL, flags | O_NONBLOCK);
        }
    }

    if (client_socket_ >= 0) {
        char buffer[4096];
        ssize_t bytes_read = recv(client_socket_, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            command_buffer_ += buffer;

            size_t newline_pos;
            while ((newline_pos = command_buffer_.find('\n')) != std::string::npos) {
                std::string cmd_json = command_buffer_.substr(0, newline_pos);
                command_buffer_.erase(0, newline_pos + 1);

                std::string response;
                executeCommand(cmd_json, response);
                response += "\n";
                send(client_socket_, response.c_str(), response.length(), 0);
            }
        } else if (bytes_read == 0 || (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(client_socket_);
            client_socket_ = -1;
            command_buffer_.clear();
        }
    }
}

void ControlSocket::executeCommand(const std::string& cmd_json, std::string& response) {
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
            if (button_list_callback_) {
                result["buttons"] = nlohmann::json::parse(button_list_callback_());
            } else {
                result["buttons"] = nlohmann::json::array();
            }
        }
        else if (command == "scroll") {
            float delta = cmd["delta"];
            pending_scrolls_.push(delta);
        }
        else if (command == "get_state") {
            if (state_callback_) {
                result["state"] = nlohmann::json::parse(state_callback_());
            }
        }
        else if (command == "select_market") {
            int idx = cmd["index"];
            pending_market_selects_.push(idx);
        }
        else if (command == "quit") {
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

}  // namespace predibloom
