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
    std::vector<Vec3> calcSquadBoxOffsets(int numSquads) const;

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
    bool             boxAdvanceActive_  { true  };  // 박스 대형 진격 중
    bool             boxAdvanceOrderIssued_{ false };
    Vec3             boxAdvanceTargetPos_{};
    uint32_t         primaryTargetId_   { 0     };  // Engage 전환용 타겟 캐시

    static constexpr float TACTIC_INTERVAL          = 1.f;
    static constexpr float VIGILANCE_DURATION       = 5.0f;   // 경계 → 각개격파 전환 시간(초)
    static constexpr float CLUSTER_RADIUS           = 10.0f;  // 플레이어 분산 판단 반경
    static constexpr float ENCIRCLE_RADIUS          = 50.0f;  // 포위 섹터 배치 반경
    static constexpr float TACTIC_HP_THRESHOLD      = 0.70f;  // 리더 HP 70% 이하 시 전술 발동
    static constexpr float TACTIC_SQUAD_RATIO       = 0.80f;  // 부대원 80% 미만 생존 시 전술 발동
    static constexpr float TACTIC_COOLDOWN_DURATION = 8.0f;   // 쿨타임 길이(초)
    static constexpr float BOX_APPROACH_DIST        = 20.f;   // 박스 대형 플레이어 전방 배치 거리
    static constexpr float BOX_SQUAD_SPACING         = 35.f;   // 부대 간 간격 (20명×sep=6 → 부대폭 24, 여유 6)
    static constexpr float BOX_ARC_DEPTH             = 10.f;   // 호형 대형 깊이: 측면 부대를 전방으로 당기는 거리
    static constexpr float BOSS_KEEP_DIST            = 18.f;   // 보스~플레이어 유지 거리
    static constexpr float BOSS_KEEP_TOL             =  2.f;   // 거리 유지 허용 오차
};

} // namespace sim
