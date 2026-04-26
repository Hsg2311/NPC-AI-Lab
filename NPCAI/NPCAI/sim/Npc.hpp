#pragma once
#include "Actor.hpp"
#include <string>

namespace sim {

class Room;
class Player;
class NpcGroup;

// ─── NpcState ────────────────────────────────────────────────────────────────
// int 값은 Renderer 색상 테이블 및 DebugSnapshot에서 사용된다.
enum class NpcState {
    Idle,           // 0   waiting; autonomous target selection
    Chase,          // 1   pursuing target
    AttackWindup,   // 2   attack preparation (no movement)
    AttackRecover,  // 3   post-attack cooldown
    Return,         // 4   walking back to spawn
    Reposition,     // 5   sidestep away from crowded position
    Dead,           // 6   terminal state
    Investigate,    // 7   group NPC: moving toward shared-memory last-known position
};

// ─── NpcConfig ───────────────────────────────────────────────────────────────
struct NpcConfig {
    float maxHp              = 80.f;
    float moveSpeed          = 4.f;
    float detectionRange     = 10.f;  // aggro 감지 반경
    float attackRange        = 2.f;   // 근접 공격 도달 거리
    float activityZoneRadius = 28.f;  // 활동 구역 반경 — 이 구역을 벗어나면 Return
    float attackDamage       = 10.f;
    float attackWindupTime   = 0.4f;  // 타격이 발동되기까지 걸리는 시간(초)
    float attackRecoverTime  = 0.6f;  // 다음 Windup까지 대기 시간(초)
    float separationRadius   = 4.f;   // 이 반경 내 다른 NPC를 밀어내는 거리
    float separationWeight   = 0.6f;  // 추격 방향 대비 분리 힘 비율
    bool  canReAggroOnReturn = true;  // 귀환 중 재어그로 허용 여부
    int   overlapThreshold   = 2;     // 이 수 이상 겹치면 Reposition 발동
    float returnSpeedMult    = 2.5f;  // Return 상태 이동 속도 배율
};

// ─── Npc ─────────────────────────────────────────────────────────────────────
class Npc : public Actor {
public:
    Npc(const std::string& name, const Vec3& pos, const NpcConfig& cfg = {});

    void update(float dt, Room& room) override;
    const char* typeName() const override { return "NPC"; }
    std::string dump() const override;

    // ── 접근자 ───────────────────────────────────────────────────────────────
    NpcState getState()              const { return state_; }
    uint32_t getTargetId()           const { return targetId_; }
    float    getDetectionRange()     const { return detectionRange_; }
    float    getAttackRange()        const { return attackRange_; }
    Vec3     getSpawnPos()           const { return spawnPos_; }
    Vec3     getActivityZoneCenter() const { return activityZoneCenter_; }
    float    getActivityZoneRadius() const { return activityZoneRadius_; }
    float    getSeparationRadius()   const { return separationRadius_; }
    float    getWindupProgress()     const;
    float    getRecoverProgress()    const;
    int      getGroupId()            const { return groupId_; }

    void setActivityZone(const Vec3& center, float radius) {
        activityZoneCenter_ = center;
        activityZoneRadius_ = radius;
    }
    void setGroupId(int id) { groupId_ = id; }

private:
    // ── 상태 전이 / 상태별 업데이트 ─────────────────────────────────────────
    void transitionTo(NpcState next, const char* reason);

    void updateIdle         (float dt, Room& room);
    void updateChase        (float dt, Room& room);
    void updateAttackWindup (float dt, Room& room);
    void updateAttackRecover(float dt, Room& room);
    void updateReturn       (float dt, Room& room);
    void updateReposition   (float dt, Room& room);
    void updateDead         ();
    void updateInvestigate  (float dt, Room& room);

    // ── 헬퍼 ──────────────────────────────────────────────────────────────────
    Actor*  resolveTarget           (Room& room) const;
    Player* selectBestTarget        (Room& room) const;
    float   evaluateTargetScore     (const Player* p, Room& room) const;
    Vec3    calcSeparationForce     (Room& room) const;
    bool    isOutsideActivityZone   () const;
    bool    isOvercrowded           (Room& room) const;

    // ── 데이터 ────────────────────────────────────────────────────────────────
    NpcState state_{ NpcState::Idle };
    Vec3     spawnPos_;
    uint32_t targetId_{ 0 };
    int      groupId_{ -1 };   // -1 = 독립 NPC (그룹 없음)

    // 설정 복사본
    float detectionRange_;
    float attackRange_;
    float moveSpeed_;
    float attackDamage_;
    float attackWindupTime_;
    float attackRecoverTime_;
    float separationRadius_;
    float separationWeight_;
    bool  canReAggroOnReturn_;
    int   overlapThreshold_;
    float returnSpeedMult_;

    // 활동 구역 — 스폰 위치 중심으로 설정
    Vec3  activityZoneCenter_;
    float activityZoneRadius_;

    // 타이머
    float windupTimer_{ 0.f };
    float recoverTimer_{ 0.f };
    float targetEvalTimer_{ 0.f };

    // 재배치
    Vec3  repositionDir_  { 1.f, 0.f, 0.f };
    float repositionTimer_{ 0.f };

    static constexpr float TARGET_EVAL_INTERVAL = 0.5f;
    static constexpr float REPOSITION_TIMEOUT   = 1.5f;
};

} // namespace sim
