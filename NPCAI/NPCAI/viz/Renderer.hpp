#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../sim/DebugSnapshot.hpp"

namespace viz {

// Camera: 월드→스크린 변환 파라미터.
// worldCenterX/Z: 화면 중앙에 대응하는 월드 좌표.
// scale: 월드 단위당 픽셀 수.
struct Camera {
    float worldCenterX = 20.f;
    float worldCenterZ = 0.f;
    float scale = 12.f;
};

class Renderer {
public:
    // 전달받은 (메모리) DC에 모든 요소를 렌더링한다.
    // clientW / clientH는 해당 DC의 픽셀 크기다.
    void render(HDC hdc, int clientW, int clientH, const sim::DebugSnapshot& snapshot);

    Camera& camera() { return camera_; }

private:
    Camera camera_;

    // ── 좌표 변환 ───────────────────────────────────────────────────────────
    POINT worldToScreen(float x, float z, int w, int h) const;

    // ── 드로우 패스 ─────────────────────────────────────────────────────────
    void drawBackground(HDC hdc, int w, int h);
    void drawGrid(HDC hdc, int w, int h);
    void drawNpc(HDC hdc, int w, int h, const sim::DebugNpcEntry& npc, const sim::DebugSnapshot& snap);
    void drawPlayer(HDC hdc, int w, int h, const sim::DebugPlayerEntry& p);
    void drawTargetLine(HDC hdc, int w, int h, const sim::DebugNpcEntry& npc, const sim::DebugSnapshot& snap);
    void drawHUD(HDC hdc, int w, int h, const sim::DebugSnapshot& snap);

    // ── GDI 헬퍼 ─────────────────────────────────────────────────────────
    void drawCircleOutline(HDC hdc, POINT center, int radiusPx);
    void drawFilledCircle(HDC hdc, POINT center, int radiusPx, COLORREF fill, COLORREF outline);
    void drawArrow(HDC hdc, POINT from, POINT to, COLORREF col);
    void drawHomeMarker(HDC hdc, POINT center, COLORREF col);

    static COLORREF npcStateColor(int state);
};

} // namespace viz
