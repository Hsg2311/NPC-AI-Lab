#pragma once
#include "Squad.hpp"   // SquadReport, SquadOrderType
#include "Vec3.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace sim {

class Room;

// ─── PlatoonBlackboard ───────────────────────────────────────────────────────
// Shared perception data owned by the Platoon.

struct PlatoonBlackboard {
    uint32_t primaryTargetId{ 0 };   // currently assigned focus target
    float    threatLevel    { 0.f }; // 0.0 – 1.0 aggregate threat
};

// ─── Platoon ─────────────────────────────────────────────────────────────────
// Conceptual top-tier commander: no physical NPC, no "death" condition.
// Owns multiple Squads and issues SquadOrderType directives each tick.
// Removed when all its Squads have disbanded.

class Platoon {
public:
    explicit Platoon(int platoonId);

    void addSquad   (int squadId);
    void removeSquad(int squadId);

    // Called by Room::updatePlatoons(), BEFORE Room::updateSquads().
    void update(float dt, Room& room);

    // Report intake: Room delivers SquadReports here after each Squad::update().
    void receiveReport(const SquadReport& report);

    int  platoonId()            const { return platoonId_; }
    bool isEmpty()              const { return squadIds_.empty(); }
    bool ownsSquad(int squadId) const;  // true if squadId is in squadIds_

private:
    // ── Decision pipeline ────────────────────────────────────────────────────
    void     evaluateTactics(Room& room);
    uint32_t selectPrimaryTarget(Room& room) const;
    void     issueOrderToAll(SquadOrderType type, uint32_t targetId,
                              const Vec3& destination, Room& room);

    // ── Data ─────────────────────────────────────────────────────────────────
    int                                    platoonId_;
    std::vector<int>                       squadIds_;
    std::unordered_map<int, SquadReport>   squadReports_;   // latest per squad
    PlatoonBlackboard                      blackboard_;
    SquadOrderType                         currentOrder_{ SquadOrderType::Attack };
    Vec3                                   retreatPoint_{};
    float                                  targetChangeTimer_{ 0.f };

    static constexpr float TARGET_CHANGE_COOLDOWN  = 1.0f;
    static constexpr float RETREAT_EFFICIENCY_THRESHOLD = 0.25f; // trigger retreat
};

} // namespace sim
