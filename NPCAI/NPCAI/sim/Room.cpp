#include "Room.hpp"
#include "Actor.hpp"
#include "Player.hpp"
#include "Npc.hpp"
#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <vector>

namespace sim {

Room::Room(uint32_t roomId, uint32_t dumpInterval)
    : roomId_(roomId), dumpInterval_(dumpInterval)
{}

void Room::addActor(std::shared_ptr<Actor> actor) {
    actors_[actor->getId()] = std::move(actor);
}

// ─── tick ────────────────────────────────────────────────────────────────────
// Update order:
//   1. Sync logger tick counter
//   2. DummyPlayerController  → assign move targets
//   3. All living Players     → execute movement
//   4. All NPCs               → AI decision + movement
//   5. Increment tick counter
//   6. Periodic snapshot dump

void Room::tick(float dt) {
    Logger::get().setTick(tickCount_);

    // 1) Assign waypoint targets to players
    dummyCtrl_.update(dt, *this);

    // 2) Update players
    for (auto& [id, actor] : actors_) {
        if (auto* p = dynamic_cast<Player*>(actor.get())) {
            p->update(dt, *this);
        }
    }

    // 3) Update NPCs
    for (auto& [id, actor] : actors_) {
        if (auto* npc = dynamic_cast<Npc*>(actor.get())) {
            npc->update(dt, *this);
        }
    }

    ++tickCount_;

    if (dumpInterval_ > 0 && (tickCount_ % dumpInterval_) == 0) {
        dumpSnapshot();
    }
}

// ─── Queries ─────────────────────────────────────────────────────────────────

Actor* Room::findActorById(uint32_t id) const {
    auto it = actors_.find(id);
    return (it != actors_.end()) ? it->second.get() : nullptr;
}

Player* Room::findNearestLivingPlayer(const Vec3& from, float maxRange) const {
    Player* nearest     = nullptr;
    float   nearestDist = maxRange + 1.f;

    for (auto& [id, actor] : actors_) {
        if (!actor->isAlive()) continue;
        auto* p = dynamic_cast<Player*>(actor.get());
        if (!p) continue;

        float d = Vec3::distance(from, p->getPosition());
        if (d <= maxRange && d < nearestDist) {
            nearestDist = d;
            nearest     = p;
        }
    }
    return nearest;
}

// ─── Snapshot dump ────────────────────────────────────────────────────────────

void Room::dumpSnapshot() const {
    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "  ROOM SNAPSHOT   roomId=" << roomId_
              << "   tick=" << std::setw(4) << tickCount_ << "\n";
    std::cout << "----------------------------------------------------------------\n";

    // Players
    std::cout << "  [PLAYERS]\n";
    bool hasPlayer = false;
    for (auto& [id, actor] : actors_) {
        if (dynamic_cast<const Player*>(actor.get())) {
            std::cout << "    " << actor->dump() << "\n";
            hasPlayer = true;
        }
    }
    if (!hasPlayer) std::cout << "    (none)\n";

    // NPCs
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

// ─── buildSnapshot ────────────────────────────────────────────────────────────
// Collects all actor data into a plain-data structure for the renderer.
// No rendering types; sim/ stays independent of viz/.

DebugSnapshot Room::buildSnapshot() const {
    DebugSnapshot snap;
    snap.tick = tickCount_;

    for (auto& [id, actor] : actors_) {
        Vec3 pos     = actor->getPosition();
        Vec3 facing  = actor->getFacing();

        if (auto* p = dynamic_cast<const Player*>(actor.get())) {
            DebugPlayerEntry e;
            e.id    = static_cast<int>(p->getId());
            e.x     = pos.x;
            e.z     = pos.z;
            e.dirX  = facing.x;
            e.dirZ  = facing.z;
            e.name  = p->getName();
            e.hp    = p->getHp();
            e.maxHp = p->getMaxHp();
            e.alive = p->isAlive();
            snap.players.push_back(e);

        } else if (auto* npc = dynamic_cast<const Npc*>(actor.get())) {
            DebugNpcEntry e;
            e.id             = static_cast<int>(npc->getId());
            e.x              = pos.x;
            e.z              = pos.z;
            e.dirX           = facing.x;
            e.dirZ           = facing.z;
            e.state          = static_cast<int>(npc->getState());
            e.targetId       = static_cast<int>(npc->getTargetId());
            e.name           = npc->getName();
            e.hp             = npc->getHp();
            e.maxHp          = npc->getMaxHp();
            e.detectionRange = npc->getDetectionRange();
            e.attackRange    = npc->getAttackRange();
            e.alive          = npc->isAlive();
            snap.npcs.push_back(e);
        }
    }
    return snap;
}

} // namespace sim
