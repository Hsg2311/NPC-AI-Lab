#include "Renderer.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace viz {

// ─── 색상 테이블 ─────────────────────────────────────────────────────────────
// NpcState int 값: 0=Idle 1=Chase 2=AttackWindup 3=AttackRecover 4=Return 5=Reposition 6=Dead

COLORREF Renderer::npcStateColor(int state) {
    switch (state) {
        case 0: return RGB(140, 140, 140);  // Idle          - 회색
        case 1: return RGB(220,  50,  50);  // Chase         - 빨간색
        case 2: return RGB(255, 140,   0);  // AttackWindup  - 주황색
        case 3: return RGB(160,  70,   0);  // AttackRecover - 진한 주황색
        case 4: return RGB( 50, 200,  80);  // Return        - 초록색
        case 5: return RGB(160,  60, 200);  // Reposition    - 보라색
        case 6: return RGB( 40,  40,  40);  // Dead          - 거의 검정
    }
    return RGB(255, 255, 255);
}

// ─── 좌표 변환 ───────────────────────────────────────────────────────────────

POINT Renderer::worldToScreen(float x, float z, int w, int h) const {
    float sx = w * 0.5f + (x - camera_.worldCenterX) * camera_.scale;
    float sy = h * 0.5f + (z - camera_.worldCenterZ) * camera_.scale;
    return { static_cast<LONG>(sx), static_cast<LONG>(sy) };
}

// ─── GDI 헬퍼 ─────────────────────────────────────────────────────────────

void Renderer::drawCircleOutline(HDC hdc, POINT c, int r) {
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Ellipse(hdc, c.x - r, c.y - r, c.x + r, c.y + r);
    SelectObject(hdc, oldBrush);
}

void Renderer::drawFilledCircle(HDC hdc, POINT c, int r, COLORREF fill, COLORREF outline) {
    HPEN   pen   = CreatePen(PS_SOLID, 2, outline);
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN   oldP  = static_cast<HPEN>  (SelectObject(hdc, pen));
    HBRUSH oldB  = static_cast<HBRUSH>(SelectObject(hdc, brush));

    Ellipse(hdc, c.x - r, c.y - r, c.x + r, c.y + r);

    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(pen);
    DeleteObject(brush);
}

