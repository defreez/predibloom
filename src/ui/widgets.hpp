#pragma once

#include "raylib.h"
#include "theme.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace predibloom::ui {

// Use the global theme
using ::ui::theme;
using ::ui::Theme;

class Button {
public:
    std::string id;
    Rectangle bounds;
    std::string label;
    std::function<void()> on_click;
    bool is_tab = false;
    bool is_selected = false;

    Button(const std::string& id, Rectangle bounds, const std::string& label,
           std::function<void()> on_click = nullptr)
        : id(id), bounds(bounds), label(label), on_click(on_click) {}

    bool contains(Vector2 point) const {
        return CheckCollisionPointRec(point, bounds);
    }

    void click() {
        if (on_click) on_click();
    }

    void draw() const {
        auto& t = theme();
        if (is_tab) {
            // Tab style
            Color text_color = is_selected ? t.text : t.text_dim;
            DrawText(label.c_str(), (int)bounds.x + 10, (int)bounds.y + 12,
                     t.font_body, text_color);
            if (is_selected) {
                DrawRectangle((int)bounds.x, (int)bounds.y + (int)bounds.height - 4,
                              (int)bounds.width, 4, t.accent);
            }
        } else {
            // Regular button
            DrawRectangleRec(bounds, t.accent);
            DrawText(label.c_str(), (int)bounds.x + 5, (int)bounds.y + 2,
                     t.font_small - 2, t.text);
        }
    }
};

class WidgetManager {
public:
    void clear() {
        buttons_.clear();
    }

    void addButton(const Button& button) {
        buttons_.push_back(button);
    }

    Button* findButton(const std::string& id) {
        for (auto& btn : buttons_) {
            if (btn.id == id) return &btn;
        }
        return nullptr;
    }

    bool clickButton(const std::string& id) {
        if (auto* btn = findButton(id)) {
            btn->click();
            return true;
        }
        return false;
    }

    // Handle mouse click, returns true if a button was clicked
    bool handleClick(Vector2 point) {
        for (auto& btn : buttons_) {
            if (btn.contains(point)) {
                btn.click();
                return true;
            }
        }
        return false;
    }

    void drawAll() const {
        for (const auto& btn : buttons_) {
            btn.draw();
        }
    }

    std::vector<std::string> listButtonIds() const {
        std::vector<std::string> ids;
        for (const auto& btn : buttons_) {
            ids.push_back(btn.id);
        }
        return ids;
    }

    const std::vector<Button>& buttons() const { return buttons_; }

private:
    std::vector<Button> buttons_;
};

} // namespace predibloom::ui
