#pragma once
#include "IMidBossTactic.hpp"
#include "Vec3.hpp"
#include <cstdint>
#include <random>
#include <unordered_map>
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

class IsisMidBossTactic : public MidBossTacticBase {
public:
    IsisMidBossTactic();

    const char* name() const override { return "IsisMidBossTactic"; }
    void update(float dt, Room& room, PlatoonLeader& leader) override;

private:
    enum class Phase {
        Engage,
        RetreatForPincer,
        RegroupForPincer,
        PincerStrike,
        Cooldown
    };

    struct StrikeCluster {
        PlayerCluster cluster{};
        float score{ 0.f };
    };

    void captureInitialSquadSizes(const PlatoonLeader& leader);
    bool checkUnlockCondition(const PlatoonLeader& leader) const;
    void enterPhase(Phase next, const char* reason, PlatoonLeader& leader);
    void enterCooldown(PlatoonLeader& leader, const char* reason);
    void issueEngage(Room& room, PlatoonLeader& leader);
    void issueRetreatForPincer(Room& room, PlatoonLeader& leader);
    void issueRegroupForPincer(Room& room, PlatoonLeader& leader);
    void issuePincerStrike(Room& room, PlatoonLeader& leader, bool issueBombers);
    void issueBomberRegroup(Room& room, TacticalSquad* squad,
                            const StrikeCluster& strikeCluster, float sideSign);
    void issueBuddyColumn(Room& room, TacticalSquad* squad,
                          const StrikeCluster& strikeCluster, float sideSign);
    std::vector<StrikeCluster> selectStrikeClusters(const Room& room,
                                                    const PlatoonLeader& leader) const;
    Player* selectPrimaryTarget(Room& room, const PlatoonLeader& leader) const;
    float rollCooldown();
    bool hasLiveBomberSquad(const PlatoonLeader& leader) const;
    bool allLiveSquadsAtSlots(Room& room, const PlatoonLeader& leader) const;
    bool activeBombersAtSlots(Room& room) const;
    bool activeBombersComplete(Room& room) const;

    Phase phase_{ Phase::Engage };
    bool initialSizesSet_{ false };
    bool tacticsUnlocked_{ false };
    bool engageIssued_{ false };
    bool pincerIssued_{ false };
    float phaseTimer_{ 0.f };
    float cooldownTimer_{ 0.f };
    float buddyRefreshTimer_{ 0.f };
    Vec3 retreatTargetPos_{};
    std::vector<int> initialSquadSizes_{};
    std::vector<TacticalSquad*> activeBomberSquads_{};
    std::mt19937 rng_;

    static constexpr float CLUSTER_RADIUS = 20.f;
    static constexpr float UNLOCK_SQUAD_RATIO = 0.80f;
    static constexpr float MIN_COOLDOWN = 7.0f;
    static constexpr float MAX_COOLDOWN = 13.0f;
    static constexpr float ISIS_RETREAT_DIST = 70.0f;
    static constexpr float ISIS_RETREAT_EXTRA_DIST = 35.0f;
    static constexpr float ISIS_RETREAT_MIN_DIST = 90.0f;
    static constexpr float RETREAT_TIMEOUT = 5.0f;
    static constexpr float RETREAT_SPEED_MULT = 0.75f;
    static constexpr float RETREAT_LEADER_SPEED_MULT = 3.0f;
    static constexpr float RETREAT_BOMBER_FRONT_OFFSET = 18.0f;
    static constexpr float RETREAT_BOMBER_SIDE_OFFSET = 20.0f;
    static constexpr float RETREAT_BUDDY_BACK_OFFSET = 12.0f;
    static constexpr float RETREAT_BUDDY_SIDE_OFFSET = 28.0f;
    static constexpr float REGROUP_TIMEOUT = 3.5f;
    static constexpr float PINCER_TIMEOUT = 7.0f;
    static constexpr float BUDDY_REFRESH_DURATION = 2.0f;
    static constexpr float BUDDY_REFRESH_INTERVAL = 0.5f;
    static constexpr float BUDDY_BACK_OFFSET = 18.0f;
    static constexpr float BUDDY_SIDE_OFFSET = 18.0f;
    static constexpr float BUDDY_COLUMN_SPACING_SCALE = 0.85f;
    static constexpr float BUDDY_COLUMN_SCALE = 1.0f;
    static constexpr int   BUDDY_COLUMN_COUNT = 2;
    static constexpr float BUDDY_SPEED_MULT = 0.75f;
    static constexpr float ISIS_WEDGE_SPEED_MULT = 1.50f;
    static constexpr float BOMBER_REGROUP_SPEED_MULT = 0.75f;
    static constexpr float BOMBER_REGROUP_BACK_OFFSET = 28.0f;
    static constexpr float BOMBER_REGROUP_SIDE_OFFSET = 22.0f;
    static constexpr float BOMBER_REGROUP_SPACING_SCALE = 0.9f;
    static constexpr float BOMBER_REGROUP_COLUMN_SCALE = 1.8f;
    static constexpr int   BOMBER_REGROUP_COLUMN_COUNT = 0;
    static constexpr float LEADER_KEEP_DIST = 20.0f;
    static constexpr float LEADER_KEEP_TOL = 3.0f;
};

