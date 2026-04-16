#include "Squad.hpp"
#include "Room.hpp"
#include "Npc.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <limits>
#include <cstdio>
#include <string>

namespace sim {

// ─── Constructor ──────────────────────────────────────────────────────────────

Squad::Squad(int squadId) : squadId_(squadId) {}

// ─── addMember ────────────────────────────────────────────────────────────────

void Squad::addMember(uint32_t npcId, bool asLeader) {
    members_.push_back(npcId);
    if (asLeader) leaderNpcId_ = npcId;
}

// ─── setPlatoonOrder ──────────────────────────────────────────────────────────
// Called by Platoon before updateSquads() each tick.
// Broken squads silently reject tactical orders (Attack/Hold).

void Squad::setPlatoonOrder(SquadOrderType type,
                             uint32_t        targetOverride,
                             Vec3            destination) {
    // Broken squads only accept survival orders
    if (status_ == SquadStatus::Broken) {
        if (type != SquadOrderType::Retreat) return;
    }
    platoonOrderType_     = type;
    platoonTargetOverride_ = targetOverride;
    platoonDestination_   = destination;
    hasPlatoonOrder_      = true;
}

// ─── update ───────────────────────────────────────────────────────────────────
// Fixed call order: clean → check leader → dispatch by status

void Squad::update(float dt, Room& room) {
    removeDeadMembers(room);
    if (members_.empty()) { disband(room); return; }

    // Route by current status
    if (status_ == SquadStatus::Confused) {
        updateConfused(dt, room);
        return;
    }
    if (status_ == SquadStatus::Broken) {
        updateBroken(dt, room);
        return;
    }

    // Normal: check leader before doing anything else
    checkLeaderValidity(room);
    if (status_ == SquadStatus::Confused) return;   // just entered Confused

    // Apply pending Platoon order to internal order
    if (hasPlatoonOrder_) {
        order_ = platoonOrderType_;
        if (platoonTargetOverride_ != 0) {
            // Reset memory timer when Platoon assigns a different target so
            // selectTarget() will immediately validate range (chaseRange check),
            // instead of coasting on the previous target's timer.
            if (platoonTargetOverride_ != targetPlayerId_)
                targetMemoryTimer_ = 0.f;
            targetPlayerId_ = platoonTargetOverride_;
        }
        if (platoonOrderType_ == SquadOrderType::Retreat) {
            // Relay retreat destination to all members immediately
            for (uint32_t id : members_) {
                if (Npc* npc = room.findNpcById(id)) {
                    NpcCommand cmd;
                    cmd.type        = NpcCommandType::Retreat;
                    cmd.destination = platoonDestination_;
                    npc->applyCommand(cmd);
                }
            }
            hasPlatoonOrder_ = false;
            return;
        }
        hasPlatoonOrder_ = false;
    }

    selectTarget(dt, room);
    pushCommandsToMembers(room);
}

// ─── buildReport ──────────────────────────────────────────────────────────────

SquadReport Squad::buildReport(Room& room) const {
    SquadReport rep;
    rep.squadId    = squadId_;
    rep.totalCount = static_cast<int>(members_.size());
    rep.status     = status_;

    int   alive  = 0;
    float hpSum  = 0.f;
    Vec3  center{};
    uint32_t engaged = 0;

    for (uint32_t id : members_) {
        Npc* npc = room.findNpcById(id);
        if (!npc || !npc->isAlive()) continue;
        ++alive;
        hpSum += npc->getHp() / npc->getMaxHp();
        center = center + npc->getPosition();
        if (npc->getTargetId() != 0) engaged = npc->getTargetId();
    }

    rep.aliveCount   = alive;
    rep.avgHpRatio   = (alive > 0) ? (hpSum / static_cast<float>(alive)) : 0.f;
    rep.center       = (alive > 0)
        ? Vec3{ center.x / alive, 0.f, center.z / alive }
        : Vec3{};
    rep.engagedTarget = engaged;
    rep.inCombat     = (engaged != 0);
    rep.combatEfficiency = (rep.totalCount > 0)
        ? (static_cast<float>(alive) / rep.totalCount) * rep.avgHpRatio
        : 0.f;

    // Leader alive check
    Npc* leader = room.findNpcById(leaderNpcId_);
    rep.leaderAlive = (leader && leader->isAlive());

    return rep;
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

// ─── checkLeaderValidity ──────────────────────────────────────────────────────
// If leader is dead, enter Confused: issue NpcCommand::Confused to all members.

void Squad::checkLeaderValidity(Room& room) {
    Npc* leader = room.findNpcById(leaderNpcId_);
    if (leader && leader->isAlive()) return;

    // Leader is gone → enter Confused
    status_        = SquadStatus::Confused;
    confusionTimer_ = 0.f;
    leaderNpcId_   = 0;     // no re-election
    targetPlayerId_ = 0;

    for (uint32_t id : members_) {
        if (Npc* npc = room.findNpcById(id)) {
            NpcCommand cmd;
            cmd.type = NpcCommandType::Confused;
            npc->applyCommand(cmd);
        }
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "squad #%d leader dead → Confused", squadId_);
    Logger::get().log("Squad", buf);
}

// ─── updateConfused ───────────────────────────────────────────────────────────
// Tick confusion timer. All Platoon orders are ignored during this phase.
// Confusion is synchronized with NPC-side CONFUSION_DURATION.

void Squad::updateConfused(float dt, Room& room) {
    (void)room;
    confusionTimer_ += dt;

    if (static_cast<int>(members_.size()) < MIN_MEMBERS_TO_DISBAND) {
        disband(room);
        return;
    }
    if (confusionTimer_ >= CONFUSION_DURATION) {
        status_ = SquadStatus::Broken;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "squad #%d confusion ended → Broken", squadId_);
        Logger::get().log("Squad", buf);
    }
}

// ─── updateBroken ─────────────────────────────────────────────────────────────
// Broken squads:
//   • Accept Platoon Retreat order only (tactical orders ignored).
//   • Per-NPC: issue EngageTarget if an enemy is in detectionRange;
//     otherwise issue Retreat(spawnPos, 0.5x speed) when NPC is Idle at distance.

void Squad::updateBroken(float dt, Room& room) {
    (void)dt;

    if (static_cast<int>(members_.size()) < MIN_MEMBERS_TO_DISBAND) {
        disband(room);
        return;
    }

    // Platoon Retreat order: relay to all members at full speed
    if (hasPlatoonOrder_ && platoonOrderType_ == SquadOrderType::Retreat) {
        for (uint32_t id : members_) {
            if (Npc* npc = room.findNpcById(id)) {
                NpcCommand cmd;
                cmd.type        = NpcCommandType::Retreat;
                cmd.destination = platoonDestination_;
                npc->applyCommand(cmd);
            }
        }
        hasPlatoonOrder_ = false;
        return;
    }
    hasPlatoonOrder_ = false;

    auto players = room.getLivingPlayers();

    for (uint32_t id : members_) {
        Npc* npc = room.findNpcById(id);
        if (!npc || !npc->isAlive()) continue;

        NpcState state = npc->getState();

        // Already fighting? Don't interrupt active combat
        bool inCombat = (state == NpcState::Chase        ||
                         state == NpcState::AttackWindup  ||
                         state == NpcState::AttackRecover ||
                         state == NpcState::Reposition);
        if (inCombat && npc->getTargetId() != 0) continue;

        // Scan for nearest player within detectionRange
        Player* nearest     = nullptr;
        float   nearestDist = npc->getDetectionRange();
        for (Player* p : players) {
            float d = Vec3::distance(npc->getPosition(), p->getPosition());
            if (d < nearestDist) { nearestDist = d; nearest = p; }
        }

        if (nearest) {
            NpcCommand cmd;
            cmd.type     = NpcCommandType::EngageTarget;
            cmd.targetId = nearest->getId();
            npc->applyCommand(cmd);
        } else if (state == NpcState::Idle) {
            // Already at spawn? Don't issue redundant retreat
            float distToSpawn = Vec3::distance(npc->getPosition(), npc->getSpawnPos());
            if (distToSpawn > 1.0f) {
                NpcCommand cmd;
                cmd.type            = NpcCommandType::Retreat;
                cmd.destination     = npc->getSpawnPos();
                cmd.speedMultiplier = 0.5f;
                npc->applyCommand(cmd);
            }
        }
    }
}

// ─── disband ──────────────────────────────────────────────────────────────────

void Squad::disband(Room& room) {
    for (uint32_t id : members_) {
        if (Npc* npc = room.findNpcById(id)) {
            npc->setSquadId(-1);
            npc->setIsLeader(false);
            npc->setSquadTarget(0);
            // Send final retreat to spawn
            NpcCommand cmd;
            cmd.type        = NpcCommandType::Retreat;
            cmd.destination = npc->getSpawnPos();
            npc->applyCommand(cmd);
        }
    }
    members_.clear();
    leaderNpcId_    = 0;
    targetPlayerId_ = 0;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "squad #%d disbanded", squadId_);
    Logger::get().log("Squad", buf);
}

