// main.cpp — NPC AI Simulator with 2D Visualization
//
// Console subsystem is kept intentionally:
//   - console window shows real-time state-transition logs from Logger
//   - WinAPI window shows the 2D visual debug view
//
// Keys: Space = pause/resume   S = step   Esc = quit

#include "viz/Application.hpp"
#include <iostream>

int main() {
    // Both windows appear: console (logs) + the visualizer window.
    std::cout << "=== NPC AI Simulator  v2  (WinAPI + GDI) ===\n";
    std::cout << "  Space  Pause / Resume\n";
    std::cout << "  S      Step one tick (while paused)\n";
    std::cout << "  Esc    Quit\n\n";

    viz::Application app;
    if (!app.init(GetModuleHandle(nullptr), SW_SHOWDEFAULT)) {
        fprintf(stderr, "Application::init() failed\n");
        return 1;
    }

    app.run();
    return 0;
}
