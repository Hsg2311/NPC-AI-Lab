#include "Room.hpp"
#include "Actor.hpp"
#include "Player.hpp"
#include "Npc.hpp"
#include "Squad.hpp"
#include "Platoon.hpp"
#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

namespace sim {

Room::Room(uint32_t roomId, uint32_t dumpInterval)
    : roomId_(roomId), dumpInterval_(dumpInterval)
{}

void Room::addActor(std::shared_ptr<Actor> actor) {
    actors_[actor->getId()] = std::move(actor);
}

// ─── tick ─────────────────────────────────────────────────────────────────────
// Update order:
//   1. Sync logger tick counter
//   2. DummyPlayerController  → assign move targets
//   3. All living Players     → execute movement
//   4. updatePlatoons()       → Platoon reads SquadReports, issues orders to Squads
//   5. updateSquads()         → Squad reads NPC states, issues NpcCommands;
//                               then builds SquadReports for next Platoon tick
//   6. All NPCs               → execute NpcCommands + AI
//   7. Increment tick counter
//   8. Periodic snapshot dump

void Room::tick(float dt) {
    Logger::get().setTick(tickCount_);

    dummyCtrl_.update(dt, *this);

    for (auto& [id, actor] : actors_) {
        if (auto* p = dynamic_cast<Player*>(actor.get()))
            p->update(dt, *this);
    }

    updatePlatoons(dt);   // ← Platoon decisions (reads last tick's SquadReports)
    updateSquads(dt);     // ← Squad decisions  (issues NpcCommands; builds new SquadReports)

    for (auto& [id, actor] : actors_) {
        if (auto* npc = dynamic_cast<Npc*>(actor.get()))
            npc->update(dt, *this);
    }

    ++tickCount_;

    if (dumpInterval_ > 0 && (tickCount_ % dumpInterval_) == 0)
        dumpSnapshot();
}

// ─── Queries ──────────────────────────────────────────────────────────────────

Actor* Room::findActorById(uint32_t id) const {
    auto it = actors_.find(id);
    return (it != actors_.end()) ? it->second.get() : nullptr;
}

Npc* Room::findNpcById(uint32_t id) {
    return dynamic_cast<Npc*>(findActorById(id));
}

// ─── Squad management ────────────────────────────────────────────────────────

Squad* Room::findSquadById(int id) {
    for (Squad& sq : squads_)
        if (sq.getSquadId() == id) return &sq;
    return nullptr;
}

int Room::createSquad() {
    int id = nextSquadId_++;
    squads_.emplace_back(id);
    return id;
}

void Room::addNpcToSquad(int squadId, uint32_t npcId, bool asLeader) {
    Squad* sq = findSquadById(squadId);
    if (!sq) return;
    sq->addMember(npcId, asLeader);
    Npc* npc = findNpcById(npcId);
    if (!npc) return;
    npc->setSquadId(squadId);
    if (asLeader) npc->setIsLeader(true);
}

void Room::spawnSquad(const std::string& namePrefix,
                       const std::vector<Vec3>& positions,
                       const NpcConfig& cfg) {
    if (positions.empty()) return;
    int sqId = createSquad();
    for (std::size_t i = 0; i < positions.size(); ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%02d",
            namePrefix.c_str(), static_cast<int>(i + 1));
        auto     npc   = std::make_shared<Npc>(buf, positions[i], cfg);
        uint32_t npcId = npc->getId();
        addActor(std::move(npc));
        addNpcToSquad(sqId, npcId, /*asLeader=*/ i == 0);
    }
    Logger::get().log("Room",
        "squad #" + std::to_string(sqId) + " '" + namePrefix
        + "' members=" + std::to_string(positions.size()));
}

// ─── Platoon management ──────────────────────────────────────────────────────

Platoon* Room::findPlatoonById(int id) {
    for (Platoon& plt : platoons_)
        if (plt.platoonId() == id) return &plt;
    return nullptr;
}

int Room::createPlatoon() {
    int id = nextPlatoonId_++;
    platoons_.emplace_back(id);
    return id;
}

