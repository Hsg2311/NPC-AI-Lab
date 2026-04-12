#pragma once
#include "Actor.hpp"
#include <string>

namespace sim {

class Room;
class Player;

// ---- NPC State ---------------------------------------------------------------
enum class NpcState {
    Idle,           // 0  waiting, scanning detectionRange
    Chase,          // 1  pursuing target
    AttackWindup,   // 2  attack preparation (no movement)
    AttackRecover,  // 3  post-attack recovery
    Return,         // 4  walking back to spawn (full disengage)
    Reposition,     // 5  moving to a better attack slot
    Regroup,        // 6  rally toward squad target (temporary disengage)
    Dead            // 7  terminal state
};

// ---- Config ------------------------------------------------------------------
struct NpcConfig {
    float maxHp              = 80.f;
    float moveSpeed          = 4.f;
    float detectionRange     = 10.f;  // Idle aggro radius
    float attackRange        = 2.f;   // melee reach
    float chaseRange         = 22.f;  // leash: give up if target farther than this
    float maxChaseDistance   = 26.f;  // return if farther than this from home
    float attackDamage       = 10.f;
    float attackWindupTime   = 0.4f;  // seconds before hit lands
    float attackRecoverTime  = 0.6f;  // seconds before next windup
    float separationRadius   = 4.f;   // push away other NPCs within this radius
    float separationWeight   = 0.6f;  // separation force strength relative to chase
    bool  canReAggroOnReturn = true;  // re-aggro players while returning home
    float repositionRadius   = 3.0f;  // orbit radius for reposition slot (>= separationRadius * 0.7)
    int   overlapThreshold   = 2;     // trigger Reposition if this many NPCs are too close
};

// ---- Npc Class ---------------------------------------------------------------
class Npc : public Actor {
public:
    Npc(const std::string& name, const Vec3& pos, const NpcConfig& cfg = {});

    void update(float dt, Room& room) override;
    const char* typeName() const override { return "NPC"; }
    std::string dump() const override;

    // ---- Accessors -----------------------------------------------------------
    NpcState getState()            const { return state_; }
    uint32_t getTargetId()         const { return targetId_; }
    float    getDetectionRange()   const { return detectionRange_; }
    float    getAttackRange()      const { return attackRange_; }
    float    getChaseRange()       const { return chaseRange_; }
    Vec3     getSpawnPos()         const { return spawnPos_; }
    float    getSeparationRadius() const { return separationRadius_; }
    float    getWindupProgress()   const;
    float    getRecoverProgress()  const;
    bool     hasRepositionTarget()    const { return hasRepositionTarget_; }
    Vec3     getRepositionTargetPos() const { return repositionTarget_; }

    // ── Squad accessors ───────────────────────────────────────────────────
    int      getSquadId()    const { return squadId_; }
    bool     getIsLeader()   const { return isLeader_; }
    void     setSquadId    (int id)         { squadId_    = id; }
    void     setIsLeader   (bool v)         { isLeader_   = v; }
    void     setSquadTarget(uint32_t tid)   { squadTargetId_ = tid; }

private:
    // ---- State transition / Per-state update ---------------------------------
    void transitionTo(NpcState next, const char* reason);

    void updateIdle         (float dt, Room& room);
    void updateChase        (float dt, Room& room);
    void updateAttackWindup (float dt, Room& room);
    void updateAttackRecover(float dt, Room& room);
    void updateReturn       (float dt, Room& room);
    void updateReposition   (float dt, Room& room);
    void updateRegroup      (float dt, Room& room);
    void updateDead         ();

    // ---- Helpers -------------------------------------------------------------
    Actor*  resolveTarget        (Room& room) const;
    Player* selectBestTarget     (Room& room) const;
    float   evaluateTargetScore  (const Player* p, Room& room) const;
    Vec3    calcSeparationForce  (Room& room) const;
    Vec3    calcRepositionTarget (const Vec3& targetPos) const;
    bool    isTooFarFromHome     () const;
    bool    isOvercrowded        (Room& room) const;

    // ---- Data ----------------------------------------------------------------
    NpcState state_{ NpcState::Idle };
    Vec3     spawnPos_;
    uint32_t targetId_{ 0 };

    // Config copies
    float detectionRange_;
    float attackRange_;
    float chaseRange_;
    float maxChaseDistance_;
    float moveSpeed_;
    float attackDamage_;
    float attackWindupTime_;
    float attackRecoverTime_;
    float separationRadius_;
    float separationWeight_;
    bool  canReAggroOnReturn_;
    float repositionRadius_;
    int   overlapThreshold_;

    // Timers
    float windupTimer_{ 0.f };
    float recoverTimer_{ 0.f };
    float targetEvalTimer_{ 0.f };

    // Reposition
    Vec3 repositionTarget_{ 0.f, 0.f, 0.f };
    bool hasRepositionTarget_{ false };

    // ── Squad integration (set by Room::updateSquads before each NPC update) ─
    int      squadId_{ -1 };        // -1 = not in squad
    bool     isLeader_{ false };
    uint32_t squadTargetId_{ 0 };   // 0 = no override

    static constexpr float TARGET_EVAL_INTERVAL = 0.5f;
};

} // namespace sim