class GrandBaumMidBossTactic : public MidBossTacticBase {
public:
    GrandBaumMidBossTactic();

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

    enum class BossMeleeState {
        AcquireTarget,
        ChaseTarget,
        AttackWindup,
        AttackRecover
    };

    enum class BossTargetPriority {
        None = 0,
        Nearest = 1,
        SlimeThreat = 2,
        SnakeThreat = 3
    };

    struct BossTargetChoice {
        uint32_t targetId{ 0 };
        BossTargetPriority priority{ BossTargetPriority::None };
    };

    void enterPhase(Phase next, const char* reason, PlatoonLeader& leader);
    void updateBossMelee(float dt, Room& room, PlatoonLeader& leader);
    void resetBossMelee(PlatoonLeader& leader);
    BossTargetChoice selectBossMeleeTarget(Room& room, const PlatoonLeader& leader) const;
    BossTargetChoice selectOriginalSnakeThreatTarget(Room& room, const PlatoonLeader& leader) const;
    BossTargetChoice selectSlimeThreatTarget(Room& room, const PlatoonLeader& leader) const;
    std::vector<uint32_t> getOriginalSnakeCandidateIds(const PlatoonLeader& leader) const;
    BossTargetChoice selectNearestPlayerTarget(const Room& room, const Vec3& center) const;
    BossTargetChoice selectNearestPlayerNear(const Room& room, const Vec3& center,
                                             float radius, BossTargetPriority priority) const;
    bool isCurrentBossMeleeTargetValid(Room& room, const PlatoonLeader& leader) const;
    bool isResourceThreatPriority(BossTargetPriority priority) const;
    Vec3 calcLiveOriginalSnakeCentroid(Room& room, const PlatoonLeader& leader,
                                       int& outLiveCount) const;
    Vec3 calcLiveSlimeCentroid(Room& room, const PlatoonLeader& leader,
                               int& outLiveCount) const;
    void moveBossToward(PlatoonLeader& leader, const Vec3& targetPos,
                        float speedMult, float dt) const;
    void issueEngage(Room& room, PlatoonLeader& leader);
    void issueShieldWall(Room& room, PlatoonLeader& leader);
    void updateSnakeAmbush(float dt, Room& room, PlatoonLeader& leader,
                           TacticalSquad* originalSnakeSquad);
    void updateSnakeEvasion(float dt, Room& room, PlatoonLeader& leader,
                            TacticalSquad* snakeSquad);
    Vec3 pickSnakePersonalWanderTarget(const Vec3& center) const;
    void issueOriginalSnakeRetreat(Room& room, PlatoonLeader& leader,
                                   TacticalSquad* originalSnakeSquad);
    void spawnSnakeWave(Room& room, PlatoonLeader& leader,
                        TacticalSquad* originalSnakeSquad);
    void issueSnakeWaveEngage(Room& room, TacticalSquad* waveSquad);
    void finishShieldWall(Room& room, PlatoonLeader& leader, const char* reason);
    void cleanupSnakeWave(Room& room);
    void captureOriginalSnakeRoster(Room& room, TacticalSquad* originalSnakeSquad);
    void reviveOriginalSnakeSquad(Room& room, PlatoonLeader& leader);
    bool shouldPreserveOriginalSnakes() const;
    int countLiveMembers(Room& room, TacticalSquad* squad) const;
    int countLiveSlimeMembers(Room& room, const PlatoonLeader& leader) const;
    bool canFormShieldWall(int liveSlimeCount) const;
    float calcShieldWallRadius(int liveSlimeCount) const;
    int calcSnakeWaveSpawnCount(int liveOriginalSnakeCount) const;
    bool isSnakeWaveAnnihilated(Room& room) const;
    TacticalNpcConfig findSnakeConfig(Room& room, TacticalSquad* originalSnakeSquad) const;
    void applyShieldWallProtection(Room& room, PlatoonLeader& leader, bool enabled);

