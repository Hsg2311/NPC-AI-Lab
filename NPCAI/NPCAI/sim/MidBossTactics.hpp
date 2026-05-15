#pragma once
#include "IMidBossTactic.hpp"
#include "Vec3.hpp"
#include <cstdint>
#include <vector>

namespace sim {

class TacticalSquad;
class Player;

class MidBossTacticBase : public IMidBossTactic {
public:
    struct PlayerCluster {
        Vec3                  centroid{};
        uint32_t              representativeId{ 0 };
        std::vector<uint32_t> playerIds{};
        float                 score{ 0.f };
    };

    void onLeaderDead(Room& room, PlatoonLeader& leader) override;

protected:
    std::vector<TacticalSquad*> collectLiveSquads(PlatoonLeader& leader) const;
    std::vector<PlayerCluster> buildPlayerClusters(const Room& room,
                                                   float clusterRadius) const;
    Vec3 calcPlayerCentroid(const Room& room, const Vec3& fallback) const;
    Vec3 calcAveragePlayerFacing(const Room& room, const Vec3& fallbackDir) const;
    Player* selectNearestPlayer(Room& room, const Vec3& from) const;
    uint32_t selectNearestPlayerId(Room& room, const Vec3& from) const;
    void assignSquadsToPlayers(const Room& room, const PlatoonLeader& leader,
                               const std::vector<TacticalSquad*>& liveSquads,
                               std::vector<uint32_t>& outTargetIds) const;
    void issueEngageAll(PlatoonLeader& leader, uint32_t targetId) const;
    void issueIdleAll(PlatoonLeader& leader) const;
};

class GoblinMidBossTactic : public MidBossTacticBase {
public:
    const char* name() const override { return "GoblinMidBossTactic"; }
    void update(float dt, Room& room, PlatoonLeader& leader) override;

private:
    enum class LeaderPhase {
        BoxAdvance,
        Engage,
        TacticalRetreat,
        Encircle,
        Vigilance,
        DivideAndConquer,
        Cooldown
    };

    enum class DivideTaskType {
        None,
        Charge,
        Screen
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

    void evaluateTactics(Room& room, PlatoonLeader& leader);
    void enterPhase(LeaderPhase next, const char* reason, PlatoonLeader& leader);
    void enterTacticFailCooldown(Room& room, PlatoonLeader& leader, const char* reason);
    Player* selectPrimaryTarget(Room& room, const PlatoonLeader& leader) const;
    float evaluatePlayerScore(const Player* p, const PlatoonLeader& leader) const;
    int clusterPlayers(const Room& room, const PlatoonLeader& leader) const;
    std::vector<PlayerCluster> buildPlayerClusters(const Room& room,
                                                   const PlatoonLeader& leader) const;
    void issueDivideAndConquer(Room& room, PlatoonLeader& leader,
                               const std::vector<TacticalSquad*>& liveSquads,
                               const std::vector<PlayerCluster>& clusters);
    void updateDivideAndConquer(float dt, Room& room, PlatoonLeader& leader);
    uint32_t selectReplacementTarget(Room& room, const PlatoonLeader& leader,
                                     const std::vector<uint32_t>& playerIds) const;
    bool allMembersArrived(const Room& room, const PlatoonLeader& leader) const;
    std::vector<Vec3> calcSquadBoxOffsets(int numSquads) const;
    bool checkTacticsConditions(const PlatoonLeader& leader) const;

    LeaderPhase leaderPhase_{ LeaderPhase::BoxAdvance };
    bool phaseOrderIssued_{ false };
    bool tacticsUnlocked_{ false };
    bool initialSizesSet_{ false };
    std::vector<int> initialSquadSizes_{};
    float tacticTimer_{ 0.f };
    float tacticCooldown_{ 0.f };
    Vec3 boxAdvanceTargetPos_{};
    Vec3 retreatTargetPos_{};
    uint32_t primaryTargetId_{ 0 };
    std::vector<DivideSquadTask> divideTasks_{};

