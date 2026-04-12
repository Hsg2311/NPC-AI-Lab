#include "Application.hpp"
#include "../sim/Player.hpp"
#include "../sim/Npc.hpp"
#include <memory>
#include <cstdio>

namespace viz {

Application::Application()
    : room_(/*roomId=*/1, /*dumpInterval=*/0)   // disable console dump in visual mode
{}

// ─── init ────────────────────────────────────────────────────────────────────

bool Application::init(HINSTANCE hInst, int nCmdShow) {
    hInst_ = hInst;

    // Register window class
    WNDCLASSEXA wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.style          = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground  = nullptr;          // we paint the whole client area manually
    wc.lpszClassName  = "NPCAISimViz";
    wc.hIcon          = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(nullptr, "RegisterClassEx failed", "Error", MB_OK);
        return false;
    }

    // Compute window rect so the *client* area matches CLIENT_W x CLIENT_H
    RECT rc = { 0, 0, CLIENT_W, CLIENT_H };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowExA(
        0,
        "NPCAISimViz",
        "NPC AI Simulator  |  Space = Pause/Resume   S = Step   Esc = Quit",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, this);   // 'this' passed as lpCreateParams

    if (!hwnd_) {
        MessageBoxA(nullptr, "CreateWindowEx failed", "Error", MB_OK);
        return false;
    }

    setupSimulation();

    // Build initial snapshot so window isn't blank before first tick
    snapshot_        = room_.buildSnapshot();
    snapshot_.paused = paused_;

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    SetTimer(hwnd_, TIMER_ID, TIMER_MS, nullptr);

    return true;
}

// ─── run ─────────────────────────────────────────────────────────────────────

void Application::run() {
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

// ─── setupSimulation ─────────────────────────────────────────────────────────

void Application::setupSimulation() {
    using namespace sim;

    // ── Players ──────────────────────────────────────────────────────────────
    auto p1 = std::make_shared<Player>( "P1", Vec3{ 0.f, 0.f,  0.f }, 100.f, 10.f );
    auto p2 = std::make_shared<Player>( "P2", Vec3{ 30.f, 0.f,  0.f }, 100.f, 10.f );
    room_.addActor(p1);
    room_.addActor(p2);

    // P1 patrols a rectangle through the goblin camp
    room_.getDummyController().addControl(p1->getId(), {
        Vec3{  5.f, 0.f,  6.f },
        Vec3{ 16.f, 0.f,  6.f },
        Vec3{ 16.f, 0.f, -6.f },
        Vec3{  5.f, 0.f, -6.f },
    }, /*loop=*/true);

    // P2 walks back-and-forth past the Orc post
    room_.getDummyController().addControl(p2->getId(), {
        Vec3{ 22.f, 0.f, 0.f },
        Vec3{ 38.f, 0.f, 0.f },
    }, /*loop=*/true);

    // ── Goblins ──────────────────────────────────────────────────────────────
    NpcConfig goblin;
    goblin.maxHp              = 60.f;
    goblin.moveSpeed          = 5.5f;
    goblin.detectionRange     = 12.f;
    goblin.attackRange        = 1.8f;
    goblin.chaseRange         = 20.f;
    goblin.maxChaseDistance   = 24.f;
    goblin.attackDamage       = 8.f;
    goblin.attackWindupTime   = 0.30f;  // windup + recover ~= 0.9s (replaces attackCooldown)
    goblin.attackRecoverTime  = 0.60f;
    goblin.separationRadius   = 3.5f;
    goblin.separationWeight   = 0.7f;
    goblin.canReAggroOnReturn = true;
    goblin.repositionRadius   = 3.0f;   // >= separationRadius * 0.7 = 2.45
    goblin.overlapThreshold   = 2;

    // ── Orc ──────────────────────────────────────────────────────────────────
    NpcConfig orc;
    orc.maxHp              = 120.f;
    orc.moveSpeed          = 3.0f;
    orc.detectionRange     = 8.f;
    orc.attackRange        = 3.0f;
    orc.chaseRange         = 18.f;
    orc.maxChaseDistance   = 22.f;
    orc.attackDamage       = 22.f;
    orc.attackWindupTime   = 0.60f;  // windup + recover ~= 2.0s
    orc.attackRecoverTime  = 1.40f;
    orc.separationRadius   = 5.0f;
    orc.separationWeight   = 0.5f;
    orc.canReAggroOnReturn = false;  // 영역 수호형: Return 상태에서는 재어그로 없음
    orc.repositionRadius   = 4.0f;   // must be >= separationRadius * 0.7 = 3.5
    orc.overlapThreshold   = 1;      // 혼자 공격

    room_.addActor(std::make_shared<Npc>("Goblin01", Vec3{ 10.f, 0.f,  0.f }, goblin));
    room_.addActor(std::make_shared<Npc>("Goblin02", Vec3{ 13.f, 0.f,  3.f }, goblin));
    room_.addActor(std::make_shared<Npc>("Orc01",    Vec3{ 28.f, 0.f,  0.f }, orc));

    printf("[Sim] Room ready: 2 players, 3 NPCs\n");
}

// ─── stepOneTick ─────────────────────────────────────────────────────────────

void Application::stepOneTick(HWND hwnd) {
    room_.tick(DT);
    snapshot_        = room_.buildSnapshot();
    snapshot_.paused = paused_;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ─── WndProc (static) ────────────────────────────────────────────────────────

LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        // Store the Application* in the window's user data immediately
        auto* cs  = reinterpret_cast<CREATESTRUCT*>(lParam);
        auto* app = static_cast<Application*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->hwnd_ = hwnd;
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }

    auto* app = reinterpret_cast<Application*>(
        GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (app) return app->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ─── handleMessage ───────────────────────────────────────────────────────────

LRESULT Application::handleMessage(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    // ── Timer: advance simulation ─────────────────────────────────────────
    case WM_TIMER:
        if (wParam == TIMER_ID && !paused_) {
            stepOneTick(hwnd);
        }
        return 0;

    // ── Paint: double-buffered render ─────────────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT cr;
        GetClientRect(hwnd, &cr);
        int cw = cr.right;
        int ch = cr.bottom;

        HDC     memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, cw, ch);
        HBITMAP oldBM = static_cast<HBITMAP>(SelectObject(memDC, memBM));

        renderer_.render(memDC, cw, ch, snapshot_);

        BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    // ── Keyboard input ────────────────────────────────────────────────────
    case WM_KEYDOWN:
        if (wParam == VK_SPACE) {
            paused_ = !paused_;
            snapshot_.paused = paused_;
            InvalidateRect(hwnd, nullptr, FALSE);
            printf("[App] %s\n", paused_ ? "PAUSED" : "RESUMED");
        }
        else if (wParam == 'S' && paused_) {
            stepOneTick(hwnd);
            printf("[App] Step -> tick %llu\n",
                static_cast<unsigned long long>(snapshot_.tick));
        }
        else if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
        }
        return 0;

    // ── Prevent background erase (we fill ourselves) ──────────────────────
    case WM_ERASEBKGND:
        return 1;

    // ── Cleanup ───────────────────────────────────────────────────────────
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

} // namespace viz
