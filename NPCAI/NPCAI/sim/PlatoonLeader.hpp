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
    Player* selectPrimaryTarget(Room& room) const;
    float   evaluatePlayerScore(const Player* p) const;
    int     clusterPlayers     (const Room& room) const;
    Vec3    calcPlayerCentroid (const Room& room) const;
    bool    allMembersArrived  (const Room& room) const;

    enum class TacticalPhase { Encircle, Vigilance, DivideAndConquer };

    bool checkTacticsConditions() const;

    std::vector<TacticalSquad*> squads_;
    float                       tacticTimer_     { 0.f };
    float                       vigilanceElapsed_{ 0.f };
    TacticalPhase               tacticalPhase_   { TacticalPhase::Encircle };
    bool                        deathReported_   { false };

    // 전술 발동 조건
    bool             tacticsUnlocked_    { false };
    bool             initialSizesSet_    { false };
    std::vector<int> initialSquadSizes_  {};
    Vec3             lastEncircleCentroid_{};
    bool             encircleSlotsAssigned_{ false };  // 현 사이클에서 슬롯 발행 완료
    float            tacticCooldown_    { 0.f };
    bool             tacticsOnCooldown_ { false };

    static constexpr float TACTIC_INTERVAL           = 1.f;
    static constexpr float APPROACH_RADIUS           = 4.5f;
    static constexpr float VIGILANCE_DURATION        = 5.0f;   // 경계 → 각개격파 전환 시간(초)
    static constexpr float CLUSTER_RADIUS            = 10.0f;  // 플레이어 분산 판단 반경
    static constexpr float ENCIRCLE_RADIUS           = 50.0f;  // 포위 섹터 배치 반경
    static constexpr float TACTIC_HP_THRESHOLD       = 0.70f;  // 리더 HP 70% 이하 시 전술 발동
    static constexpr float TACTIC_SQUAD_RATIO        = 0.80f;  // 부대원 80% 미만 생존 시 전술 발동
    static constexpr float ENCIRCLE_RECALC_THRESHOLD = 12.0f;  // 포위 재배치 거리 임계값
    static constexpr float TACTIC_COOLDOWN_DURATION =  8.0f;  // 쿨타임 길이(초)
};

} // namespace sim