    static constexpr float TACTIC_INTERVAL          = 1.f;
    static constexpr float CLUSTER_RADIUS           = 20.f;
    static constexpr float ENCIRCLE_RADIUS          = 50.0f;
    static constexpr float TACTIC_HP_THRESHOLD      = 0.70f;
    static constexpr float TACTIC_SQUAD_RATIO       = 0.80f;
    static constexpr float TACTIC_COOLDOWN_DURATION = 8.0f;
    static constexpr float TACTIC_FAIL_COOLDOWN_DURATION = 5.0f;
    static constexpr float DIVIDE_ENGAGE_PROTECT_DURATION = 3.0f;
    static constexpr float SCREEN_BLOCK_SPACING     = 8.0f;
    static constexpr float SCREEN_SLOT_SPACING_SCALE = 0.65f;
    static constexpr float SCREEN_SLOT_COLUMN_SCALE = 3.0f;
    static constexpr int   SCREEN_SLOT_COLUMN_COUNT = 7;
    static constexpr float SCREEN_BLOCK_CENTER_BIAS = 0.5f;
    static constexpr float BOX_FRONT_OFFSET         = 15.f;
    static constexpr float BOX_SQUAD_SPACING        = 35.f;
    static constexpr float BOX_ARC_DEPTH            = 10.f;
    static constexpr float BOSS_KEEP_DIST           = 18.f;
    static constexpr float BOSS_KEEP_TOL            = 2.f;
    static constexpr float REGROUP_DIST             = 70.f;
    static constexpr float VIGILANCE_GUARD_RADIUS   = 20.f;
    static constexpr float TACTICAL_SPEED_MULT      = 3.f;
};

class GrandBaumMidBossTactic : public MidBossTacticBase {
public:
    explicit GrandBaumMidBossTactic(float grandBaumA = 0.5f);

    const char* name() const override { return "GrandBaumMidBossTactic"; }
    void update(float dt, Room& room, PlatoonLeader& leader) override;

private:
    enum class Phase {
        Engage,
        ShieldWall,
        Cooldown
    };

    void enterPhase(Phase next, const char* reason, PlatoonLeader& leader);
    void issueEngage(Room& room, PlatoonLeader& leader);
    void issueShieldWall(Room& room, PlatoonLeader& leader);
    void updateAmbush(float dt, Room& room, PlatoonLeader& leader,
                      TacticalSquad* ambushSquad);
    bool areShieldWallSquadsReady(Room& room, const PlatoonLeader& leader) const;
    uint32_t selectAmbushTarget(Room& room, const PlatoonLeader& leader,
                                TacticalSquad* ambushSquad) const;

    float grandBaumA_;
    Phase phase_{ Phase::Engage };
    float engageRefreshTimer_{ 0.f };
    float orderRefreshTimer_{ 0.f };
    float ambushPrepTimer_{ 0.f };
    float shieldWallTimer_{ 0.f };
    float tacticCooldown_{ 0.f };
    bool engageOrderIssued_{ false };
    bool ambushEngageIssued_{ false };

    static constexpr float ENGAGE_REFRESH_INTERVAL = 1.0f;
    static constexpr float ORDER_REFRESH_INTERVAL = 0.5f;
    static constexpr float TACTIC_COOLDOWN_DURATION = 8.0f;
    static constexpr float SHIELDWALL_MAX_DURATION = 6.0f;
    static constexpr float SHIELD_FRONT_DIST      = 8.f;
    static constexpr float SHIELD_SIDE_OFFSET     = 10.f;
    static constexpr float AMBUSH_REAR_DIST       = 18.f;
    static constexpr float AMBUSH_MAX_PREP_TIME   = 4.f;
    static constexpr float AMBUSH_CLUSTER_RADIUS  = 20.f;
};

} // namespace sim
