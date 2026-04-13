#pragma once
#include "raylib.h"

namespace ui {

struct Theme {
    // Backgrounds
    Color bg_dark       = {30, 30, 30, 255};      // #1E1E1E
    Color bg_panel      = {37, 37, 37, 255};      // #252525
    Color bg_selected   = {58, 58, 58, 255};      // #3A3A3A

    // Accent
    Color accent        = {255, 149, 0, 255};     // #FF9500 Bloomberg orange

    // Text
    Color text          = {255, 255, 255, 255};   // #FFFFFF
    Color text_dim      = {136, 136, 136, 255};   // #888888

    // Grid/borders
    Color border        = {51, 51, 51, 255};      // #333333

    // Status
    Color positive      = {0, 255, 0, 255};       // #00FF00
    Color negative      = {255, 68, 68, 255};     // #FF4444

    // Font sizes
    int font_body       = 20;
    int font_small      = 18;
    int font_header     = 26;
};

inline Theme& theme() {
    static Theme t;
    return t;
}

} // namespace ui
