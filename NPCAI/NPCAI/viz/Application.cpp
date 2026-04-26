#include "Application.hpp"
#include "../sim/ScenarioSoloNpc.hpp"
#include <cstdio>

namespace viz {

Application::Application()
    : room_(/*roomId=*/1, /*dumpInterval=*/0)   // 시각화 모드에서 콘솔 덤프 비활성화
{}

// ─── init ────────────────────────────────────────────────────────────────────

bool Application::init(HINSTANCE hInst, int nCmdShow) {
    hInst_ = hInst;

    // 윈도우 클래스 등록
    WNDCLASSEXA wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.style          = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground  = nullptr;          // 클라이언트 영역 전체를 직접 그림
    wc.lpszClassName  = "NPCAISimViz";
    wc.hIcon          = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(nullptr, "RegisterClassEx failed", "Error", MB_OK);
        return false;
    }

    // 클라이언트 영역이 CLIENT_W x CLIENT_H가 되도록 창 크기 계산
    RECT rc = { 0, 0, CLIENT_W, CLIENT_H };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowExA(
        0,
        "NPCAISimViz",
        "NPC AI Simulator  |  Arrows = Move   Space = Pause/Resume   S = Step   Esc = Quit",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, this);   // lpCreateParams로 this 포인터 전달

    if (!hwnd_) {
        MessageBoxA(nullptr, "CreateWindowEx failed", "Error", MB_OK);
        return false;
    }

    scenario_ = std::make_unique<sim::ScenarioSoloNpc>();
    scenario_->setup(room_);
    controlledPlayer_ = scenario_->controlledPlayer();

    // 첫 틱 전에 창이 비어 있지 않도록 초기 스냅샷 빌드
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

// ─── stepOneTick ─────────────────────────────────────────────────────────────

void Application::stepOneTick(HWND hwnd) {
    // ── 방향키 입력 → moveTarget 갱신 ───────────────────────────────────────
    if (controlledPlayer_ && controlledPlayer_->isAlive())
    {
        sim::Vec3 dir{};
        if (keysHeld_[0]) dir.z -= 1.f;   // 위
        if (keysHeld_[1]) dir.z += 1.f;   // 아래
        if (keysHeld_[2]) dir.x -= 1.f;   // 왼쪽
        if (keysHeld_[3]) dir.x += 1.f;   // 오른쪽

        sim::Vec3 pos = controlledPlayer_->getPosition();
        if (dir.lengthSq() > 0.5f)
            controlledPlayer_->setMoveTarget(pos + dir.normalized() * 100.f);
        else
            controlledPlayer_->setMoveTarget(pos);
    }

    room_.tick(DT);
    snapshot_        = room_.buildSnapshot();
    snapshot_.paused = paused_;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ─── WndProc (static) ────────────────────────────────────────────────────────

LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        // Application 포인터를 창 유저 데이터에 즉시 저장
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

    // ── 타이머: 시뮬레이션 진행 ──────────────────────────────────────────
    case WM_TIMER:
        if (wParam == TIMER_ID && !paused_) {
            stepOneTick(hwnd);
        }
        return 0;

    // ── 페인트: 더블 버퍼 렌더링 ─────────────────────────────────────────
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

    // ── 키보드 입력 ──────────────────────────────────────────────────────
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
        else if (wParam == VK_UP)    { keysHeld_[0] = true; }
        else if (wParam == VK_DOWN)  { keysHeld_[1] = true; }
        else if (wParam == VK_LEFT)  { keysHeld_[2] = true; }
        else if (wParam == VK_RIGHT) { keysHeld_[3] = true; }
        return 0;

    case WM_KEYUP:
        if      (wParam == VK_UP)    { keysHeld_[0] = false; }
        else if (wParam == VK_DOWN)  { keysHeld_[1] = false; }
        else if (wParam == VK_LEFT)  { keysHeld_[2] = false; }
        else if (wParam == VK_RIGHT) { keysHeld_[3] = false; }
        return 0;

    // ── 배경 지우기 방지 (직접 채움) ─────────────────────────────────────
    case WM_ERASEBKGND:
        return 1;

    // ── 정리 ─────────────────────────────────────────────────────────────
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

} // namespace viz
