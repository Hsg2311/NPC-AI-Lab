#pragma once
#include "Actor.hpp"
#include <string>

namespace sim {

// ─── State enum ────────────────────────────────────────────────────────────

enum class NpcState {
    Idle,
    Chase,
    Attack,
    Return,
    Dead
};

inline const char* npcStateStr(NpcState s) {
    switch (s) {
        case NpcState::Idle:   return "Idle";
        case NpcState::Chase:  return "Chase";
        case NpcState::Attack: return "Attack";
        case NpcState::Return: return "Return";
        case NpcState::Dead:   return "Dead";
    }
    return "?";
}

// ─── Config ────────────────────────────────────────────────────────────────

struct NpcConfig {
    float maxHp          = 80.f;
    float moveSpeed      = 4.f;
    float detectionRange = 10.f;  // Idle: aggro radius
    float attackRange    = 2.f;   // melee reach
    float chaseRange     = 22.f;  // leash: return if target further than this
    float attackDamage   = 10.f;
    float attackCooldown = 1.0f;  // seconds between hits
};

// ─── Npc ───────────────────────────────────────────────────────────────────

class Npc : public Actor {
public:
    Npc(const std::string& name, const Vec3& pos, const NpcConfig& cfg = {});

    void        update(float dt, Room& room) override;
    const char* typeName() const override { return "NPC"; }
    std::string dump()     const override;

    NpcState getState()          const { return state_; }
    float    getDetectionRange() const { return detectionRange_; }
    float    getAttackRange()    const { return attackRange_; }
    uint32_t getTargetId()       const { return targetId_; }

private:
    // ── State transition ───────────────────────────────────────────────────
    void transitionTo(NpcState newState, const std::string& reason = "");

    // ── Per-state update ───────────────────────────────────────────────────
    void updateIdle  (float dt, Room& room);
    void updateChase (float dt, Room& room);
    void updateAttack(float dt, Room& room);
    void updateReturn(float dt, Room& room);
    void updateDead  ();

    // ── Helpers ────────────────────────────────────────────────────────────
    Actor* resolveTarget(Room& room) const;  // nullptr if dead/gone

    // ── Data ───────────────────────────────────────────────────────────────
    NpcState state_          = NpcState::Idle;
    Vec3     spawnPos_;

    uint32_t targetId_       = 0;     // 0 = no target

    float    detectionRange_;
    float    attackRange_;
    float    chaseRange_;
    float    moveSpeed_;
    float    attackDamage_;
    float    attackCooldown_;
    float    attackTimer_    = 0.f;   // counts down to next attack
};

} // namespace sim
