#pragma once
#include <vector>
#include <string>
#include <cstdint>

// 순수 데이터 - 렌더링 타입 없음, WinAPI 없음.
// Room::buildSnapshot()이 생성하고 viz::Renderer가 소비한다.
// 시뮬레이션과 렌더링 레이어 사이의 유일한 결합 지점이다.

namespace sim {

struct DebugPlayerEntry {
    int         id{ 0 };
    float       x{ 0.f };
    float       z{ 0.f };
    float       dirX{ 1.f };
    float       dirZ{ 0.f };  // XZ 평면 단위 방향 벡터
    std::string name;
    float       hp{ 0.f };
    float       maxHp{ 100.f };
    bool        alive{ true };
    int         aggroCount{ 0 };  // 이 플레이어를 타겟으로 Chase/Windup/Recover/Reposition 상태인 NPC 수
    // ── 공격 상태 ────────────────────────────────────────────────────────────
    int   attackState{ 0 };      // 0=없음 1=준비(Windup) 2=회복(Recover)
    float attackProgress{ 0.f }; // 0~1, 현재 단계 진행률
    float attackRange{ 2.5f };
    bool  isDummy{ false };  // DummyPlayerController가 조종하는 더미 플레이어 여부
};

// 상태: 0=Idle 1=Chase 2=AttackWindup 3=AttackRecover 4=Return 5=Reposition 6=Dead 7=Investigate
struct DebugNpcEntry {
    int         id{ 0 };
    float       x{ 0.f };
    float       z{ 0.f };
    float       dirX{ 1.f };
    float       dirZ{ 0.f };
    int         state{ 0 };
    int         targetId{ 0 };   // 0 = 타겟 없음
    std::string name;
    float       hp{ 0.f };
    float       maxHp{ 80.f };
    float       detectionRange{ 0.f };
    float       attackRange{ 0.f };
    bool        alive{ true };
    float       homeX{ 0.f };
    float       homeZ{ 0.f };
    float       windupProgress{ 0.f };   // 0~1, AttackWindup 진행률
    float       recoverProgress{ 0.f };  // 0~1, AttackRecover 진행률
    // ── 활동 구역 ─────────────────────────────────────────────────────────
    float activityZoneCenterX{ 0.f };
    float activityZoneCenterZ{ 0.f };
    float activityZoneRadius { 0.f };
    // ── NpcGroup ──────────────────────────────────────────────────────────
    int   groupId{ -1 };   // -1 = 그룹 없음
};

// 그룹 활동 구역 + 공유 메모리 위치 시각화용
struct DebugGroupEntry {
    int   groupId{ -1 };
    float centerX{ 0.f };
    float centerZ{ 0.f };
    float radius { 0.f };
    bool  hasMemory{ false };
    float memoryX{ 0.f };   // 마지막 목격 위치 (hasMemory == true 시 유효)
    float memoryZ{ 0.f };
};

// 상태: 0=Idle 1=Chase 2=AttackWindup 3=AttackRecover 4=Flank 5=ChargeThrough 6=Confused 7=Dead 8=HoldSlot 9=PressureWait
struct DebugTacticalNpcEntry {
    int         id{ 0 };
    float       x{ 0.f };
    float       z{ 0.f };
    float       dirX{ 1.f };
    float       dirZ{ 0.f };
    int         state{ 0 };
    int         targetId{ 0 };
    std::string name;
    float       hp{ 0.f };
    float       maxHp{ 100.f };
    float       attackRange{ 0.f };
    bool        alive{ true };
    float       homeX{ 0.f };
    float       homeZ{ 0.f };
    float       windupProgress{ 0.f };
    float       recoverProgress{ 0.f };
    int         squadId{ -1 };
    bool        isLeader{ false };
    float       slotX{ 0.f };  // Flank 상태 목적지 (시각화용)
    float       slotZ{ 0.f };
};

// FinalBoss 시각화용 — 상태 대신 활성 BT 리프 이름을 노출한다
struct DebugBossEntry {
    int         id{ 0 };
    float       x{ 0.f };
    float       z{ 0.f };
    float       dirX{ 1.f };
    float       dirZ{ 0.f };
    std::string name;
    std::string activeLeaf;          // 현재 활성 BT 리프 이름
    float       hp{ 0.f };
    float       maxHp{ 1000.f };
    float       attackRange{ 0.f };
    int         targetId{ 0 };       // 0 = 타겟 없음
    float       actionProgress{ 0.f }; // 0~1, 활성 액션 진행률
    bool        alive{ true };
};

struct DebugTelegraphEntry {
    float x{ 0.f };
    float z{ 0.f };
    float radius{ 0.f };
    float progress{ 0.f };
    int   kind{ 0 };
};

struct DebugSnapshot {
    uint64_t                           tick{ 0 };
    bool                               paused{ false };
    std::vector<DebugPlayerEntry>      players;
    std::vector<DebugNpcEntry>         npcs;
    std::vector<DebugGroupEntry>       groups;
    std::vector<DebugTacticalNpcEntry> tacticalNpcs;
    std::vector<DebugBossEntry>        bosses;
    std::vector<DebugTelegraphEntry>   telegraphs;
};

} // namespace sim
