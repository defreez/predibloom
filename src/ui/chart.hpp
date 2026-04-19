#pragma once

#include "theme.hpp"
#include "raylib.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace predibloom::ui {

// Use the global theme
using ::ui::theme;

struct ChartPoint {
    float x;
    float y;
};

struct ChartSeries {
    std::vector<ChartPoint> points;
    Color color;
    std::string label;
};

struct ChartOptions {
    std::string x_label_start;
    std::string x_label_end;
    int y_label_count = 4;       // Number of Y-axis labels
    float y_min = NAN;           // NAN = auto from data
    float y_max = NAN;           // NAN = auto from data
};

// Draw a line chart inside the given bounds.
// Each series is drawn as connected line segments.
// Y-axis auto-scales to data range (or uses options.y_min/y_max).
// X-axis maps ChartPoint::x values linearly across the plot area.
inline void DrawLineChart(Rectangle bounds, const std::vector<ChartSeries>& series,
                          const ChartOptions& opts = {}) {
    auto& t = theme();

    const int left_margin = 45;
    const int bottom_margin = 20;
    const int top_margin = 5;
    const int right_margin = 10;

    // Plot area
    float plot_x = bounds.x + left_margin;
    float plot_y = bounds.y + top_margin;
    float plot_w = bounds.width - left_margin - right_margin;
    float plot_h = bounds.height - top_margin - bottom_margin;

    if (plot_w <= 0 || plot_h <= 0) return;

    // Find data range
    float x_min = 1e30f, x_max = -1e30f;
    float y_min = 1e30f, y_max = -1e30f;

    for (const auto& s : series) {
        for (const auto& p : s.points) {
            x_min = std::min(x_min, p.x);
            x_max = std::max(x_max, p.x);
            y_min = std::min(y_min, p.y);
            y_max = std::max(y_max, p.y);
        }
    }

    // Override with options if provided
    if (!std::isnan(opts.y_min)) y_min = opts.y_min;
    if (!std::isnan(opts.y_max)) y_max = opts.y_max;

    // Handle degenerate ranges
    if (x_max <= x_min) { x_min -= 1; x_max += 1; }
    if (y_max <= y_min) { y_min -= 1; y_max += 1; }

    // Add small padding to Y range
    float y_pad = (y_max - y_min) * 0.05f;
    y_min -= y_pad;
    y_max += y_pad;

    // Mapping functions
    auto map_x = [&](float x) -> float {
        return plot_x + (x - x_min) / (x_max - x_min) * plot_w;
    };
    auto map_y = [&](float y) -> float {
        return plot_y + plot_h - (y - y_min) / (y_max - y_min) * plot_h;
    };

    // Draw background
    DrawRectangle((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height, t.bg_panel);

    // Draw grid lines and Y-axis labels
    int label_count = std::max(2, opts.y_label_count);
    for (int i = 0; i < label_count; i++) {
        float frac = (float)i / (label_count - 1);
        float val = y_min + frac * (y_max - y_min);
        float py = map_y(val);

        // Grid line
        DrawLine((int)plot_x, (int)py, (int)(plot_x + plot_w), (int)py, t.border);

        // Label
        char label[32];
        snprintf(label, sizeof(label), "%.0f", val);
        int tw = MeasureText(label, t.font_small - 4);
        DrawText(label, (int)(plot_x - tw - 4), (int)(py - 6), t.font_small - 4, t.text_dim);
    }

    // Draw X-axis labels
    if (!opts.x_label_start.empty()) {
        DrawText(opts.x_label_start.c_str(), (int)plot_x,
                 (int)(plot_y + plot_h + 4), t.font_small - 4, t.text_dim);
    }
    if (!opts.x_label_end.empty()) {
        int tw = MeasureText(opts.x_label_end.c_str(), t.font_small - 4);
        DrawText(opts.x_label_end.c_str(), (int)(plot_x + plot_w - tw),
                 (int)(plot_y + plot_h + 4), t.font_small - 4, t.text_dim);
    }

    // Draw border around plot area
    DrawLine((int)plot_x, (int)plot_y, (int)plot_x, (int)(plot_y + plot_h), t.border);
    DrawLine((int)plot_x, (int)(plot_y + plot_h), (int)(plot_x + plot_w), (int)(plot_y + plot_h), t.border);

    // Clip to plot area for data rendering
    BeginScissorMode((int)plot_x, (int)plot_y, (int)plot_w, (int)plot_h);

    // Draw series
    for (const auto& s : series) {
        if (s.points.size() < 2) continue;
        for (size_t i = 1; i < s.points.size(); i++) {
            int x1 = (int)map_x(s.points[i-1].x);
            int y1 = (int)map_y(s.points[i-1].y);
            int x2 = (int)map_x(s.points[i].x);
            int y2 = (int)map_y(s.points[i].y);
            DrawLine(x1, y1, x2, y2, s.color);
        }
    }

    EndScissorMode();

    // Draw legend (top-right of plot area)
    int legend_x = (int)(plot_x + plot_w);
    int legend_y = (int)(plot_y + 2);
    for (const auto& s : series) {
        if (s.label.empty()) continue;
        int tw = MeasureText(s.label.c_str(), t.font_small - 4);
        legend_x -= tw + 16;
        DrawRectangle(legend_x, legend_y + 2, 8, 8, s.color);
        DrawText(s.label.c_str(), legend_x + 12, legend_y, t.font_small - 4, t.text_dim);
    }
}

} // namespace predibloom::ui
