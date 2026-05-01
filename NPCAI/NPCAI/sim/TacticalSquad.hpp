#pragma once
#include "TacticalNpc.hpp"
#include <vector>
#include <cstdint>

namespace sim {

class Room;

// ─── SquadOrderType (PlatoonLeader → TacticalSquad) ─────────────────────────
enum class SquadOrderType {
    Idle,
    Engage,          // 정면 공격
    FlankLeft,       // 좌측 측면 기동
    FlankRight,      // 우측 측면 기동
    Encircle,        // 포위 (sectorAngle 각도 섹터 배정)
    AlternateAttack, // 교대 공격 (attackTurn 인덱스)
    Retreat,
};

struct SquadOrder {
    SquadOrderType type        = SquadOrderType::Idle;
    uint32_t       targetId    = 0;
    float          sectorAngle = 0.f;  // Encircle: 이 Squad의 중심 각도 (라디안)
    float          sectorSpan  = 0.f;  // Encircle: 섹터 폭 (라디안)
    int            attackTurn  = 0;    // AlternateAttack: 공격 순번 (0부터)
    int            totalTurns  = 1;    // AlternateAttack: 전체 순번 수
    float          approachRadius = 5.f; // Flank/Encircle: 타겟 기준 접근 반경
    Vec3           leaderPos   = {};   // Flank 방향 계산용 리더 위치
};

// ─── TacticalSquad ───────────────────────────────────────────────────────────
class TacticalSquad {
public:
    TacticalSquad(int squadId, float memberAttackRange);

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

private:
    void removeDeadMembers(Room& room);
    void pushCommandsToMembers(Room& room);

    // ── 슬롯 계산 ─────────────────────────────────────────────────────────────
    // 월드 좌표 슬롯 목록 반환 (멤버 수만큼)
    std::vector<Vec3> calcFlankSlots (const Vec3& targetPos, const Vec3& leaderPos,
                                      bool leftSide, float radius, int count) const;
    std::vector<Vec3> calcEncircleSlots(const Vec3& targetPos, float sectorAngle,
                                        float sectorSpan, float radius, int count) const;

    int                  squadId_;
    float                memberAttackRange_;
    std::vector<uint32_t> memberIds_;
    SquadOrder           currentOrder_{};
    bool                 orderDirty_{ false };  // 새 명령 수신 플래그
};

} // namespace sim
