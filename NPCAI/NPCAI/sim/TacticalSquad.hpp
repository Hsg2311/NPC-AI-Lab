#pragma once
#include "TacticalNpc.hpp"
#include <vector>
#include <cstdint>

namespace sim {

class Room;

// ─── SquadOrderType (PlatoonLeader → TacticalSquad) ─────────────────────────
enum class SquadOrderType {
    Idle,
    Engage,      // 정면 공격
    Encircle,    // 포위 (sectorAngle 각도 섹터 배정)
    DenseHold,   // 밀집 대형, 현재 위치 고정 (경계/각개격파 후방부대)
    WedgeCharge, // 쐐기 대형, 타겟 향해 돌진 (각개격파 공격대)
    BoxAdvance,  // 박스 대형 진격: sectorPos(상대 오프셋) 슬롯으로 이동, 전투 중 NPC 유지
};

struct SquadOrder {
    SquadOrderType type              = SquadOrderType::Idle;
    uint32_t       targetId          = 0;
    float          sectorAngle       = 0.f;   // Encircle: 이 Squad의 중심 각도 (라디안)
    float          sectorSpan        = 0.f;   // Encircle: 섹터 폭 (라디안)
    float          approachRadius    = 5.f;   // Encircle/BoxAdvance: 타겟 기준 접근 반경
    Vec3           leaderPos         = {};    // WedgeCharge/BoxAdvance: 방향 계산용
    Vec3           sectorPos         = {};    // BoxAdvance: 부대 상대 오프셋
    Vec3           formationTargetPos= {};    // BoxAdvance: 대형 시작 시점의 고정 타겟 위치
};

// ─── TacticalSquad ───────────────────────────────────────────────────────────
class TacticalSquad {
public:
    TacticalSquad(int squadId, float memberAttackRange, float memberSeparationRadius);

    // ── 멤버 관리 ─────────────────────────────────────────────────────────────
    void addMember   (uint32_t npcId);
    void removeMember(uint32_t npcId);

    // ── PlatoonLeader가 매 평가 주기마다 호출 ────────────────────────────────
    void receiveOrder(const SquadOrder& order);

    // ── Room::tick에서 PlatoonLeader 업데이트 후 호출 ─────────────────────────
    void update(float dt, Room& room);

    // ── 접근자 ────────────────────────────────────────────────────────────────
    int                         getSquadId()  const { return squadId_; }
    const std::vector<uint32_t>& getMembers() const { return memberIds_; }
    bool                        isEmpty()     const { return memberIds_.empty(); }

    // ── 소속 멤버 전체에 Confused 명령 발행 (PlatoonLeader 사망 시) ──────────
    void pushConfusedToMembers(Room& room);

    // PlatoonLeader::evaluateTactics()에서 isEmpty() 평가 전 호출
    void removeDeadMembers(Room& room);

    // BoxAdvance 중 leaderPos 갱신 (기존 호출 호환용)
    void updateBoxLeaderPos(const Vec3& pos);

private:
    void pushCommandsToMembers(Room& room);

    // ── 슬롯 계산 ─────────────────────────────────────────────────────────────
    std::vector<Vec3> calcEncircleSlots(const Vec3& targetPos, float sectorAngle,
                                        float sectorSpan, float radius, int count) const;
    std::vector<Vec3> calcDenseSlots (const Vec3& center, const Vec3& forward, int count) const;
    std::vector<Vec3> calcWedgeSlots (const Vec3& targetPos, const Vec3& fromPos, int count) const;

    int                  squadId_;
    float                memberAttackRange_;
    float                memberSeparationRadius_;
    std::vector<uint32_t> memberIds_;
    SquadOrder           currentOrder_{};
    bool                 orderDirty_{ false };  // 새 명령 수신 플래그
};

} // namespace sim
