#include "raylib.h"
#include "app.hpp"
#include "../ui/theme.hpp"
#include <cstring>

int main(int argc, char** argv) {
    bool enable_control = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--control") == 0) {
            enable_control = true;
        }
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Predibloom");
    MaximizeWindow();
    SetTargetFPS(60);

    predibloom::App app;

    if (enable_control) {
        app.initControlSocket();
    }

    while (!WindowShouldClose()) {
        if (enable_control) {
            app.handleControlCommands();
        }

        app.Update(GetFrameTime());

        BeginDrawing();
        ClearBackground(ui::theme().bg_dark);
        app.Draw();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