// ─── selectTarget ─────────────────────────────────────────────────────────────
// Normal state only. Uses leader position as squad center.
// Platoon-specified targetOverride takes precedence when set.

void Squad::selectTarget(float dt, Room& room) {
    if (order_ != SquadOrderType::Attack) {
        targetPlayerId_ = 0;
        return;
    }

    Npc* leader = room.findNpcById(leaderNpcId_);
    if (!leader) { targetPlayerId_ = 0; return; }

    Vec3  leaderPos   = leader->getPosition();
    float detectRange = leader->getDetectionRange();
    float chaseRange  = leader->getChaseRange();
    auto  players     = room.getLivingPlayers();

    // ── Hysteresis: keep existing target while still within chaseRange ────────
    if (targetPlayerId_ != 0) {
        bool stillReachable = false;
        for (Player* p : players) {
            if (p->getId() != targetPlayerId_) continue;
            if (Vec3::distance(leaderPos, p->getPosition()) <= chaseRange) {
                stillReachable     = true;
                targetMemoryTimer_ = TARGET_MEMORY_DURATION;
            }
            break;
        }
        if (stillReachable) return;

        targetMemoryTimer_ -= dt;
        if (targetMemoryTimer_ > 0.f) return;
        targetPlayerId_ = 0;
    }

    // ── Acquire new target (must be within detectionRange) ────────────────────
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

// ─── pushCommandsToMembers ────────────────────────────────────────────────────
// Translate current squad state to per-NPC commands.

void Squad::pushCommandsToMembers(Room& room) {
    // Pre-validate target range using leader as squad anchor.
    // Prevents issuing EngageTarget when Platoon override points to an
    // out-of-range player, which would cause NPC Idle→Chase→Idle flapping.
    bool canEngage = false;
    if (order_ == SquadOrderType::Attack && targetPlayerId_ != 0) {
        Npc*   leader = room.findNpcById(leaderNpcId_);
        Actor* tgt    = room.findActorById(targetPlayerId_);
        canEngage = leader && tgt && tgt->isAlive() &&
                    Vec3::distance(leader->getPosition(), tgt->getPosition())
                        <= leader->getChaseRange();
    }

    for (uint32_t id : members_) {
        Npc* npc = room.findNpcById(id);
        if (!npc || !npc->isAlive()) continue;

        NpcCommand cmd;
        if (canEngage) {
            cmd.type     = NpcCommandType::EngageTarget;
            cmd.targetId = targetPlayerId_;
        } 
        else {
            cmd.type = NpcCommandType::HoldSlot;
        }
        npc->applyCommand(cmd);

        // Keep legacy squadTargetId_ in sync for buildReport / Renderer
        npc->setSquadTarget(targetPlayerId_);
    }
}

} // namespace sim