void Room::addSquadToPlatoon(int platoonId, int squadId) {
    Platoon* plt = findPlatoonById(platoonId);
    if (plt) plt->addSquad(squadId);
}

void Room::spawnPlatoon(const std::string& namePrefix,
                         const std::vector<std::vector<Vec3>>& squadPositions,
                         const NpcConfig& cfg) {
    if (squadPositions.empty()) return;
    int pltId = createPlatoon();
    for (std::size_t si = 0; si < squadPositions.size(); ++si) {
        char sqName[64];
        std::snprintf(sqName, sizeof(sqName), "%sS%d_",
            namePrefix.c_str(), static_cast<int>(si + 1));
        spawnSquad(sqName, squadPositions[si], cfg);
        int sqId = nextSquadId_ - 1;   // spawnSquad just created this id
        addSquadToPlatoon(pltId, sqId);
    }
    Logger::get().log("Room",
        "platoon #" + std::to_string(pltId) + " '" + namePrefix
        + "' squads=" + std::to_string(squadPositions.size()));
}

// ─── updatePlatoons ──────────────────────────────────────────────────────────
// Platoon reads last tick's SquadReports (already stored via receiveReport)
// and issues orders to Squads via setPlatoonOrder.

void Room::updatePlatoons(float dt) {
    for (Platoon& plt : platoons_)
        plt.update(dt, *this);

    // Remove empty Platoons
    platoons_.erase(
        std::remove_if(platoons_.begin(), platoons_.end(),
            [](const Platoon& p) { return p.isEmpty(); }),
        platoons_.end());
}

// ─── updateSquads ────────────────────────────────────────────────────────────
// Pass 1: each Squad updates (issues NpcCommands to its members).
// Pass 2: build SquadReports and deliver to the owning Platoon for next tick.
// Pass 3: remove empty Squads (disbanded or all members dead).

void Room::updateSquads(float dt) {
    // Pass 1
    for (Squad& sq : squads_)
        sq.update(dt, *this);

    // Pass 2: each squad's report goes only to the Platoon that owns it
    for (Squad& sq : squads_) {
        SquadReport rep = sq.buildReport(*this);
        for (Platoon& plt : platoons_) {
            if (plt.ownsSquad(sq.getSquadId()))
                plt.receiveReport(rep);
        }
    }

    // Pass 3
    squads_.erase(
        std::remove_if(squads_.begin(), squads_.end(),
            [](const Squad& sq) { return sq.isEmpty(); }),
        squads_.end());
}

// ─── findNearestLivingPlayer ─────────────────────────────────────────────────

Player* Room::findNearestLivingPlayer(const Vec3& from, float maxRange) const {
    Player* nearest     = nullptr;
    float   nearestDist = maxRange + 1.f;
    for (auto& [id, actor] : actors_) {
        if (!actor->isAlive()) continue;
        auto* p = dynamic_cast<Player*>(actor.get());
        if (!p) continue;
        float d = Vec3::distance(from, p->getPosition());
        if (d <= maxRange && d < nearestDist) { nearestDist = d; nearest = p; }
    }
    return nearest;
}

// ─── getLivingPlayers ────────────────────────────────────────────────────────

std::vector<Player*> Room::getLivingPlayers() const {
    std::vector<Player*> result;
    for (const auto& [id, actor] : actors_) {
        if (!actor->isAlive()) continue;
        if (auto* p = dynamic_cast<Player*>(actor.get()))
            result.push_back(p);
    }
    return result;
}

// ─── findNearbyNpcPositions ──────────────────────────────────────────────────

void Room::findNearbyNpcPositions(const Vec3& from, float radius,
                                   uint32_t excludeId,
                                   std::vector<Vec3>& out) const {
    for (const auto& [id, actor] : actors_) {
        if (id == excludeId)                    continue;
        if (!actor->isAlive())                  continue;
        if (!dynamic_cast<Npc*>(actor.get()))   continue;
        if (Vec3::distance(from, actor->getPosition()) < radius)
            out.push_back(actor->getPosition());
    }
}