void Renderer::drawArrow(HDC hdc, POINT from, POINT to, COLORREF col) {
    HPEN pen    = CreatePen(PS_SOLID, 2, col);
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));

    MoveToEx(hdc, from.x, from.y, nullptr);
    LineTo  (hdc, to.x,   to.y);

    float dx  = static_cast<float>(to.x - from.x);
    float dy  = static_cast<float>(to.y - from.y);
    float len = std::sqrtf(dx * dx + dy * dy);
    if (len > 0.5f) {
        float nx = dx / len;
        float ny = dy / len;
        float s  = 5.f;
        POINT L = { static_cast<LONG>(to.x - nx * s * 2 - ny * s),
                    static_cast<LONG>(to.y - ny * s * 2 + nx * s) };
        POINT R = { static_cast<LONG>(to.x - nx * s * 2 + ny * s),
                    static_cast<LONG>(to.y - ny * s * 2 - nx * s) };
        MoveToEx(hdc, to.x, to.y, nullptr); LineTo(hdc, L.x, L.y);
        MoveToEx(hdc, to.x, to.y, nullptr); LineTo(hdc, R.x, R.y);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void Renderer::drawHomeMarker(HDC hdc, POINT c, COLORREF col) {
    int  s      = 5;
    HPEN pen    = CreatePen(PS_SOLID, 1, col);
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    MoveToEx(hdc, c.x - s, c.y - s, nullptr); LineTo(hdc, c.x + s, c.y + s);
    MoveToEx(hdc, c.x + s, c.y - s, nullptr); LineTo(hdc, c.x - s, c.y + s);
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
    float halfW = (w * 0.5f) / camera_.scale;
    float halfH = (h * 0.5f) / camera_.scale;
    float xMin  = camera_.worldCenterX - halfW;
    float xMax  = camera_.worldCenterX + halfW;
    float zMin  = camera_.worldCenterZ - halfH;
    float zMax  = camera_.worldCenterZ + halfH;
    float step  = 5.f;

    HPEN minorPen = CreatePen(PS_SOLID, 1, RGB(40, 42, 60));
    HPEN oldPen   = static_cast<HPEN>(SelectObject(hdc, minorPen));

    for (float x = std::floorf(xMin / step) * step; x <= xMax; x += step) {
        POINT a = worldToScreen(x, zMin, w, h);
        POINT b = worldToScreen(x, zMax, w, h);
        MoveToEx(hdc, a.x, a.y, nullptr); LineTo(hdc, b.x, b.y);
    }
    for (float z = std::floorf(zMin / step) * step; z <= zMax; z += step) {
        POINT a = worldToScreen(xMin, z, w, h);
        POINT b = worldToScreen(xMax, z, w, h);
        MoveToEx(hdc, a.x, a.y, nullptr); LineTo(hdc, b.x, b.y);
    }

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
    POINT    center = worldToScreen(npc.x,     npc.z,     w, h);
    POINT    home   = worldToScreen(npc.homeX, npc.homeZ, w, h);
    COLORREF col    = npcStateColor(npc.state);

    // ── 홈 마커 (X) ─────────────────────────────────────────────────────────
    {
        COLORREF hcol = npc.alive ? RGB(70, 70, 100) : RGB(40, 40, 50);
        drawHomeMarker(hdc, home, hcol);
    }

    // ── 활동 구역 (구역 중심 기준 점선 원) ──────────────────────────────────
    if (npc.alive && npc.activityZoneRadius > 0.f) {
        POINT zonePt = worldToScreen(npc.activityZoneCenterX, npc.activityZoneCenterZ, w, h);
        int   zonePx = static_cast<int>(npc.activityZoneRadius * camera_.scale);
        HPEN pen    = CreatePen(PS_DASH, 1, RGB(50, 110, 60));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        drawCircleOutline(hdc, zonePt, zonePx);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    // ── Return 상태: NPC에서 홈 위치까지 점선 ───────────────────────────────
    if (npc.alive && npc.state == 4 /* Return */) {
        HPEN pen    = CreatePen(PS_DOT, 1, RGB(50, 200, 80));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, center.x, center.y, nullptr);
        LineTo  (hdc, home.x,   home.y);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    // ── 감지 범위 원 (흐리게) ──────────────────────────────────────────────
    {
        int      detPx  = static_cast<int>(npc.detectionRange * camera_.scale);
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
        // ── 공격 범위 원 (빨간색) ────────────────────────────────────────────
        {
            int  atkPx  = static_cast<int>(npc.attackRange * camera_.scale);
            HPEN pen    = CreatePen(PS_SOLID, 1, RGB(180, 40, 40));
            HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
            drawCircleOutline(hdc, center, atkPx);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        // ── 타겟 선 (점선 노란색) ────────────────────────────────────────────
        drawTargetLine(hdc, w, h, npc, snap);

    }

    // ── 몸체 원 ─────────────────────────────────────────────────────────────
    {
        COLORREF bodyCol    = npc.alive ? col : RGB(35, 35, 35);
        COLORREF outlineCol = npc.alive
            ? RGB(std::min(255, GetRValue(col) + 60),
                  std::min(255, GetGValue(col) + 60),
                  std::min(255, GetBValue(col) + 60))
            : RGB(70, 70, 70);
        drawFilledCircle(hdc, center, 9, bodyCol, outlineCol);
    }

    // ── Windup / Recover 진행 바 (몸체 위 20x4 px) ──────────────────────────
    if (npc.alive && (npc.state == 2 || npc.state == 3)) {
        float    progress = (npc.state == 2) ? npc.windupProgress : npc.recoverProgress;
        const int BAR_W   = 20;
        const int BAR_H   = 4;
        int bx = center.x - BAR_W / 2;
        int by = center.y - 18;

        {
            RECT     bgR = { bx, by, bx + BAR_W, by + BAR_H };
            HBRUSH   bg  = CreateSolidBrush(RGB(40, 40, 40));
            FillRect(hdc, &bgR, bg);
            DeleteObject(bg);
        }
        int fillW = static_cast<int>(BAR_W * progress);
        if (fillW > 0) {
            COLORREF fc  = (npc.state == 2) ? RGB(255, 160, 0) : RGB(100, 55, 0);
            RECT     fR  = { bx, by, bx + fillW, by + BAR_H };
            HBRUSH   fb  = CreateSolidBrush(fc);
            FillRect(hdc, &fR, fb);
            DeleteObject(fb);
        }
    }

    // ── 방향 화살표 ─────────────────────────────────────────────────────────
    if (npc.alive && (std::fabsf(npc.dirX) + std::fabsf(npc.dirZ)) > 0.05f) {
        POINT tip = {
            static_cast<LONG>(center.x + npc.dirX * 16.f),
            static_cast<LONG>(center.y + npc.dirZ * 16.f)
        };
        drawArrow(hdc, center, tip, col);
    }

    // ── 레이블: 이름 [상태] ──────────────────────────────────────────────────
    {
        static const char* stateNames[] = {
            "Idle","Chase","Windup","Recover","Return","Repos","Dead"
        };
        const char* sname = (npc.state >= 0 && npc.state < 7)
            ? stateNames[npc.state] : "?";
        char label[80];
        std::snprintf(label, sizeof(label), "%s [%s]", npc.name.c_str(), sname);

        SetTextColor(hdc, npc.alive ? col : RGB(70, 70, 70));
        SetBkMode   (hdc, TRANSPARENT);
        TextOutA    (hdc, center.x - 28, center.y + 12,
                     label, static_cast<int>(std::strlen(label)));
    }
}

// ─── drawPlayer ──────────────────────────────────────────────────────────────

void Renderer::drawPlayer(HDC hdc, int w, int h,
                            const sim::DebugPlayerEntry& p) {
    if (!p.alive) return;

    POINT center = worldToScreen(p.x, p.z, w, h);

    drawFilledCircle(hdc, center, 10,
        RGB( 40,  90, 200),
        RGB(120, 180, 255));

    if ((std::fabsf(p.dirX) + std::fabsf(p.dirZ)) > 0.05f) {
        POINT tip = {
            static_cast<LONG>(center.x + p.dirX * 18.f),
            static_cast<LONG>(center.y + p.dirZ * 18.f)
        };
        drawArrow(hdc, center, tip, RGB(180, 220, 255));
    }

    SetTextColor(hdc, RGB(140, 200, 255));
    SetBkMode   (hdc, TRANSPARENT);
    TextOutA    (hdc, center.x - 10, center.y - 24,
                 p.name.c_str(), static_cast<int>(p.name.size()));

    // ── Aggro count (빨간 텍스트로 플레이어 옆에 표시) ──────────────────────
    if (p.aggroCount > 0) {
        char aggroBuf[16];
        std::snprintf(aggroBuf, sizeof(aggroBuf), "x%d", p.aggroCount);
        SetTextColor(hdc, RGB(255, 80, 80));
        TextOutA(hdc, center.x + 12, center.y - 8,
                 aggroBuf, static_cast<int>(std::strlen(aggroBuf)));
    }
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

    // 틱 카운터
    char tickBuf[64];
    std::snprintf(tickBuf, sizeof(tickBuf), "Tick: %llu",
        static_cast<unsigned long long>(snap.tick));
    SetTextColor(hdc, RGB(200, 200, 200));
    TextOutA(hdc, 10, 10, tickBuf, static_cast<int>(std::strlen(tickBuf)));

    // 일시정지 표시
    if (snap.paused) {
        SetTextColor(hdc, RGB(255, 210, 50));
        const char* msg = "[ PAUSED ]  Space=Resume   S=Step";
        TextOutA(hdc, 10, 30, msg, static_cast<int>(std::strlen(msg)));
    } else {
        SetTextColor(hdc, RGB(80, 200, 100));
        const char* msg = "[ RUNNING ]  Space=Pause   Esc=Quit";
        TextOutA(hdc, 10, 30, msg, static_cast<int>(std::strlen(msg)));
    }

    // 플레이어 어그로 요약
    int aggroY = 55;
    for (const auto& p : snap.players) {
        if (!p.alive) continue;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%s aggro: %d", p.name.c_str(), p.aggroCount);
        SetTextColor(hdc, RGB(140, 200, 255));
        TextOutA(hdc, 10, aggroY, buf, static_cast<int>(std::strlen(buf)));
        aggroY += 16;
    }

    // 상태 범례 (좌측 하단) - 7개 항목
    struct LegendEntry { const char* name; COLORREF col; };
    static const LegendEntry legend[] = {
        { "Idle",    RGB(140, 140, 140) },
        { "Chase",   RGB(220,  50,  50) },
        { "Windup",  RGB(255, 140,   0) },
        { "Recover", RGB(160,  70,   0) },
        { "Return",  RGB( 50, 200,  80) },
        { "Repos",   RGB(160,  60, 200) },
        { "Dead",    RGB( 40,  40,  40) },
    };

    int ly = h - 137;  // 7 entries × 17px + header 18px
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

// ─── render (공개 진입점) ────────────────────────────────────────────────────

void Renderer::render(HDC hdc, int clientW, int clientH,
                       const sim::DebugSnapshot& snapshot) {
    drawBackground(hdc, clientW, clientH);
    drawGrid(hdc, clientW, clientH);

    for (const auto& npc : snapshot.npcs) {
        drawNpc(hdc, clientW, clientH, npc, snapshot);
    }
    for (const auto& p : snapshot.players) {
        drawPlayer(hdc, clientW, clientH, p);
    }

    drawHUD(hdc, clientW, clientH, snapshot);
}

} // namespace viz
