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
    ChargeThrough = 5,
    Confused      = 6,
    Dead          = 7,  // 종료 상태
    HoldSlot      = 8,  // DenseHold/Encircle/BoxAdvance 명령: 슬롯 이동 후 유지
    PressureWait  = 9,  // 공격 슬롯 대기: 플레이어 탈출 방향을 비운 외곽 압박
};

// ─── TacticalCommand ─────────────────────────────────────────────────────────
enum class TacticalCommandType {
    None,
    EngageTarget,   // targetId_ 설정 후 Chase
    FlankTarget,    // targetId_ + assignedSlot_ 설정 후 Flank
    Idle,           // 전투 해제
    Confused,       // PlatoonLeader 사망: 방황
    HoldSlot,       // assignedSlot 이동 후 유지 (공격 안 함, 경계용)
    GuardSlot,      // HoldSlot처럼 이동/정지하되 도착 후 가장 가까운 플레이어 주시
    ChargeThrough,
};

struct TacticalCommand {
    TacticalCommandType type             = TacticalCommandType::None;
    uint32_t            targetId         = 0;
    Vec3                slotOffset       = {};     // Flank/HoldSlot: 목적지 월드 좌표
    Vec3                slotRefTargetPos = {};     // 슬롯 계산 시점의 타겟 위치 (유효성 체크용)
    float               abandonDist      = 15.f;  // 타겟 이탈 시 슬롯 포기 거리 (Flank 전용)
    float               speedMult        = 1.f;   // Flank 이동 속도 배율 (동시 도착용)
    std::vector<uint32_t> targetIds       = {};
    uint32_t            chargeId          = 0;
    Vec3                chargeDir        = {};
    Vec3                chargeCenter     = {};
    float               impactRadius     = 3.f;
    float               impactDamage     = 0.f;
    float               passDistance     = 6.f;
    bool                useHoldFacing    = false;
    Vec3                holdFacing       = {};
};

// ─── TacticalNpcConfig ───────────────────────────────────────────────────────
struct TacticalNpcConfig {
    float maxHp            = 100.f;
    float moveSpeed        = 4.f;
    float attackRange      = 2.8f;
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
    TacticalNpcState getDisplayState()  const;
    uint32_t         getTargetId()      const { return targetId_; }
    int              getSquadId()       const { return squadId_; }
    Vec3             getSpawnPos()      const { return spawnPos_; }
    Vec3             getAssignedSlot()  const { return assignedSlot_; }
    float            getAttackRange()   const { return attackRange_; }
    float            getAttackDamage()  const { return attackDamage_; }
    float            getSeparationRadius() const { return separationRadius_; }
    TacticalNpcConfig getConfig()       const;
    float            getWindupProgress()   const;
    float            getRecoverProgress()  const;
    bool             isAtSlot()           const;  // Flank/HoldSlot 슬롯 도착 여부
    bool             isChargeComplete()   const { return chargeComplete_; }
    bool             isEligibleForAttackReservation(const Actor& target) const;

    void setSquadId(int id) { squadId_ = id; }
    void reviveAt(const Vec3& pos);

protected:
    void transitionTo(TacticalNpcState next, const char* reason);

    // ── 상태별 업데이트 ──────────────────────────────────────────────────────
    void updateIdle         (float dt, Room& room);
    void updateChase        (float dt, Room& room);
    void updateAttackWindup (float dt, Room& room);
    void updateAttackRecover(float dt, Room& room);
    void updateFlank        (float dt, Room& room);
    void updateChargeThrough(float dt, Room& room);
    void updateConfused     (float dt, Room& room);
    void updateHoldSlot     (float dt, Room& room);
    void updatePressureWait (float dt, Room& room);
    void updateDead         (Room& room);

    // ── 헬퍼 ──────────────────────────────────────────────────────────────────
    void   consumePendingCommand(Room& room);
    Actor* resolveTarget    (Room& room) const;
    bool   hasReservedAttackSlot() const;
    void   updateReservedAttackStaleTimer(float dt, Room& room);
    bool   canEnterAttackSlot(Room& room);
    void   releaseAttackReservation(Room& room);
    void   releaseStaleAttackReservation(Room& room, float currentDist);
    void   resetAttackReservationLease();
    void   resetPressureWaitTarget();
    void   refreshPressureWaitScatterOffsets();
    Vec3   computePressureWaitDesired(const Actor& target) const;
    void   moveTowardPressureWait(float dt, Room& room, const Actor& target);

