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
    // 전술 사이클 단계 (마스터 단계)
    // BoxAdvance → Engage → TacticRegroup → Encircle → Cooldown → BoxAdvance → ...
    enum class LeaderPhase { BoxAdvance, Engage, TacticRegroup, Encircle, Cooldown };

    void evaluateTactics(Room& room);
    void enterPhase(LeaderPhase next, const char* reason);
    void enterTacticFailCooldown(Room& room, const char* reason);
    void removeDeadMembersFromSquads(Room& room);
    Player* selectPrimaryTarget(Room& room) const;
    float   evaluatePlayerScore(const Player* p) const;
    void    assignSquadsToPlayers(const Room& room,
                const std::vector<TacticalSquad*>& liveSquads,
                std::vector<uint32_t>& outTargetIds) const;
    Vec3    calcPlayerCentroid(const Room& room) const;
    int     clusterPlayers(const Room& room) const;
    bool    allMembersArrived (const Room& room) const;
    std::vector<Vec3> calcSquadBoxOffsets(int numSquads) const;

    bool checkTacticsConditions() const;

    std::vector<TacticalSquad*> squads_;
    float                       tacticTimer_  { 0.f };
    bool                        deathReported_{ false };

    // ── 전술 단계 상태 ────────────────────────────────────────────────────────
    LeaderPhase      leaderPhase_      { LeaderPhase::BoxAdvance };
    bool             phaseOrderIssued_ { false };  // 현 단계 명령 발행 완료 여부
    bool             tacticsUnlocked_  { false };  // 단방향 래치: 고급 전술 허가
    bool             initialSizesSet_  { false };
    std::vector<int> initialSquadSizes_{};
    float            tacticCooldown_   { 0.f };
    Vec3             boxAdvanceTargetPos_{};
    Vec3             regroupTargetPos_  {};    // TacticRegroup 집결 목표 위치
    uint32_t         primaryTargetId_   { 0 };  // Engage 전환용 타겟 캐시

    static constexpr float TACTIC_INTERVAL         = 1.f;
    static constexpr float CLUSTER_RADIUS          = 20.f;   // 플레이어 군집 판단 거리
    static constexpr float ENCIRCLE_RADIUS         = 50.0f;  // 포위 섹터 배치 반경
    static constexpr float TACTIC_HP_THRESHOLD     = 0.70f;  // 리더 HP 70% 이하 시 전술 발동
    static constexpr float TACTIC_SQUAD_RATIO      = 0.80f;  // 부대원 80% 미만 생존 시 전술 발동
    static constexpr float TACTIC_COOLDOWN_DURATION= 8.0f;   // 쿨타임 길이(초)
    static constexpr float TACTIC_FAIL_COOLDOWN_DURATION= 5.0f; // 실패 쿨타임 길이(초)
    static constexpr float BOX_APPROACH_DIST       = 20.f;   // 박스 대형 플레이어 전방 배치 거리
    static constexpr float BOX_SQUAD_SPACING       = 35.f;   // 부대 간 간격
    static constexpr float BOX_ARC_DEPTH           = 10.f;   // 호형 대형 깊이
    static constexpr float BOSS_KEEP_DIST          = 18.f;   // 보스~플레이어 유지 거리
    static constexpr float BOSS_KEEP_TOL           =  2.f;   // 거리 유지 허용 오차
    static constexpr float REGROUP_DIST            = 55.f;   // 집결: 보스에서 플레이어 반대 방향 거리
};

} // namespace sim
