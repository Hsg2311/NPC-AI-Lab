#pragma once
#include "TacticalNpc.hpp"
#include "TacticalSquad.hpp"
#include <cstdint>
#include <vector>

namespace sim {

class Room;

// ─── PlatoonLeader ───────────────────────────────────────────────────────────
// TacticalNpc 상속: 자체 전투 FSM + evaluateTactics()로 Squad 지휘.
// 사망 시 소속 Squad 전체에 Confused 명령 발행.
// 플레이어를 항상 인식하며 (보스 맵 = 활동 구역 전체), 전술 조건 충족 후 부대 단위 대형 전환을 관리한다.
class PlatoonLeader : public TacticalNpc {
public:
    PlatoonLeader(const std::string& name, const Vec3& pos,
                  const TacticalNpcConfig& cfg = {});

    void update(float dt, Room& room) override;
    const char* typeName() const override { return "PlatoonLeader"; }

    // ── Squad 등록/조회 ──────────────────────────────────────────────────────
    void addSquad(TacticalSquad* squad);
    const std::vector<TacticalSquad*>& getSquads() const { return squads_; }

private:
    // 전술 사이클 단계:
    // BoxAdvance -> Engage -> TacticalRetreat -> BoxAdvance -> Encircle/Vigilance
    // 전술 선택은 TacticalRetreat 이후 BoxAdvance가 완성된 순간에만 수행한다.
    enum class LeaderPhase {
        BoxAdvance,      // 보스 중심 박스 대형 집결
        Engage,          // 일반 교전
        TacticalRetreat, // 전술 발동 직후, 플레이어 centroid 반대 방향으로 전체 후퇴
        Encircle,        // 플레이어 1군집 포위
        Vigilance,       // 플레이어 분산 시 보스 중심 경계 대형
        DivideAndConquer,
        Cooldown         // 포위 완료 후 재평가 대기
    };

    struct PlayerCluster {
        Vec3                  centroid{};
        uint32_t              representativeId{ 0 };
        std::vector<uint32_t> playerIds{};
        float                 score{ 0.f };
    };

    enum class DivideTaskType {
        None,
        Charge
    };

    struct DivideSquadTask {
        TacticalSquad*        squad{ nullptr };
        DivideTaskType        type{ DivideTaskType::None };
        uint32_t              targetId{ 0 };
        std::vector<uint32_t> clusterPlayerIds{};
        bool                  taskCompleted{ false };
        bool                  engageIssued{ false };
        float                 engageProtectTimer{ 0.f };
    };

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
    std::vector<PlayerCluster> buildPlayerClusters(const Room& room) const;
    void    issueDivideAndConquer(Room& room,
                const std::vector<TacticalSquad*>& liveSquads,
                const std::vector<PlayerCluster>& clusters);
    void    updateDivideAndConquer(float dt, Room& room);
    uint32_t selectReplacementTarget(Room& room, const std::vector<uint32_t>& playerIds) const;
    bool    allMembersArrived(const Room& room) const;
    std::vector<Vec3> calcSquadBoxOffsets(int numSquads) const;

    bool checkTacticsConditions() const;

    std::vector<TacticalSquad*> squads_;
    float                       tacticTimer_{ 0.f };
    bool                        deathReported_{ false };

    // ── 전술 단계 상태 ────────────────────────────────────────────────────────
    LeaderPhase      leaderPhase_{ LeaderPhase::BoxAdvance };
    bool             phaseOrderIssued_{ false }; // 현 단계 명령 발행 완료 여부
    bool             tacticsUnlocked_{ false };  // 전술 조건 충족 후 유지되는 단방향 래치
    bool             initialSizesSet_{ false };  // Squad 최초 규모 기록 여부
    std::vector<int> initialSquadSizes_{};        // 생존율 전술 조건 계산 기준
    float            tacticCooldown_{ 0.f };
    Vec3             boxAdvanceTargetPos_{};      // BoxAdvance 발행 시점의 플레이어 중심/방향 기준
    Vec3             retreatTargetPos_{};         // TacticalRetreat 중 보스 후퇴 목표
    uint32_t         primaryTargetId_{ 0 };       // 단계 전환 시 사용할 primary target 캐시
    std::vector<DivideSquadTask> divideTasks_{};

    static constexpr float TACTIC_INTERVAL          = 1.f;
    static constexpr float CLUSTER_RADIUS           = 20.f;  // 플레이어 군집 판단 거리
    static constexpr float ENCIRCLE_RADIUS          = 50.0f; // 포위 슬롯 반경
    static constexpr float TACTIC_HP_THRESHOLD      = 0.70f; // 리더 HP 70% 이하 시 전술 발동
    static constexpr float TACTIC_SQUAD_RATIO       = 0.80f; // 부대 생존율 80% 이하 시 전술 발동
    static constexpr float TACTIC_COOLDOWN_DURATION = 8.0f;  // 포위 완료 후 쿨타임
    static constexpr float TACTIC_FAIL_COOLDOWN_DURATION = 5.0f; // 실패/예외 쿨타임
    static constexpr float DIVIDE_ENGAGE_PROTECT_DURATION = 3.0f;
    static constexpr float SCREEN_BLOCK_SPACING     = 12.0f; // 각개격파 차단 경계 Squad 간격
    static constexpr float BOX_FRONT_OFFSET         = 15.f;  // 보스 앞쪽 박스 대형 중심 거리
    static constexpr float BOX_SQUAD_SPACING        = 35.f;  // 박스 대형 부대 간격
    static constexpr float BOX_ARC_DEPTH            = 10.f;  // 측면 부대를 당기는 호형 깊이
    static constexpr float BOSS_KEEP_DIST           = 18.f;  // 일반 교전 중 보스 유지 거리
    static constexpr float BOSS_KEEP_TOL            = 2.f;   // 거리 유지 허용 오차
    static constexpr float REGROUP_DIST             = 70.f;  // 공통 후퇴 거리
    static constexpr float VIGILANCE_GUARD_RADIUS   = 20.f;  // 경계 대형의 보스 주변 거리
};

} // namespace sim
