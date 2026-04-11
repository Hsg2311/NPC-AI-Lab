#pragma once
#include <vector>
#include <string>
#include <cstdint>

// Pure data — no rendering types, no WinAPI.
// Produced by Room::buildSnapshot(), consumed by viz::Renderer.
// This is the only coupling point between the sim and rendering layers.

namespace sim {

struct DebugPlayerEntry {
    int         id    = 0;
    float       x     = 0.f, z    = 0.f;
    float       dirX  = 1.f, dirZ = 0.f;  // facing unit vector (XZ plane)
    std::string name;
    float       hp    = 0.f, maxHp = 100.f;
    bool        alive = true;
};

struct DebugNpcEntry {
    int         id             = 0;
    float       x              = 0.f, z    = 0.f;
    float       dirX           = 1.f, dirZ = 0.f;
    int         state          = 0;   // NpcState cast to int: 0=Idle 1=Chase 2=Attack 3=Return 4=Dead
    int         targetId       = 0;   // 0 = no target
    std::string name;
    float       hp             = 0.f, maxHp = 80.f;
    float       detectionRange = 10.f;
    float       attackRange    = 2.f;
    bool        alive          = true;
};

struct DebugSnapshot {
    uint64_t                      tick   = 0;
    bool                          paused = false;
    std::vector<DebugPlayerEntry> players;
    std::vector<DebugNpcEntry>    npcs;
};

} // namespace sim
