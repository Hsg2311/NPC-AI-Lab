#pragma once
#include "Vec3.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace sim {

class Room;   // forward-declare only — Squad.cpp includes Room.hpp

enum class SquadOrderType { Hold, Attack, Retreat };
enum class SquadStatus    { Normal, Panic };

// ─── Squad ───────────────────────────────────────────────────────────────────
// Upper-layer decision unit: owns target selection, leader tracking, and panic.
// Never controls NPC FSM internals directly.
// References NPCs by id only (no raw pointer stored).

class Squad {
public:
    explicit Squad(int squadId);

    // Called once per tick by Room::updateSquads(), BEFORE NPC updates.
    void update(float dt, Room& room);

    // ── Accessors ────────────────────────────────────────────────────────────
    int             getSquadId()        const { return squadId_; }
    uint32_t        getLeaderId()       const { return leaderNpcId_; }
    uint32_t        getTargetPlayerId() const { return targetPlayerId_; }
    SquadStatus     getStatus()         const { return status_; }
    SquadOrderType  getOrder()          const { return order_; }
    bool            isEmpty()           const { return members_.empty(); }

    const std::vector<uint32_t>& getMembers() const { return members_; }

    // ── Mutators (called by Room during squad setup) ──────────────────────────
    void addMember(uint32_t npcId, bool asLeader = false);
    void setOrder (SquadOrderType order) { order_ = order; }

private:
    // ── Sub-steps of update() — called in fixed order ────────────────────────
    void removeDeadMembers  (Room& room);
    void checkLeaderValidity(Room& room);
    void enterPanic         (Room& room);
    void tickPanic          (float dt, Room& room);
    void attemptRecovery    (Room& room);
    void disband            (Room& room);
    void selectTarget       (float dt, Room& room);

    // ── Data ─────────────────────────────────────────────────────────────────
    int                  squadId_;
    uint32_t             leaderNpcId_{ 0 };      // 0 = invalid / not set
    uint32_t             targetPlayerId_{ 0 };   // 0 = no target
    std::vector<uint32_t> members_;
    SquadOrderType       order_{ SquadOrderType::Attack };
    SquadStatus          status_{ SquadStatus::Normal };
    float                panicTimer_{ 0.f };
    float                targetMemoryTimer_{ 0.f };  // countdown after target leaves chaseRange

    static constexpr float PANIC_DURATION         = 8.0f;
    static constexpr int   MIN_MEMBERS_TO_RECOVER = 2;
    static constexpr float TARGET_MEMORY_DURATION = 4.0f;  // secs to hold target after leaving chaseRange
};

} // namespace sim
