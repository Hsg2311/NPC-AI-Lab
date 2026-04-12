#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../sim/Room.hpp"
#include "Renderer.hpp"

namespace viz {

// Application owns the WinAPI window, the sim::Room, and the Renderer.
// Tick loop:
//   WM_TIMER (every ~16ms):
//     if (!paused_) room_.tick(DT)     <- simulate
//     snapshot_ = room_.buildSnapshot()
//     InvalidateRect                   <- request repaint
//   WM_PAINT:
//     double-buffer render via Renderer
//
// Keys:
//   Arrows – move player (HumanControl mode only)
//   Space  – toggle pause / resume
//   S      – step one tick (pause must be active)
//   Esc    – quit
//
// simMode_:
//   AutoWaypoint  – original P1/P2 auto-waypoint simulation
//   HumanControl  – 1 human-controlled player + same NPC layout

class Application {
public:
    Application();

    bool init(HINSTANCE hInst, int nCmdShow);
    void run();

private:
    // ── WinAPI plumbing ─────────────────────────────────────────────────────
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // ── Simulation helpers ──────────────────────────────────────────────────
    void setupSimulation();           // AutoWaypoint: P1/P2 + NPC (기존)
    void setupHumanSimulation();      // HumanControl: P1(human) + same NPCs
    void stepOneTick(HWND hwnd);

    // ── Simulation mode ─────────────────────────────────────────────────────
    enum class SimMode { AutoWaypoint, HumanControl };

    // ── Data ────────────────────────────────────────────────────────────────
    HINSTANCE hInst_{ nullptr };
    HWND hwnd_{ nullptr };
    bool paused_{ false };

    sim::Room room_;
    sim::DebugSnapshot snapshot_{};
    Renderer renderer_{};

    SimMode      simMode_{ SimMode::HumanControl };
    // ── Player control (HumanControl 모드 전용) ─────────────────────────────
    // keysHeld_: 0=Up  1=Down  2=Left  3=Right
    bool         keysHeld_[4]{};
    sim::Player* controlledPlayer_{ nullptr };   // non-owning

    static constexpr int TIMER_ID{ 1 };
    static constexpr UINT TIMER_MS{ 16 };        // ~60 FPS
    static constexpr float DT{ 1.f / 60.f };
    static constexpr int CLIENT_W{ 1280 };
    static constexpr int CLIENT_H{ 800 };
};

} // namespace viz
