#pragma once
#include "Actor.hpp"
#include <string>
#include <vector>

namespace sim {

class Room;
class Player;
class TacticalSquad;

// ─── TacticalNpcState ────────────────────────────────────────────────────────
// int 값은 Renderer 색상 테이블 및 DebugSnapshot에서 사용된다.
enum class TacticalNpcState {
    Idle          = 0,  // 명령 대기 (자율 감지 없음)
    Chase         = 1,  // EngageTarget 명령 후 타겟 추격
    AttackWindup  = 2,  // 공격 준비 (이동 없음)
    AttackRecover = 3,  // 공격 후 쿨다운
    Flank         = 4,  // FlankTarget 명령: assignedSlot 위치까지 이동
    AlternateWait = 5,  // 교대 공격: 공격 순번 대기
    Return        = 6,  // Retreat 명령: spawnPos 귀환
    Dead          = 7,  // 종료 상태
};

// ─── TacticalCommand ─────────────────────────────────────────────────────────
enum class TacticalCommandType {
    None,
    EngageTarget,   // targetId_ 설정 후 Chase
    FlankTarget,    // targetId_ + assignedSlot_ 설정 후 Flank
    AlternateWait,  // 타겟 유지, 공격 순번 대기
    Retreat,        // spawnPos 귀환
    Idle,           // 전투 해제
    Confused,       // PlatoonLeader 사망: 방황
};

struct TacticalCommand {
    TacticalCommandType type       = TacticalCommandType::None;
    uint32_t            targetId   = 0;
    Vec3                slotOffset = {};  // 타겟 기준 오프셋 (Flank용)
};

// ─── TacticalNpcConfig ───────────────────────────────────────────────────────
struct TacticalNpcConfig {
    float maxHp            = 100.f;
    float moveSpeed        = 4.f;
    float attackRange      = 2.f;
    float attackDamage     = 15.f;
    float attackWindupTime = 0.4f;
    float attackRecoverTime= 0.8f;
    float separationRadius = 3.f;
    float separationWeight = 0.5f;
};

// ─── TacticalNpc ─────────────────────────────────────────────────────────────
class TacticalNpc : public Actor {
public:
    TacticalNpc(const std::string& name, const Vec3& pos,
                const TacticalNpcConfig& cfg = {});

    void update(float dt, Room& room) override;
    const char* typeName() const override { return "TacticalNpc"; }
    std::string dump() const override;

    // ── 명령 수신 (TacticalSquad가 호출) ────────────────────────────────────
    void receiveCommand(const TacticalCommand& cmd);

    // ── 접근자 ───────────────────────────────────────────────────────────────
    TacticalNpcState getState()         const { return state_; }
    uint32_t         getTargetId()      const { return targetId_; }
    int              getSquadId()       const { return squadId_; }
    Vec3             getSpawnPos()      const { return spawnPos_; }
    Vec3             getAssignedSlot()  const { return assignedSlot_; }
    float            getAttackRange()   const { return attackRange_; }
    float            getSeparationRadius() const { return separationRadius_; }
    float            getWindupProgress()   const;
    float            getRecoverProgress()  const;

    void setSquadId(int id) { squadId_ = id; }

protected:
    void transitionTo(TacticalNpcState next, const char* reason);

    // ── 상태별 업데이트 ──────────────────────────────────────────────────────
    void updateIdle         (float dt, Room& room);
    void updateChase        (float dt, Room& room);
    void updateAttackWindup (float dt, Room& room);
    void updateAttackRecover(float dt, Room& room);
    void updateFlank        (float dt, Room& room);
    void updateAlternateWait(float dt, Room& room);
    void updateReturn       (float dt, Room& room);
    void updateDead         ();

    // ── 헬퍼 ──────────────────────────────────────────────────────────────────
    void   consumePendingCommand();
    Actor* resolveTarget    (Room& room) const;
    Vec3   calcSeparationForce(const std::vector<Vec3>& nearby) const;

    // ── 데이터 ────────────────────────────────────────────────────────────────
    TacticalNpcState state_{ TacticalNpcState::Idle };
    TacticalCommand  pendingCmd_{};
    uint32_t         targetId_{ 0 };
    Vec3             assignedSlot_{};   // Flank 목적지 (월드 좌표)
    Vec3             spawnPos_;
    int              squadId_{ -1 };
    std::string      logPrefix_{};

    float moveSpeed_;
    float attackRange_;
    float attackDamage_;
    float attackWindupTime_;
    float attackRecoverTime_;
    float separationRadius_;
    float separationWeight_;

    float windupTimer_{ 0.f };
    float recoverTimer_{ 0.f };

    // 방황 타이머 (Confused 상태용)
    float confusedTimer_{ 0.f };
    Vec3  wanderDir_{ 1.f, 0.f, 0.f };
    static constexpr float CONFUSED_DURATION = 3.f;

    std::vector<Vec3> nearbyCache_;
};

} // namespace sim