    Phase phase_{ Phase::Engage };
    float engageRefreshTimer_{ 0.f };
    float orderRefreshTimer_{ 0.f };
    float snakeRetreatTimer_{ 0.f };
    float tacticCooldown_{ 0.f };
    float previousHpRatio_{ 1.f };
    int shieldWallTriggerStage_{ 0 };
    bool engageOrderIssued_{ false };
    bool snakeWaveSpawned_{ false };
    bool pendingShieldWallTrigger_{ false };
    bool shieldWallRingIssued_{ false };
    Vec3 shieldWallRingCenter_{};
    float shieldWallRingRadius_{ 12.f };
    float shieldWallRingStartAngle_{ 0.f };
    int originalSnakeCountAtShieldWall_{ 0 };
    int snakeWaveSquadId_{ -1 };
    BossMeleeState bossMeleeState_{ BossMeleeState::AcquireTarget };
    float bossMeleeTimer_{ 0.f };
    float bossMeleeTargetLockTimer_{ 0.f };
    float bossMeleeSamePriorityRetargetTimer_{ 0.f };
    uint32_t bossMeleeTargetId_{ 0 };
    BossTargetPriority bossMeleeTargetPriority_{ BossTargetPriority::None };
    std::vector<uint32_t> originalSnakeRoster_{};
    std::unordered_map<uint32_t, Vec3> originalSnakeSpawnPositions_{};
    bool  snakeWanderCenterSet_{ false };
    Vec3  snakeWanderCenter_{};
    std::unordered_map<uint32_t, Vec3> snakePersonalTargets_{};
    std::unordered_map<uint32_t, float> snakePersonalTimers_{};
    std::unordered_map<uint32_t, bool> snakePersonalEvading_{};
    SnakeAmbushStage snakeAmbushStage_{ SnakeAmbushStage::Evasion };
    std::vector<uint32_t> snakeWaveNpcIds_{};

    static constexpr float ENGAGE_REFRESH_INTERVAL = 1.0f;
    static constexpr float ORDER_REFRESH_INTERVAL = 0.5f;
    static constexpr float TACTIC_COOLDOWN_DURATION = 8.0f;
    static constexpr float FIRST_SHIELD_WALL_HP_RATIO = 0.66f;
    static constexpr float SECOND_SHIELD_WALL_HP_RATIO = 0.33f;
    static constexpr float MIN_SHIELD_RING_RADIUS = 7.f;
    static constexpr float MAX_SHIELD_RING_RADIUS = 12.f;
    static constexpr float SLIME_RING_SLOT_SPACING = 4.5f;
    static constexpr float SHIELD_RING_MIN_ARC_SPACING = 3.0f;
    static constexpr float SHIELD_RING_LANE_SPACING = 2.2f;
    static constexpr int   MIN_SHIELD_WALL_SLIME_COUNT = 10;
    static constexpr float SHIELDWALL_DAMAGE_MULT = 0.1f;
    static constexpr float BOSS_CHASE_SPEED_MULT = 8.0f;
    static constexpr float BOSS_TARGET_LOCK_DURATION = 1.4f;
    static constexpr float BOSS_SAME_PRIORITY_RETARGET_INTERVAL = 2.5f;
    static constexpr float BOSS_SLIME_THREAT_RANGE = 12.f;
    static constexpr float SNAKE_OUTER_RADIUS = 64.f;
    static constexpr float SNAKE_EVASION_RADIUS = 24.f;
    static constexpr float SNAKE_EVASION_SPEED_MULT = 0.45f;
    static constexpr float SNAKE_RETREAT_SPEED_MULT = 1.0f;
    static constexpr float SNAKE_RETREAT_MAX_TIME = 1.5f;
    static constexpr float SNAKE_DETECT_RANGE      = 18.f;
    static constexpr float SNAKE_STOP_EVADE_RANGE  = 24.f;
    static constexpr float SNAKE_DISPERSE_WANDER_RADIUS = 24.f;
    static constexpr float SNAKE_PERSONAL_SCATTER_RADIUS = 5.f;
    static constexpr float SNAKE_PERSONAL_MAX_LEASH_RADIUS = 42.f;
    static constexpr float SNAKE_THREAT_WEIGHT_RANGE = SNAKE_STOP_EVADE_RANGE;
    static constexpr float SNAKE_WANDER_INTERVAL   = 3.5f;
    static constexpr float SNAKE_WANDER_SPEED_MULT = 0.2f;
    static constexpr float SNAKE_EVASION_REFRESH   = 0.5f;
    static constexpr int   SNAKE_WAVE_MAX_COUNT = 60;
    static constexpr int   SNAKE_WAVE_MULTIPLIER = 10;
    static constexpr int   SNAKE_WAVE_SQUAD_ID = 9003;
};

} // namespace sim
