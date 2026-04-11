#include "Renderer.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace viz {

// ─── Color table ─────────────────────────────────────────────────────────────
// NpcState int: 0=Idle  1=Chase  2=Attack  3=Return  4=Dead

COLORREF Renderer::npcStateColor(int state) {
    switch (state) {
        case 0: return RGB(140, 140, 140);  // Idle   – gray
        case 1: return RGB(220,  50,  50);  // Chase  – red
        case 2: return RGB(255, 165,   0);  // Attack – orange
        case 3: return RGB( 50, 200,  80);  // Return – green
        case 4: return RGB( 40,  40,  40);  // Dead   – black
    }
    return RGB(255, 255, 255);
}

// ─── Coordinate transform ─────────────────────────────────────────────────────

POINT Renderer::worldToScreen(float x, float z, int w, int h) const {
    float sx = w * 0.5f + (x - camera_.worldCenterX) * camera_.scale;
    float sy = h * 0.5f + (z - camera_.worldCenterZ) * camera_.scale;
    return { static_cast<LONG>(sx), static_cast<LONG>(sy) };
}

// ─── GDI helpers ─────────────────────────────────────────────────────────────

void Renderer::drawCircleOutline(HDC hdc, POINT c, int r) {
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Ellipse(hdc, c.x - r, c.y - r, c.x + r, c.y + r);
    SelectObject(hdc, oldBrush);
}

void Renderer::drawFilledCircle(HDC hdc, POINT c, int r, COLORREF fill, COLORREF outline) {
    HPEN pen = CreatePen(PS_SOLID, 2, outline);
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN oldP = static_cast<HPEN>  (SelectObject(hdc, pen));
    HBRUSH oldB  = static_cast<HBRUSH>(SelectObject(hdc, brush));

    Ellipse(hdc, c.x - r, c.y - r, c.x + r, c.y + r);

    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(pen);
    DeleteObject(brush);
}

void Renderer::drawArrow(HDC hdc, POINT from, POINT to, COLORREF col) {
    HPEN pen = CreatePen(PS_SOLID, 2, col);
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));

    MoveToEx(hdc, from.x, from.y, nullptr);
    LineTo(hdc, to.x,   to.y);

    // Arrowhead
    float dx = static_cast<float>(to.x - from.x);
    float dy = static_cast<float>(to.y - from.y);
    float len = std::sqrtf(dx * dx + dy * dy);
    if (len > 0.5f) {
        float nx = dx / len;
        float ny = dy / len;
        float s  = 5.f;
        POINT L = { static_cast<LONG>(to.x - nx * s * 2 - ny * s),
                    static_cast<LONG>(to.y - ny * s * 2 + nx * s)
        };
        POINT R = { static_cast<LONG>(to.x - nx * s * 2 + ny * s),
                    static_cast<LONG>(to.y - ny * s * 2 - nx * s)
        };

        MoveToEx(hdc, to.x, to.y, nullptr);
        LineTo(hdc, L.x, L.y);
        MoveToEx(hdc, to.x, to.y, nullptr);
        LineTo(hdc, R.x, R.y);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

// ─── drawBackground ──────────────────────────────────────────────────────────

void Renderer::drawBackground(HDC hdc, int w, int h) {
    HBRUSH bg = CreateSolidBrush(RGB(22, 22, 35));
    RECT   rc = { 0, 0, w, h };
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
}

// ─── drawGrid ────────────────────────────────────────────────────────────────

void Renderer::drawGrid(HDC hdc, int w, int h) {
    float halfW  = (w * 0.5f) / camera_.scale;
    float halfH  = (h * 0.5f) / camera_.scale;
    float xMin   = camera_.worldCenterX - halfW;
    float xMax   = camera_.worldCenterX + halfW;
    float zMin   = camera_.worldCenterZ - halfH;
    float zMax   = camera_.worldCenterZ + halfH;
    float step   = 5.f;

    // Minor grid lines
    HPEN minorPen = CreatePen(PS_SOLID, 1, RGB(40, 42, 60));
    HPEN oldPen   = static_cast<HPEN>(SelectObject(hdc, minorPen));

    for (float x = std::floorf(xMin / step) * step; x <= xMax; x += step) {
        POINT a = worldToScreen(x, zMin, w, h);
        POINT b = worldToScreen(x, zMax, w, h);
        MoveToEx(hdc, a.x, a.y, nullptr);
        LineTo  (hdc, b.x, b.y);
    }
    for (float z = std::floorf(zMin / step) * step; z <= zMax; z += step) {
        POINT a = worldToScreen(xMin, z, w, h);
        POINT b = worldToScreen(xMax, z, w, h);
        MoveToEx(hdc, a.x, a.y, nullptr);
        LineTo  (hdc, b.x, b.y);
    }

    // Axis lines (brighter)
    SelectObject(hdc, oldPen);
    DeleteObject(minorPen);

    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(70, 72, 100));
    SelectObject(hdc, axisPen);

    { POINT a = worldToScreen(xMin, 0, w, h); POINT b = worldToScreen(xMax, 0, w, h);
      MoveToEx(hdc, a.x, a.y, nullptr); LineTo(hdc, b.x, b.y); }
    { POINT a = worldToScreen(0, zMin, w, h); POINT b = worldToScreen(0, zMax, w, h);
      MoveToEx(hdc, a.x, a.y, nullptr); LineTo(hdc, b.x, b.y); }

    SelectObject(hdc, oldPen);
    DeleteObject(axisPen);
}

