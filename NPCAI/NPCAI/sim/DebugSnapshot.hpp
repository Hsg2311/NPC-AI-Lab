#pragma once
#include <vector>
#include <string>
#include <cstdint>

// Pure data -- no rendering types, no WinAPI.
// Produced by Room::buildSnapshot(), consumed by viz::Renderer.
// This is the only coupling point between the sim and rendering layers.

namespace sim {

struct DebugPlayerEntry {
    int         id{ 0 };
    float       x{ 0.f };
    float       z{ 0.f };
    float       dirX{ 1.f };
    float       dirZ{ 0.f };  // facing unit vector (XZ plane)
    std::string name;
    float       hp{ 0.f };
    float       maxHp{ 100.f };
    bool        alive{ true };
    int         aggroCount{ 0 };  // NPCs in Chase/Windup/Recover/Reposition targeting this player
};

// state: 0=Idle 1=Chase 2=AttackWindup 3=AttackRecover 4=Return 5=Reposition 6=Dead
struct DebugNpcEntry {
    int         id{ 0 };
    float       x{ 0.f };
    float       z{ 0.f };
    float       dirX{ 1.f };
    float       dirZ{ 0.f };
    int         state{ 0 };
    int         targetId{ 0 };   // 0 = no target
    std::string name;
    float       hp{ 0.f };
    float       maxHp{ 80.f };
    float       detectionRange{ 0.f };
    float       attackRange{ 0.f };
    bool        alive{ true };
    // -- New fields --
    float       homeX{ 0.f };
    float       homeZ{ 0.f };
    float       windupProgress{ 0.f };   // 0-1, AttackWindup fraction complete
    float       recoverProgress{ 0.f };  // 0-1, AttackRecover fraction complete
    bool        hasRepositionTarget{ false };
    float       repositionX{ 0.f };
    float       repositionZ{ 0.f };
    // ── Squad fields ──────────────────────────────────────────────────────
    int  squadId{ -1 };     // -1 = not in squad
    bool isLeader{ false };
};

struct DebugSnapshot {
    uint64_t                      tick{ 0 };
    bool                          paused{ false };
    std::vector<DebugPlayerEntry> players;
    std::vector<DebugNpcEntry>    npcs;
};

} // namespace sim
