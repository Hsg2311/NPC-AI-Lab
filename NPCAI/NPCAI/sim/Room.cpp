#include "Room.hpp"
#include "Actor.hpp"
#include "Player.hpp"
#include "Npc.hpp"
#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>

namespace sim {

// gridKey 인코딩 상수 — 월드 좌표 ±60,000 유닛까지 충돌 없이 커버
static constexpr int   GRID_COORD_OFFSET = 10000;
static constexpr int64_t GRID_COORD_RANGE = 20001LL;

Room::Room(uint32_t roomId, uint32_t dumpInterval)
    : roomId_(roomId), dumpInterval_(dumpInterval)
{}

void Room::addActor(std::shared_ptr<Actor> actor) {
    if (auto p = std::dynamic_pointer_cast<Player>(actor))
        players_[p->getId()] = std::move(p);
    else if (auto n = std::dynamic_pointer_cast<Npc>(actor))
        npcs_[n->getId()] = std::move(n);
}

// ─── tick ─────────────────────────────────────────────────────────────────────
// 업데이트 순서:
//   1. 로거 틱 카운터 동기화
//   2. DummyPlayerController  -> 이동 목표 할당
//   3. 생존한 모든 Player     -> 이동 실행
//   4. 모든 NpcGroup          -> 메모리 만료 슬롯 정리
//   5. 캐시 재구성             -> aggroCount_ / spatialGrid_
//   6. 모든 NPC               -> AI 실행
//   7. 틱 카운터 증가
//   8. 주기적 스냅샷 출력

void Room::tick(float dt) {
    Logger::get().setTick(tickCount_);

    dummyCtrl_.update(dt, *this);

    for (auto& [id, p] : players_)
        p->update(dt, *this);

    for (auto& group : npcGroups_)
        group->update(tickCount_);

    // NPC 업데이트 전 캐시 재구성
    rebuildLivingPlayersCache();
    rebuildAggroCount();
    rebuildSpatialGrid();

    for (auto& [id, npc] : npcs_)
        npc->update(dt, *this);

    ++tickCount_;

    if (dumpInterval_ > 0 && (tickCount_ % dumpInterval_) == 0)
        dumpSnapshot();
}

// ─── 쿼리 ─────────────────────────────────────────────────────────────────────

Actor* Room::findActorById(uint32_t id) const {
    auto it = players_.find(id);
    if (it != players_.end()) return it->second.get();
    auto it2 = npcs_.find(id);
    if (it2 != npcs_.end()) return it2->second.get();
    return nullptr;
}

// ─── findNearestLivingPlayer ─────────────────────────────────────────────────

Player* Room::findNearestLivingPlayer(const Vec3& from, float maxRange) const {
    Player* nearest       = nullptr;
    float   maxRangeSq    = maxRange * maxRange;
    float   nearestDistSq = (maxRange + 1.f) * (maxRange + 1.f);
    for (const auto& [id, p] : players_) {
        if (!p->isAlive()) continue;
        float dSq = Vec3::distanceSq(from, p->getPosition());
        if (dSq <= maxRangeSq && dSq < nearestDistSq) { nearestDistSq = dSq; nearest = p.get(); }
    }
    return nearest;
}

// ─── rebuildLivingPlayersCache / getLivingPlayers ────────────────────────────

void Room::rebuildLivingPlayersCache() {
    livingPlayersCache_.clear();
    for (const auto& [id, p] : players_)
        if (p->isAlive())
            livingPlayersCache_.push_back(p.get());
}

const std::vector<Player*>& Room::getLivingPlayers() const {
    return livingPlayersCache_;
}

// ─── findNearbyNpcPositions ──────────────────────────────────────────────────
// C: 공간 분할 그리드로 O(N) 전체 스캔 → 주변 셀만 조회

void Room::findNearbyNpcPositions(const Vec3& from, float radius,
                                   uint32_t excludeId,
                                   std::vector<Vec3>& out) const {
    int minCx = static_cast<int>(std::floor((from.x - radius) / GRID_CELL_SIZE));
    int maxCx = static_cast<int>(std::floor((from.x + radius) / GRID_CELL_SIZE));
    int minCz = static_cast<int>(std::floor((from.z - radius) / GRID_CELL_SIZE));
    int maxCz = static_cast<int>(std::floor((from.z + radius) / GRID_CELL_SIZE));

    for (int cx = minCx; cx <= maxCx; ++cx) {
        for (int cz = minCz; cz <= maxCz; ++cz) {
            auto it = spatialGrid_.find(gridKey(cx, cz));
            if (it == spatialGrid_.end()) continue;
            for (uint32_t npcId : it->second) {
                if (npcId == excludeId) continue;
                auto nit = npcs_.find(npcId);
                if (nit == npcs_.end() || !nit->second->isAlive()) continue;
                const Vec3& pos = nit->second->getPosition();
                if (Vec3::distanceSq(from, pos) < radius * radius)
                    out.push_back(pos);
            }
        }
    }
}

// ─── countNpcsTargeting ──────────────────────────────────────────────────────
// B: aggroCount_ 캐시 조회 O(1) (rebuildAggroCount()에서 틱당 1회 재구성)

int Room::countNpcsTargeting(uint32_t playerId) const {
    auto it = aggroCount_.find(playerId);
    return (it != aggroCount_.end()) ? it->second : 0;
}

// ─── rebuildAggroCount ───────────────────────────────────────────────────────

void Room::rebuildAggroCount() {
    aggroCount_.clear();
    for (const auto& [id, npc] : npcs_) {
        if (!npc->isAlive()) continue;
        NpcState s = npc->getState();
        if (s == NpcState::Chase        ||
            s == NpcState::AttackWindup  ||
            s == NpcState::AttackRecover ||
            s == NpcState::Reposition)
            aggroCount_[npc->getTargetId()]++;
    }
}

// ─── rebuildSpatialGrid ──────────────────────────────────────────────────────

void Room::rebuildSpatialGrid() {
    spatialGrid_.clear();
    for (const auto& [id, npc] : npcs_) {
        if (!npc->isAlive()) continue;
        Vec3 pos = npc->getPosition();
        int  cx  = static_cast<int>(std::floor(pos.x / GRID_CELL_SIZE));
        int  cz  = static_cast<int>(std::floor(pos.z / GRID_CELL_SIZE));
        spatialGrid_[gridKey(cx, cz)].push_back(id);
    }
}

// ─── gridKey ─────────────────────────────────────────────────────────────────

int64_t Room::gridKey(int cx, int cz) {
    return (static_cast<int64_t>(cx) + GRID_COORD_OFFSET) * GRID_COORD_RANGE
         + (static_cast<int64_t>(cz) + GRID_COORD_OFFSET);
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
    if (players_.empty()) {
        std::cout << "    (none)\n";
    } else {
        for (const auto& [id, p] : players_)
            std::cout << "    " << p->dump() << "\n";
    }

    std::cout << "  [NPCS]\n";
    if (npcs_.empty()) {
        std::cout << "    (none)\n";
    } else {
        for (const auto& [id, npc] : npcs_)
            std::cout << "    " << npc->dump() << "\n";
    }

    std::cout << "================================================================\n\n";
}

// ─── buildSnapshot ───────────────────────────────────────────────────────────

DebugSnapshot Room::buildSnapshot() const {
    DebugSnapshot snap;
    snap.tick = tickCount_;

    for (const auto& [id, p] : players_) {
        Vec3 pos    = p->getPosition();
        Vec3 facing = p->getFacing();
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
    }

    for (const auto& [id, npc] : npcs_) {
        Vec3 pos    = npc->getPosition();
        Vec3 facing = npc->getFacing();
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
