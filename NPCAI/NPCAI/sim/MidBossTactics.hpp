#pragma once
#include "IMidBossTactic.hpp"
#include "Vec3.hpp"
#include <cstdint>
#include <vector>

namespace sim {

class TacticalSquad;
class Player;
struct TacticalNpcConfig;

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
    void onLeaderDead(Room& room, PlatoonLeader& leader) override;

private:
    enum class Phase {
        Engage,
        ShieldWall,
        Cooldown
    };

    enum class SnakeAmbushStage {
        Evasion,
        RetreatingOriginal,
        WaveActive,
        ReturningOriginal
    };

    void enterPhase(Phase next, const char* reason, PlatoonLeader& leader);
    void issueEngage(Room& room, PlatoonLeader& leader);
    void issueShieldWall(Room& room, PlatoonLeader& leader);
    void updateSnakeAmbush(float dt, Room& room, PlatoonLeader& leader,
                           TacticalSquad* originalSnakeSquad);
    void updateSnakeEvasion(float dt, Room& room, PlatoonLeader& leader,
                            TacticalSquad* snakeSquad);
    void pickNewSnakeWanderTarget();
    void issueOriginalSnakeRetreat(Room& room, PlatoonLeader& leader,
                                   TacticalSquad* originalSnakeSquad);
    void spawnSnakeWave(Room& room, PlatoonLeader& leader,
                        TacticalSquad* originalSnakeSquad);
    void issueSnakeWaveEngage(Room& room, TacticalSquad* waveSquad);
    void finishShieldWall(Room& room, PlatoonLeader& leader, const char* reason);
    void cleanupSnakeWave(Room& room);
    int countLiveMembers(Room& room, TacticalSquad* squad) const;
    int calcSnakeWaveSpawnCount(int liveOriginalSnakeCount) const;
    bool isSnakeWaveAnnihilated(Room& room) const;
    TacticalNpcConfig findSnakeConfig(Room& room, TacticalSquad* originalSnakeSquad) const;
    void applyShieldWallProtection(Room& room, PlatoonLeader& leader, bool enabled);

    float grandBaumA_;
    Phase phase_{ Phase::Engage };
    float engageRefreshTimer_{ 0.f };
    float orderRefreshTimer_{ 0.f };
    float snakeRetreatTimer_{ 0.f };
    float tacticCooldown_{ 0.f };
    bool engageOrderIssued_{ false };
    bool snakeWaveSpawned_{ false };
    bool shieldWallRingIssued_{ false };
    Vec3 shieldWallRingCenter_{};
    float shieldWallRingStartAngle_{ 0.f };
    int originalSnakeCountAtShieldWall_{ 0 };
    int snakeWaveSquadId_{ -1 };
    bool  snakeWanderCenterSet_{ false };
    Vec3  snakeWanderCenter_{};
    Vec3  snakeWanderTarget_{};
    float snakeWanderTimer_{ 0.f };
    bool  snakeIsEvading_{ false };
    SnakeAmbushStage snakeAmbushStage_{ SnakeAmbushStage::Evasion };
    std::vector<uint32_t> snakeWaveNpcIds_{};

    static constexpr float ENGAGE_REFRESH_INTERVAL = 1.0f;
    static constexpr float ORDER_REFRESH_INTERVAL = 0.5f;
    static constexpr float TACTIC_COOLDOWN_DURATION = 8.0f;
    static constexpr float SHIELD_RING_RADIUS     = 12.f;
    static constexpr float SHIELDWALL_DAMAGE_MULT = 0.1f;
    static constexpr float SNAKE_OUTER_RADIUS = 64.f;
    static constexpr float SNAKE_EVASION_RADIUS = 44.f;
    static constexpr float SNAKE_EVASION_SPEED_MULT = 0.75f;
    static constexpr float SNAKE_RETREAT_SPEED_MULT = 1.0f;
    static constexpr float SNAKE_RETREAT_MAX_TIME = 1.5f;
    static constexpr float SNAKE_DETECT_RANGE      = 22.f;
    static constexpr float SNAKE_STOP_EVADE_RANGE  = 32.f;
    static constexpr float SNAKE_WANDER_RADIUS     = 10.f;
    static constexpr float SNAKE_WANDER_INTERVAL   = 3.5f;
    static constexpr float SNAKE_WANDER_SPEED_MULT = 0.15f;
    static constexpr float SNAKE_EVASION_REFRESH   = 0.5f;
    static constexpr int   SNAKE_WAVE_MAX_COUNT = 60;
    static constexpr int   SNAKE_WAVE_MULTIPLIER = 10;
    static constexpr int   SNAKE_WAVE_SQUAD_ID = 9003;
};

} // namespace sim
