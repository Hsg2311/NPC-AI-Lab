#pragma once
#include "TacticalNpc.hpp"
#include <vector>
#include <cstdint>

namespace sim {

class Room;

// ─── SquadOrderType (PlatoonLeader → TacticalSquad) ─────────────────────────
enum class SquadOrderType {
    Idle,
    Engage,        // 정면 공격
    Encircle,      // 포위: sectorAngle/sectorSpan으로 섹터 배정
    DenseHold,     // 밀집 대형, 현재 위치 고정
    BoxAdvance,    // 보스 중심 박스 대형: sectorPos 상대 오프셋으로 이동
    GuardBoss,     // 보스 중심 3방향 경계 대형
    RetreatFormUp, // 전술 발동 직후 대형을 만들지 않고 공통 방향으로 후퇴
    WedgeCharge,   // 쐐기 대형으로 플레이어 군집을 관통 돌진
    FormationHold,
    FormationGuard,
    RingGuard,
    DistributedEngage,
};

struct SquadOrder {
    SquadOrderType type               = SquadOrderType::Idle;
    uint32_t       targetId           = 0;
    float          slotSpacingScale   = 1.f; // GuardBoss/Dense slots spacing multiplier
    float          slotColumnScale    = 1.f; // Dense slot column count multiplier
    int            slotColumnCount    = 0;   // Fixed dense slot columns, 0 = auto
    float          sectorAngle        = 0.f; // Encircle/GuardBoss 중심 각도
    float          sectorSpan         = 0.f; // Encircle 섹터 폭
    float          approachRadius     = 5.f; // Encircle/GuardBoss 반경
    Vec3           leaderPos          = {};  // BoxAdvance 방향 계산용 / RetreatFormUp 시작 리더 위치
    Vec3           sectorPos          = {};  // BoxAdvance 부대 상대 오프셋
    Vec3           formationTargetPos = {};  // 대형이 바라볼 타겟/플레이어 centroid
    Vec3           tacticCenter       = {};  // 포위/경계 중심 또는 후퇴 목표 위치
    float          speedMult          = 1.f;
    float          chargeSpeedMult    = 0.f; // WedgeCharge only: 0 = shared default
    std::vector<uint32_t> targetIds    = {};  // WedgeCharge 대상 군집 플레이어들
};

// ─── TacticalSquad ───────────────────────────────────────────────────────────
class TacticalSquad {
public:
    TacticalSquad(int squadId, float memberAttackRange, float memberSeparationRadius);

    // ── 멤버 관리 ─────────────────────────────────────────────────────────────
    void addMember(uint32_t npcId);
    void removeMember(uint32_t npcId);

    // ── PlatoonLeader 명령 수신 및 Room tick 갱신 ────────────────────────────
    // 매 평가 주기마다 호출
    void receiveOrder(const SquadOrder& order);
    void update(float dt, Room& room);

    // ── 접근자 ────────────────────────────────────────────────────────────────
    int getSquadId() const { return squadId_; }
    const std::vector<uint32_t>& getMembers() const { return memberIds_; }
    bool isEmpty() const { return memberIds_.empty(); }

    // 소속 멤버 전체에 Confused 명령 발행 (PlatoonLeader 사망 시)
    void pushConfusedToMembers(Room& room);
    // PlatoonLeader::evaluateTactics()에서 isEmpty() 평가 전 호출
    void removeDeadMembers(Room& room);
    // BoxAdvance 중 leaderPos 갱신 (기존 호출 호환용)
    void updateBoxLeaderPos(const Vec3& pos);
    Vec3 calcCentroid(Room& room) const;
    bool areMembersAtSlots(Room& room) const;
    bool areChargeMembersComplete(Room& room) const;

private:
    void pushCommandsToMembers(Room& room);

    // ── 슬롯 계산 ─────────────────────────────────────────────────────────────
    std::vector<Vec3> calcEncircleSlots(const Vec3& targetPos, float sectorAngle,
                                        float sectorSpan, float radius, int count) const;
    std::vector<Vec3> calcDenseSlots(const Vec3& center, const Vec3& forward,
                                     int count, float spacingScale = 1.f,
                                     float columnScale = 1.f,
                                     int fixedColumnCount = 0) const;
    std::vector<Vec3> calcWedgeSlots(const Vec3& apex, const Vec3& forward, int count) const;

    int                   squadId_;
    float                 memberAttackRange_;
    float                 memberSeparationRadius_;
    std::vector<uint32_t> memberIds_;
    SquadOrder            currentOrder_{};
    bool                  orderDirty_{ false }; // 새 명령 수신 후 1회 슬롯 재계산
    bool                  wedgePrepared_{ false };
    std::vector<uint32_t> wedgeMemberIds_{};
    std::vector<Vec3>     wedgePrepareSlots_{};
    std::vector<Vec3>     wedgeExitSlots_{};
    uint32_t              activeWedgeChargeId_{ 0 };
};

} // namespace sim
