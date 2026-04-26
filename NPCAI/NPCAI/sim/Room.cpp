#include "Room.hpp"
#include "Actor.hpp"
#include "Player.hpp"
#include "Npc.hpp"
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
// 업데이트 순서:
//   1. 로거 틱 카운터 동기화
//   2. DummyPlayerController  -> 이동 목표 할당
//   3. 생존한 모든 Player     -> 이동 실행
//   4. 모든 NPC               -> AI 실행
//   5. 틱 카운터 증가
//   6. 주기적 스냅샷 출력

void Room::tick(float dt) {
    Logger::get().setTick(tickCount_);

    dummyCtrl_.update(dt, *this);

    for (auto& [id, actor] : actors_) {
        if (auto* p = dynamic_cast<Player*>(actor.get()))
            p->update(dt, *this);
    }

    // NPC 업데이트 전 그룹 메모리 만료 슬롯 정리
    for (auto& group : npcGroups_)
        group->update(tickCount_);

    for (auto& [id, actor] : actors_) {
        if (auto* npc = dynamic_cast<Npc*>(actor.get()))
            npc->update(dt, *this);
    }

    ++tickCount_;

    if (dumpInterval_ > 0 && (tickCount_ % dumpInterval_) == 0)
        dumpSnapshot();
}

// ─── 쿼리 ─────────────────────────────────────────────────────────────────────

Actor* Room::findActorById(uint32_t id) const {
    auto it = actors_.find(id);
    return (it != actors_.end()) ? it->second.get() : nullptr;
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

// ─── createNpcGroup / getNpcGroup ────────────────────────────────────────────

NpcGroup* Room::createNpcGroup(const Vec3& center, float radius,
                                uint32_t memoryDurationTick) {
    int id = static_cast<int>(npcGroups_.size());
    npcGroups_.push_back(
        std::make_unique<NpcGroup>(id, center, radius, memoryDurationTick));
    return npcGroups_.back().get();
}

NpcGroup* Room::getNpcGroup(int groupId) {
    if (groupId < 0 || groupId >= static_cast<int>(npcGroups_.size()))
        return nullptr;
    return npcGroups_[groupId].get();
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
            e.activityZoneCenterX = npc->getActivityZoneCenter().x;
            e.activityZoneCenterZ = npc->getActivityZoneCenter().z;
            e.activityZoneRadius  = npc->getActivityZoneRadius();
            e.groupId             = npc->getGroupId();
            snap.npcs.push_back(e);
        }
    }

    // 그룹 활동 구역 및 공유 메모리 위치
    for (const auto& group : npcGroups_) {
        DebugGroupEntry g;
        g.groupId  = group->getGroupId();
        g.centerX  = group->getCenter().x;
        g.centerZ  = group->getCenter().z;
        g.radius   = group->getRadius();
        const SharedTargetMemory* mem = group->getBestMemory(tickCount_);
        if (mem) {
            g.hasMemory = true;
            g.memoryX   = mem->lastKnownPosition.x;
            g.memoryZ   = mem->lastKnownPosition.z;
        }
        snap.groups.push_back(g);
    }

    return snap;
}

} // namespace sim