// ─── drawTargetLine ──────────────────────────────────────────────────────────

void Renderer::drawTargetLine(HDC hdc, int w, int h,
                                const sim::DebugNpcEntry&  npc,
                                const sim::DebugSnapshot&  snap) {
    if (npc.targetId == 0) return;

    for (const auto& p : snap.players) {
        if (p.id != npc.targetId) continue;

        POINT from = worldToScreen(npc.x, npc.z, w, h);
        POINT to   = worldToScreen(p.x,   p.z,   w, h);

        HPEN pen    = CreatePen(PS_DOT, 1, RGB(255, 220, 60));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, from.x, from.y, nullptr);
        LineTo  (hdc, to.x,   to.y);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        break;
    }
}

// ─── drawNpc ─────────────────────────────────────────────────────────────────

void Renderer::drawNpc(HDC hdc, int w, int h,
                         const sim::DebugNpcEntry&  npc,
                         const sim::DebugSnapshot&  snap) {
    POINT   center = worldToScreen(npc.x, npc.z, w, h);
    COLORREF col   = npcStateColor(npc.state);

    // ── Detection range circle (thin, dim) ──────────────────────────────────
    {
        int     detPx  = static_cast<int>(npc.detectionRange * camera_.scale);
        COLORREF detCol = npc.alive
            ? RGB(GetRValue(col) / 3, GetGValue(col) / 3, GetBValue(col) / 3 + 20)
            : RGB(35, 35, 40);
        HPEN pen    = CreatePen(PS_SOLID, 1, detCol);
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        drawCircleOutline(hdc, center, detPx);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    if (npc.alive) {
        // ── Attack range circle (solid red, thin) ───────────────────────────
        {
            int  atkPx  = static_cast<int>(npc.attackRange * camera_.scale);
            HPEN pen    = CreatePen(PS_SOLID, 1, RGB(180, 40, 40));
            HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
            drawCircleOutline(hdc, center, atkPx);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        // ── Target line ─────────────────────────────────────────────────────
        drawTargetLine(hdc, w, h, npc, snap);
    }

    // ── Body circle ─────────────────────────────────────────────────────────
    {
        COLORREF bodyCol    = npc.alive ? col : RGB(35, 35, 35);
        COLORREF outlineCol = npc.alive
            ? RGB(std::min(255, GetRValue(col) + 60),
                  std::min(255, GetGValue(col) + 60),
                  std::min(255, GetBValue(col) + 60))
            : RGB(70, 70, 70);
        drawFilledCircle(hdc, center, 9, bodyCol, outlineCol);
    }

    // ── Facing arrow ────────────────────────────────────────────────────────
    if (npc.alive && (std::fabsf(npc.dirX) + std::fabsf(npc.dirZ)) > 0.05f) {
        POINT tip = {
            static_cast<LONG>(center.x + npc.dirX * 16.f),
            static_cast<LONG>(center.y + npc.dirZ * 16.f)
        };
        drawArrow(hdc, center, tip, col);
    }

    // ── Label ───────────────────────────────────────────────────────────────
    {
        static const char* stateNames[] = { "Idle","Chase","Attack","Return","Dead" };
        const char* sname = (npc.state >= 0 && npc.state < 5) ? stateNames[npc.state] : "?";
        char label[64];
        std::snprintf(label, sizeof(label), "%s [%s]", npc.name.c_str(), sname);

        SetTextColor(hdc, npc.alive ? col : RGB(70, 70, 70));
        SetBkMode   (hdc, TRANSPARENT);
        TextOutA    (hdc, center.x - 28, center.y + 12, label, static_cast<int>(std::strlen(label)));
    }
}

// ─── drawPlayer ──────────────────────────────────────────────────────────────

void Renderer::drawPlayer(HDC hdc, int w, int h,
                            const sim::DebugPlayerEntry& p) {
    if (!p.alive) return;

    POINT center = worldToScreen(p.x, p.z, w, h);

    drawFilledCircle(hdc, center, 10,
        RGB( 40,  90, 200),   // fill: blue
        RGB(120, 180, 255));  // outline: light blue

    // Facing arrow
    if ((std::fabsf(p.dirX) + std::fabsf(p.dirZ)) > 0.05f) {
        POINT tip = {
            static_cast<LONG>(center.x + p.dirX * 18.f),
            static_cast<LONG>(center.y + p.dirZ * 18.f)
        };
        drawArrow(hdc, center, tip, RGB(180, 220, 255));
    }

    // Name label
    SetTextColor(hdc, RGB(140, 200, 255));
    SetBkMode   (hdc, TRANSPARENT);
    TextOutA    (hdc, center.x - 10, center.y - 24,
                 p.name.c_str(), static_cast<int>(p.name.size()));
}

// ─── drawHUD ─────────────────────────────────────────────────────────────────

void Renderer::drawHUD(HDC hdc, int w, int h,
                         const sim::DebugSnapshot& snap) {
    HFONT font = CreateFontA(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_MODERN, "Consolas");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));

    SetBkMode(hdc, TRANSPARENT);

    // Tick counter
    char tickBuf[64];
    std::snprintf(tickBuf, sizeof(tickBuf), "Tick: %llu",
        static_cast<unsigned long long>(snap.tick));
    SetTextColor(hdc, RGB(200, 200, 200));
    TextOutA(hdc, 10, 10, tickBuf, static_cast<int>(std::strlen(tickBuf)));

    // Pause indicator
    if (snap.paused) {
        SetTextColor(hdc, RGB(255, 210, 50));
        const char* msg = "[ PAUSED ]  Space=Resume   S=Step";
        TextOutA(hdc, 10, 30, msg, static_cast<int>(std::strlen(msg)));
    } else {
        SetTextColor(hdc, RGB(80, 200, 100));
        const char* msg = "[ RUNNING ]  Space=Pause   Esc=Quit";
        TextOutA(hdc, 10, 30, msg, static_cast<int>(std::strlen(msg)));
    }

    // State legend (bottom-left)
    struct LegendEntry { const char* name; COLORREF col; };
    static const LegendEntry legend[] = {
        { "Idle",   RGB(140, 140, 140) },
        { "Chase",  RGB(220,  50,  50) },
        { "Attack", RGB(255, 165,   0) },
        { "Return", RGB( 50, 200,  80) },
        { "Dead",   RGB( 60,  60,  60) },
    };

    int ly = h - 120;
    SetTextColor(hdc, RGB(160, 160, 160));
    TextOutA(hdc, 10, ly, "NPC States:", 11);
    ly += 18;

    for (const auto& e : legend) {
        HBRUSH b = CreateSolidBrush(e.col);
        RECT   r = { 10, ly + 1, 24, ly + 13 };
        FillRect(hdc, &r, b);
        DeleteObject(b);

        SetTextColor(hdc, e.col);
        TextOutA(hdc, 28, ly, e.name, static_cast<int>(std::strlen(e.name)));
        ly += 17;
    }

    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

// ─── render (public entry) ───────────────────────────────────────────────────

void Renderer::render(HDC hdc, int clientW, int clientH,
                       const sim::DebugSnapshot& snapshot) {
    drawBackground(hdc, clientW, clientH);
    drawGrid(hdc, clientW, clientH);

    // NPCs first (behind players)
    for (const auto& npc : snapshot.npcs) {
        drawNpc(hdc, clientW, clientH, npc, snapshot);
    }

    // Players on top
    for (const auto& p : snapshot.players) {
        drawPlayer(hdc, clientW, clientH, p);
    }

    drawHUD(hdc, clientW, clientH, snapshot);
}

} // namespace viz
