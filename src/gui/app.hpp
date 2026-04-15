#pragma once

namespace predibloom {

class App {
public:
    void Update(float dt);
    void Draw() const;

private:
    static constexpr int TOOLBAR_HEIGHT = 44;
    static constexpr int TICKER_HEIGHT = 36;
    static constexpr float LEFT_PANEL_RATIO = 0.35f;

    void DrawToolbar(int x, int y, int w, int h) const;
    void DrawLeftPanel(int x, int y, int w, int h) const;
    void DrawRightPanel(int x, int y, int w, int h) const;
    void DrawTickerBar(int x, int y, int w, int h) const;
};

} // namespace predibloom
