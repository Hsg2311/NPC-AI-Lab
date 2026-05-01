#pragma once
#include "TacticalNpc.hpp"
#include "TacticalSquad.hpp"
#include <vector>

namespace sim {

class Room;

// ─── PlatoonLeader ───────────────────────────────────────────────────────────
// TacticalNpc 상속: 자체 전투 FSM + evaluateTactics()로 Squad 지휘.
// 사망 시 소속 Squad 전체에 Confused 명령 발행.
// 플레이어를 항상 인식 (보스 맵 = 활동 구역 전체).
class PlatoonLeader : public TacticalNpc {
public:
    PlatoonLeader(const std::string& name, const Vec3& pos,
                  const TacticalNpcConfig& cfg = {});

    void update(float dt, Room& room) override;
    const char* typeName() const override { return "PlatoonLeader"; }

    // ── Squad 등록 ────────────────────────────────────────────────────────────
    void addSquad(TacticalSquad* squad);

    // ── 접근자 ────────────────────────────────────────────────────────────────
    const std::vector<TacticalSquad*>& getSquads() const { return squads_; }

private:
    void evaluateTactics(Room& room);
    // 점수 기반 primary target 선택 (거리 + HP 가중치)
    Player* selectPrimaryTarget(Room& room) const;
    float   evaluatePlayerScore(const Player* p) const;

    std::vector<TacticalSquad*> squads_;
    float                       tacticTimer_{ 0.f };
    bool                        deathReported_{ false };  // 사망 명령 1회만 발행

    static constexpr float TACTIC_INTERVAL   = 1.f;
    static constexpr float APPROACH_RADIUS   = 4.5f;  // 슬롯 배치 반경
};

} // namespace sim
