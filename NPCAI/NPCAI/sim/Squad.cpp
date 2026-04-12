#include "Squad.hpp"
#include "Room.hpp"
#include "Npc.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <limits>
#include <string>

namespace sim {

// ─── Constructor ──────────────────────────────────────────────────────────────

Squad::Squad(int squadId) : squadId_(squadId) {}

// ─── addMember ────────────────────────────────────────────────────────────────

void Squad::addMember(uint32_t npcId, bool asLeader) {
    members_.push_back(npcId);
    if (asLeader) leaderNpcId_ = npcId;
}

// ─── update ───────────────────────────────────────────────────────────────────
// Fixed call order: remove dead → check leader → tick panic / select target

void Squad::update(float dt, Room& room) {
    removeDeadMembers(room);
    if (members_.empty()) return;   // Room will erase this squad (isEmpty == true)

    checkLeaderValidity(room);

    if (status_ == SquadStatus::Panic)
        tickPanic(dt, room);
    else
        selectTarget(dt, room);
}

// ─── removeDeadMembers ────────────────────────────────────────────────────────

void Squad::removeDeadMembers(Room& room) {
    auto it = members_.begin();
    while (it != members_.end()) {
        Npc* npc = room.findNpcById(*it);
        if (!npc || !npc->isAlive())
            it = members_.erase(it);
        else
            ++it;
    }
}

// ─── checkLeaderValidity / enterPanic ─────────────────────────────────────────

void Squad::checkLeaderValidity(Room& room) {
    if (status_ == SquadStatus::Panic) return;   // already in panic

    Npc* leader = room.findNpcById(leaderNpcId_);
    if (!leader || !leader->isAlive())
        enterPanic(room);
}

void Squad::enterPanic(Room& room) {
    status_         = SquadStatus::Panic;
    panicTimer_     = 0.f;
    targetPlayerId_ = 0;

    // Immediately clear squad target on all living members
    for (uint32_t id : members_) {
        if (Npc* npc = room.findNpcById(id))
            npc->setSquadTarget(0);
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "squad #%d leader dead - entering Panic", squadId_);
    Logger::get().log("Squad", buf);
}

// ─── tickPanic ────────────────────────────────────────────────────────────────

void Squad::tickPanic(float dt, Room& room) {
    panicTimer_ += dt;
    if (panicTimer_ >= PANIC_DURATION)
        attemptRecovery(room);
}

// ─── attemptRecovery ──────────────────────────────────────────────────────────
// removeDeadMembers already ran this tick, so all entries in members_ are alive.

void Squad::attemptRecovery(Room& room) {
    int aliveCount = static_cast<int>(members_.size());

    if (aliveCount < MIN_MEMBERS_TO_RECOVER) {
        char buf[64];
        std::snprintf(buf, sizeof(buf),
            "squad #%d too few survivors (%d) - disbanding", squadId_, aliveCount);
        Logger::get().log("Squad", buf);
        disband(room);
        return;
    }

    // Elect first living member as new leader
    leaderNpcId_ = members_.front();
    if (Npc* newLeader = room.findNpcById(leaderNpcId_))
        newLeader->setIsLeader(true);

    status_     = SquadStatus::Normal;
    panicTimer_ = 0.f;

    char buf[80];
    std::snprintf(buf, sizeof(buf),
        "squad #%d recovered - new leader NPC#%u", squadId_, leaderNpcId_);
    Logger::get().log("Squad", buf);
}

// ─── disband ──────────────────────────────────────────────────────────────────

void Squad::disband(Room& room) {
    for (uint32_t id : members_) {
        if (Npc* npc = room.findNpcById(id)) {
            npc->setSquadId(-1);
            npc->setIsLeader(false);
            npc->setSquadTarget(0);
        }
    }
    members_.clear();
    leaderNpcId_    = 0;
    targetPlayerId_ = 0;
    // status_ intentionally left as Panic so isEmpty() == true triggers removal
}

// ─── selectTarget ─────────────────────────────────────────────────────────────
// Uses leader position as squad center — no average calculation.

void Squad::selectTarget(float dt, Room& room) {
    if (order_ != SquadOrderType::Attack) {
        targetPlayerId_ = 0;
        return;
    }

    Npc* leader = room.findNpcById(leaderNpcId_);
    if (!leader) {
        targetPlayerId_ = 0;
        return;
    }

    Vec3  leaderPos   = leader->getPosition();
    float detectRange = leader->getDetectionRange();
    float chaseRange  = leader->getChaseRange();
    auto  players     = room.getLivingPlayers();

    // ── Hysteresis: keep existing target while it stays within chaseRange ─────
    if (targetPlayerId_ != 0) {
        bool stillReachable = false;
        for (Player* p : players) {
            if (p->getId() != targetPlayerId_) continue;
            if (Vec3::distance(leaderPos, p->getPosition()) <= chaseRange) {
                stillReachable     = true;
                targetMemoryTimer_ = TARGET_MEMORY_DURATION;  // refresh
            }
            break;
        }
        if (stillReachable) return;  // nothing to do

        // Target left chaseRange — count down memory before clearing
        targetMemoryTimer_ -= dt;
        if (targetMemoryTimer_ > 0.f) return;
        targetPlayerId_ = 0;  // memory expired, fall through to acquire new
    }

    // ── Acquire new target (must enter within detectionRange) ─────────────────
    Player* best     = nullptr;
    float   bestDist = std::numeric_limits<float>::max();

    for (Player* p : players) {
        float d = Vec3::distance(leaderPos, p->getPosition());
        if (d > detectRange) continue;
        if (d < bestDist) { bestDist = d; best = p; }
    }

    if (best) {
        targetPlayerId_    = best->getId();
        targetMemoryTimer_ = TARGET_MEMORY_DURATION;
        char buf[80];
        std::snprintf(buf, sizeof(buf),
            "squad #%d acquired target=%s dist=%.1f",
            squadId_, best->getName().c_str(), bestDist);
        Logger::get().log("Squad", buf);
    }
}

} // namespace sim
