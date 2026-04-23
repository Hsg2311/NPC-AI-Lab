#pragma once
#include "Actor.hpp"
#include "Squad.hpp"   // NpcCommand, NpcReport
#include <string>

namespace sim {

class Room;
class Player;

// ─── NpcState ────────────────────────────────────────────────────────────────
// int values used by Renderer (state color table) and DebugSnapshot.
enum class NpcState {
    Idle,           // 0   waiting; squad members await EngageTarget command
    Chase,          // 1   pursuing target
    AttackWindup,   // 2   attack preparation (no movement)
    AttackRecover,  // 3   post-attack cooldown
    Return,         // 4   walking back to spawn (standalone NPCs)
    Reposition,     // 5   sidestep away from crowded position
    Regroup,        // 6   rally toward squad target (legacy; standalone NPCs)
    Confused,       // 7   disoriented after leader death (NpcCommand::Confused)
    MoveToSlot,     // 8   moving to formation slot position (future formation use)
    Retreat,        // 9   command-driven retreat to destination
    Dead            // 10  terminal state
};

// ─── NpcConfig ───────────────────────────────────────────────────────────────
struct NpcConfig {
    float maxHp              = 80.f;
    float moveSpeed          = 4.f;
    float detectionRange     = 10.f;  // aggro radius (Idle / Broken squad scan)
    float attackRange        = 2.f;   // melee reach
    float chaseRange         = 22.f;  // leash: give up if target farther than this
    float maxChaseDistance   = 26.f;  // Return if farther than this from home
    float attackDamage       = 10.f;
    float attackWindupTime   = 0.4f;  // seconds before hit lands
    float attackRecoverTime  = 0.6f;  // seconds before next windup
    float separationRadius   = 4.f;   // push away NPCs within this radius
    float separationWeight   = 0.6f;  // separation force relative to chase force
    bool  canReAggroOnReturn = true;  // standalone NPCs only: re-aggro while returning
    int   overlapThreshold   = 2;     // trigger Reposition if this many NPCs too close
    float returnSpeedMult    = 2.5f;  // Return 상태 이동 속도 배율
};

// ─── Npc ─────────────────────────────────────────────────────────────────────
class Npc : public Actor {
public:
    Npc(const std::string& name, const Vec3& pos, const NpcConfig& cfg = {});

    void update(float dt, Room& room) override;
    const char* typeName() const override { return "NPC"; }
    std::string dump() const override;

    // ── Accessors ─────────────────────────────────────────────────────────────
    NpcState getState()            const { return state_; }
    uint32_t getTargetId()         const { return targetId_; }
    float    getDetectionRange()   const { return detectionRange_; }
    float    getAttackRange()      const { return attackRange_; }
    float    getChaseRange()       const { return chaseRange_; }
    Vec3     getSpawnPos()         const { return spawnPos_; }
    float    getSeparationRadius() const { return separationRadius_; }
    float    getWindupProgress()   const;
    float    getRecoverProgress()  const;

    // ── Squad integration ─────────────────────────────────────────────────────
    int      getSquadId()    const { return squadId_; }
    bool     getIsLeader()   const { return isLeader_; }
    void     setSquadId  (int id)       { squadId_  = id; }
    void     setIsLeader (bool v)       { isLeader_ = v; }
    void     setSquadTarget(uint32_t t) { squadTargetId_ = t; }  // legacy compat

    // Command interface (Squad → NPC)
    void      applyCommand(const NpcCommand& cmd);
    NpcReport buildReport() const;

private:
    // ── State transition / Per-state update ───────────────────────────────────
    void transitionTo(NpcState next, const char* reason);

    void updateIdle         (float dt, Room& room);
    void updateChase        (float dt, Room& room);
    void updateAttackWindup (float dt, Room& room);
    void updateAttackRecover(float dt, Room& room);
    void updateReturn       (float dt, Room& room);
    void updateReposition   (float dt, Room& room);
    void updateRegroup      (float dt, Room& room);
    void updateConfused     (float dt, Room& room);
    void updateMoveToSlot   (float dt, Room& room);
    void updateRetreat      (float dt, Room& room);
    void updateDead         ();

    // ── Helpers ───────────────────────────────────────────────────────────────
    Actor*  resolveTarget        (Room& room) const;
    Player* selectBestTarget     (Room& room) const;
    float   evaluateTargetScore  (const Player* p, Room& room) const;
    Vec3    calcSeparationForce  (Room& room) const;
    bool    isTooFarFromHome     () const;
    bool    isOvercrowded        (Room& room) const;

    // ── Data ──────────────────────────────────────────────────────────────────
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
    int   overlapThreshold_;
    float returnSpeedMult_;

    // Timers
    float windupTimer_{ 0.f };
    float recoverTimer_{ 0.f };
    float targetEvalTimer_{ 0.f };

    // Reposition
    Vec3  repositionDir_  { 1.f, 0.f, 0.f };  // perpendicular sidestep direction
    float repositionTimer_{ 0.f };

    // ── Squad fields ──────────────────────────────────────────────────────────
    int  leashBreakCount_{ 0 };       // 이번 귀환 중 누적 리쉬 위반 횟수
    bool countedThisEngage_{ false }; // 이번 교전에서 이미 카운트했으면 true (중복 방지)

    int      squadId_{ -1 };        // -1 = standalone (uses legacy self-target)
    bool     isLeader_{ false };
    uint32_t squadTargetId_{ 0 };   // legacy: squad target override

    // ── Command-driven fields (squad members only) ────────────────────────────
    Vec3  retreatDestination_{ 0.f, 0.f, 0.f };
    Vec3  slotDestination_   { 0.f, 0.f, 0.f };
    float commandSpeedMult_  { 1.f };   // speed multiplier from NpcCommand
    float confusedTimer_     { 0.f };
    Vec3  confusedDir_       { 1.f, 0.f, 0.f };

    static constexpr float TARGET_EVAL_INTERVAL  = 0.5f;
    static constexpr float CONFUSION_DURATION    = 3.0f;
    static constexpr float REPOSITION_TIMEOUT    = 1.5f;
    static constexpr float LEASH_REAGGRO_RATIO   = 0.75f; // 리쉬 위반 귀환 중 re-aggro 허용 기준 (maxChaseDistance_ 대비 비율)
    static constexpr int   MAX_LEASH_BREAKS      = 2;     // 이 횟수 이상 리쉬 위반 시 강제 귀환 (re-aggro 차단)
};

} // namespace sim
