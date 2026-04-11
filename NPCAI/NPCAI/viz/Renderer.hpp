#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../sim/DebugSnapshot.hpp"

namespace viz {

// Camera: world→screen transform parameters.
// worldCenterX/Z: which world point maps to the screen center.
// scale: pixels per one world unit.
struct Camera {
    float worldCenterX = 20.f;
    float worldCenterZ = 0.f;
    float scale = 12.f;
};

class Renderer {
public:
    // Renders everything to the supplied (memory) DC.
    // clientW / clientH are the pixel dimensions of that DC.
    void render(HDC hdc, int clientW, int clientH, const sim::DebugSnapshot& snapshot);

    Camera& camera() { return camera_; }

private:
    Camera camera_;

    // ── Coordinate transform ────────────────────────────────────────────────
    POINT worldToScreen(float x, float z, int w, int h) const;

    // ── Draw passes ─────────────────────────────────────────────────────────
    void drawBackground(HDC hdc, int w, int h);
    void drawGrid(HDC hdc, int w, int h);
    void drawNpc(HDC hdc, int w, int h, const sim::DebugNpcEntry& npc, const sim::DebugSnapshot& snap);
    void drawPlayer(HDC hdc, int w, int h, const sim::DebugPlayerEntry& p);
    void drawTargetLine(HDC hdc, int w, int h, const sim::DebugNpcEntry& npc, const sim::DebugSnapshot& snap);
    void drawHUD(HDC hdc, int w, int h, const sim::DebugSnapshot& snap);

    // ── GDI helpers ─────────────────────────────────────────────────────────
    void drawCircleOutline(HDC hdc, POINT center, int radiusPx);
    void drawFilledCircle(HDC hdc, POINT center, int radiusPx, COLORREF fill, COLORREF outline);
    void drawArrow(HDC hdc, POINT from, POINT to, COLORREF col);

    static COLORREF npcStateColor(int state);
};

} // namespace viz
