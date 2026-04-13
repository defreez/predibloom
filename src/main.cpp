#include "raylib.h"
#include "app.hpp"
#include "ui/theme.hpp"

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Predibloom");
    MaximizeWindow();
    SetTargetFPS(60);

    predibloom::App app;

    while (!WindowShouldClose()) {
        app.Update(GetFrameTime());

        BeginDrawing();
        ClearBackground(ui::theme().bg_dark);
        app.Draw();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