// ─── countNpcsTargeting ──────────────────────────────────────────────────────

int Room::countNpcsTargeting(uint32_t playerId) const {
    int count = 0;
    for (const auto& [id, actor] : actors_) {
        auto* npc = dynamic_cast<Npc*>(actor.get());
        if (!npc || !npc->isAlive())        continue;
        if (npc->getTargetId() != playerId) continue;
        NpcState s = npc->getState();
        if (s == NpcState::Chase        ||
            s == NpcState::AttackWindup  ||
            s == NpcState::AttackRecover ||
            s == NpcState::Reposition)
            ++count;
    }
    return count;
}

// ─── dumpSnapshot ────────────────────────────────────────────────────────────

void Room::dumpSnapshot() const {
    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "  ROOM SNAPSHOT   roomId=" << roomId_
              << "   tick=" << std::setw(4) << tickCount_ << "\n";
    std::cout << "----------------------------------------------------------------\n";

    std::cout << "  [PLAYERS]\n";
    bool hasPlayer = false;
    for (auto& [id, actor] : actors_) {
        if (dynamic_cast<const Player*>(actor.get())) {
            std::cout << "    " << actor->dump() << "\n";
            hasPlayer = true;
        }
    }
    if (!hasPlayer) std::cout << "    (none)\n";

    std::cout << "  [NPCS]\n";
    bool hasNpc = false;
    for (auto& [id, actor] : actors_) {
        if (dynamic_cast<const Npc*>(actor.get())) {
            std::cout << "    " << actor->dump() << "\n";
            hasNpc = true;
        }
    }
    if (!hasNpc) std::cout << "    (none)\n";

    std::cout << "================================================================\n\n";
}

// ─── buildSnapshot ───────────────────────────────────────────────────────────

DebugSnapshot Room::buildSnapshot() const {
    DebugSnapshot snap;
    snap.tick = tickCount_;

    for (auto& [id, actor] : actors_) {
        Vec3 pos    = actor->getPosition();
        Vec3 facing = actor->getFacing();

        if (auto* p = dynamic_cast<const Player*>(actor.get())) {
            DebugPlayerEntry e;
            e.id         = static_cast<int>(p->getId());
            e.x          = pos.x;
            e.z          = pos.z;
            e.dirX       = facing.x;
            e.dirZ       = facing.z;
            e.name       = p->getName();
            e.hp         = p->getHp();
            e.maxHp      = p->getMaxHp();
            e.alive      = p->isAlive();
            e.aggroCount = countNpcsTargeting(p->getId());
            snap.players.push_back(e);

        } else if (auto* npc = dynamic_cast<const Npc*>(actor.get())) {
            DebugNpcEntry e;
            e.id                  = static_cast<int>(npc->getId());
            e.x                   = pos.x;
            e.z                   = pos.z;
            e.dirX                = facing.x;
            e.dirZ                = facing.z;
            e.state               = static_cast<int>(npc->getState());
            e.targetId            = static_cast<int>(npc->getTargetId());
            e.name                = npc->getName();
            e.hp                  = npc->getHp();
            e.maxHp               = npc->getMaxHp();
            e.detectionRange      = npc->getDetectionRange();
            e.attackRange         = npc->getAttackRange();
            e.alive               = npc->isAlive();
            e.homeX               = npc->getSpawnPos().x;
            e.homeZ               = npc->getSpawnPos().z;
            e.windupProgress      = npc->getWindupProgress();
            e.recoverProgress     = npc->getRecoverProgress();
            e.hasRepositionTarget = npc->hasRepositionTarget();
            e.repositionX         = npc->getRepositionTargetPos().x;
            e.repositionZ         = npc->getRepositionTargetPos().z;
            e.squadId             = npc->getSquadId();
            e.isLeader            = npc->getIsLeader();
            snap.npcs.push_back(e);
        }
    }
    return snap;
}

} // namespace sim