    // ── 데이터 ────────────────────────────────────────────────────────────────
    TacticalNpcState state_{ TacticalNpcState::Idle };
    TacticalCommand  pendingCmd_{};
    uint32_t         targetId_{ 0 };
    Vec3             assignedSlot_{};      // Flank/HoldSlot 목적지 (월드 좌표)
    Vec3             slotRefTargetPos_{};  // 슬롯 발행 시점의 타겟 위치 (유효성 체크용)
    float            abandonDist_{ 15.f }; // Flank 슬롯 포기 거리
    uint32_t         chargeId_{ 0 };
    Vec3             chargeDir_{};
    Vec3             chargeCenter_{};
    float            impactRadius_{ 3.f };
    float            impactDamage_{ 0.f };
    float            passDistance_{ 6.f };
    bool             chargeComplete_{ false };
    bool             guardNearestPlayer_{ false };
    bool             useHoldFacing_{ false };
    Vec3             holdFacing_{};
    Vec3             confusedAnchor_{};
    Vec3             confusedTarget_{};
    float            confusedRetargetTimer_{ 0.f };
    int              confusedWanderStep_{ 0 };
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

    float speedMult_{ 1.f };      // Flank 이동 속도 배율 (동시 도착 보정)
    float windupTimer_{ 0.f };
    float recoverTimer_{ 0.f };
    float pressureWaitTimer_{ 0.f };
    Vec3  pressureWaitDesired_{};
    Vec3  pressureWaitTargetAnchor_{};
    Vec3  pressureWaitFacingAnchor_{ 1.f, 0.f, 0.f };
    float pressureWaitRetargetTimer_{ 0.f };
    float pressureWaitAngleOffset_{ 0.f };
    float pressureWaitRadiusOffset_{ 0.f };
    uint32_t pressureWaitScatterSeed_{ 0 };
    bool  pressureWaitDesiredValid_{ false };
    bool  pressureReentering_{ false };
    uint32_t reservedAttackTargetId_{ 0 };
    float reservedAttackStaleTimer_{ 0.f };
    float reservedAttackProgressDist_{ 0.f };
    uint32_t blockedAttackReservationTargetId_{ 0 };
    float blockedAttackReservationDist_{ 0.f };

    static constexpr float TACTICAL_SPEED_MULT = 3.0f;
    static constexpr int   MAX_TACTICAL_ATTACKERS_PER_TARGET = 5;
    static constexpr float TACTICAL_ATTACK_RESERVATION_MAX_DIST = 18.0f;
    static constexpr float TACTICAL_ATTACK_RESERVATION_STALE_TIME = 3.0f;
    static constexpr float TACTICAL_ATTACK_RESERVATION_PROGRESS_DIST = 0.5f;
    static constexpr float TACTICAL_PRESSURE_EXTRA_RADIUS = 9.0f;
    static constexpr float TACTICAL_PRESSURE_SEPARATION_MULT = 2.2f;
    static constexpr float TACTICAL_PRESSURE_SEPARATION_RADIUS_MULT = 1.35f;
    static constexpr float TACTICAL_PRESSURE_SPEED_MULT = 0.9f;
    static constexpr float TACTICAL_PRESSURE_REENTER_MIN_TIME = 0.45f;
    static constexpr float TACTICAL_PRESSURE_REENTER_STAGGER = 0.12f;
    static constexpr float TACTICAL_PRESSURE_REENTER_SPEED_MULT = 1.0f;
    static constexpr float TACTICAL_PRESSURE_RETARGET_INTERVAL = 0.35f;
    static constexpr float TACTICAL_PRESSURE_TARGET_MOVE_REFRESH_DIST = 1.5f;
    static constexpr float TACTICAL_PRESSURE_FACING_REFRESH_DOT = 0.9396926f; // cos(20 degrees)
    static constexpr float TACTICAL_PRESSURE_SLOW_RADIUS = 4.0f;
    static constexpr float TACTICAL_PRESSURE_STOP_RADIUS = 1.0f;
    static constexpr float TACTICAL_PRESSURE_SEPARATION_WEIGHT_MULT = 0.35f;
    static constexpr float TACTICAL_PRESSURE_NEAR_SEPARATION_MIN_SCALE = 0.45f;
    static constexpr float TACTICAL_PRESSURE_OVERLAP_RADIUS_MIN = 4.0f;
    static constexpr float TACTICAL_PRESSURE_OVERLAP_RADIUS_MULT = 1.4f;
    static constexpr float TACTICAL_PRESSURE_OVERLAP_DRIFT_MULT = 0.34f;
    static constexpr float TACTICAL_PRESSURE_FRONT_GAP_DEGREES = 35.0f;
    static constexpr float TACTICAL_PRESSURE_RADIUS_OFFSET_MIN = -1.0f;
    static constexpr float TACTICAL_PRESSURE_RADIUS_OFFSET_SPAN = 7.0f;
    static constexpr float TACTICAL_RECOVER_SEPARATION_DRIFT_MULT = 0.03f;
    static constexpr float CONFUSED_WANDER_RADIUS = 100.0f;
    static constexpr float CONFUSED_SEPARATION_RADIUS = 6.0f;
    static constexpr float CONFUSED_SEPARATION_WEIGHT = 0.55f;
    static constexpr float CONFUSED_SPEED_MULT = 1.f;
    static constexpr float CONFUSED_RETARGET_MIN = 0.35f;
    static constexpr float CONFUSED_RETARGET_SPAN = 0.75f;

    std::vector<Vec3> nearbyCache_;
};

} // namespace sim
