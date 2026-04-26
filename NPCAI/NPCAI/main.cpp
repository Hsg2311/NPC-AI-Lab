// main.cpp - NPC AI 시뮬레이터 (2D 시각화 포함)
//
// 콘솔 서브시스템은 의도적으로 유지:
//   - 콘솔 창: Logger의 실시간 상태 전이 로그 출력
//   - WinAPI 창: 2D 시각 디버그 뷰 표시
//
// 키: Space = 일시정지/재개   S = 한 틱 진행   Esc = 종료

#include "viz/Application.hpp"
#include <iostream>

int main() {
    // 콘솔 (로그)과 시각화 창, 두 창이 함께 표시된다.
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
