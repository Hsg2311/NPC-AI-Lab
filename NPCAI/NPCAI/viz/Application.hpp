#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <memory>
#include "../sim/Room.hpp"
#include "../sim/Scenario.hpp"
#include "Renderer.hpp"

namespace viz {

// Application은 WinAPI 창, sim::Room, Renderer를 소유한다.
// 틱 루프:
//   WM_TIMER (약 16ms마다):
//     if (!paused_) room_.tick(DT)     <- 시뮬레이션 진행
//     snapshot_ = room_.buildSnapshot()
//     InvalidateRect                   <- 화면 갱신 요청
//   WM_PAINT:
//     Renderer를 통한 더블 버퍼 렌더링
//
// 키:
//   방향키 - 플레이어 이동
//   Space  - 일시정지 / 재개 전환
//   S      - 한 틱 진행 (일시정지 상태에서만)
//   Esc    - 종료

class Application {
public:
    Application();

    bool init(HINSTANCE hInst, int nCmdShow);
    void run();

private:
    // ── WinAPI 배선 ─────────────────────────────────────────────────────────
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // ── 시뮬레이션 헬퍼 ─────────────────────────────────────────────────────
    void stepOneTick(HWND hwnd);

    // ── 데이터 ──────────────────────────────────────────────────────────────
    HINSTANCE hInst_{ nullptr };
    HWND hwnd_{ nullptr };
    bool paused_{ false };

    sim::Room     room_;
    sim::DebugSnapshot snapshot_{};
    Renderer      renderer_{};

    std::unique_ptr<sim::Scenario> scenario_;

    // keysHeld_: 0=위 1=아래 2=왼쪽 3=오른쪽
    bool         keysHeld_[4]{};
    sim::Player* controlledPlayer_{ nullptr };   // 비소유 포인터, 시나리오에서 설정

    static constexpr int TIMER_ID{ 1 };
    static constexpr UINT TIMER_MS{ 16 };        // 약 60 FPS
    static constexpr float DT{ 1.f / 60.f };
    static constexpr int CLIENT_W{ 1280 };
    static constexpr int CLIENT_H{ 800 };
};

} // namespace viz
