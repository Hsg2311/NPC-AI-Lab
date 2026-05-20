#include "Room.hpp"
#include "Actor.hpp"
#include "Player.hpp"
#include "Npc.hpp"
#include "TacticalNpc.hpp"
#include "TacticalSquad.hpp"
#include "PlatoonLeader.hpp"
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
static constexpr float SOFT_BLOCK_RADIUS = 2.4f;
static constexpr float SOFT_BLOCK_MIN_SPEED = 0.35f;
static constexpr float SOFT_BLOCK_PUSH_SPEED = 5.0f;
static constexpr float SHIELD_WALL_HARD_BLOCK_RADIUS = 2.6f;
static constexpr int SHIELD_WALL_HARD_BLOCK_ITERATIONS = 3;
static constexpr float SHIELD_WALL_KNOCKBACK_PADDING = 0.4f;
static constexpr float SHIELD_WALL_KNOCKBACK_SPEED = 90.f;
static constexpr float SHIELD_WALL_KNOCKBACK_DURATION = 0.32f;

Room::Room(uint32_t roomId, uint32_t dumpInterval)
    : roomId_(roomId), dumpInterval_(dumpInterval)
{}

void Room::addActor(std::shared_ptr<Actor> actor) {
    if (auto p = std::dynamic_pointer_cast<Player>(actor))
        players_[p->getId()] = std::move(p);
    else if (auto n = std::dynamic_pointer_cast<Npc>(actor))
        npcs_[n->getId()] = std::move(n);
}

void Room::addTacticalNpc(std::shared_ptr<TacticalNpc> npc) {
    tacticalNpcs_[npc->getId()] = std::move(npc);
}

void Room::addTacticalSquad(std::unique_ptr<TacticalSquad> squad) {
    tacticalSquads_.push_back(std::move(squad));
}

void Room::removeTacticalNpc(uint32_t npcId) {
    tacticalNpcs_.erase(npcId);
    shieldWallBlockerIds_.erase(
        std::remove(shieldWallBlockerIds_.begin(), shieldWallBlockerIds_.end(), npcId),
        shieldWallBlockerIds_.end());
}

void Room::removeTacticalSquad(int squadId) {
    tacticalSquads_.erase(
        std::remove_if(tacticalSquads_.begin(), tacticalSquads_.end(),
            [squadId](const std::unique_ptr<TacticalSquad>& squad) {
                return squad && squad->getSquadId() == squadId;
            }),
        tacticalSquads_.end());
}

void Room::registerPlatoonLeader(PlatoonLeader* leader) {
    platoonLeaders_.push_back(leader);
}

// ─── tick ─────────────────────────────────────────────────────────────────────
// 업데이트 순서:
//   1. 로거 틱 카운터 동기화
//   2. DummyPlayerController   → 이동 목표 할당
//   3. 생존한 모든 Player      → 이동 실행
//   4. 모든 NpcGroup           → 메모리 만료 슬롯 정리
//   5. 캐시 재구성              → livingPlayers / aggroCount_ / spatialGrid_
//   6. 모든 NPC                → AI 실행
//   7. PlatoonLeader           → evaluateTactics + 자체 전투 FSM
//   8. TacticalSquad           → 슬롯 계산 + 멤버 명령 발행
//   9. TacticalNpc 멤버        → 명령 소비 + FSM 실행
//  10. 틱 카운터 증가
//  11. 주기적 스냅샷 출력

void Room::tick(float dt) {
    Logger::get().setTick(tickCount_);
    debugTelegraphs_.clear();

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

    // 전술 NPC 시스템: PlatoonLeader → TacticalSquad → TacticalNpc 멤버 순서
    for (auto* leader : platoonLeaders_)
        leader->update(dt, *this);

    for (auto& sq : tacticalSquads_)
        sq->update(dt, *this);

    for (auto& [id, tnpc] : tacticalNpcs_) {
        // PlatoonLeader는 위에서 이미 업데이트됨
        if (tnpc->typeName() != std::string("PlatoonLeader"))
            tnpc->update(dt, *this);
    }

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
    auto it3 = tacticalNpcs_.find(id);
    if (it3 != tacticalNpcs_.end()) return it3->second.get();
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
                const Actor* actor = nullptr;
                auto nit = npcs_.find(npcId);
                if (nit != npcs_.end()) {
                    actor = nit->second.get();
                } else {
                    auto tit = tacticalNpcs_.find(npcId);
                    if (tit != tacticalNpcs_.end())
                        actor = tit->second.get();
                }
                if (!actor || !actor->isAlive()) continue;
                const Vec3& pos = actor->getPosition();
                if (Vec3::distanceSq(from, pos) < radius * radius)
                    out.push_back(pos);
            }
        }
    }
}

Vec3 Room::adjustPlayerMoveForNpcSoftBlock(const Vec3& playerPos,
                                           const Vec3& desiredMove,
                                           float dt,
                                           bool applyShieldWallHardBlock) const {
    if (desiredMove.lengthSq() < 1e-6f)
        return desiredMove;

    Vec3 predicted = playerPos + desiredMove;
    Vec3 push{};
    float strongest = 0.f;
    const float radiusSq = SOFT_BLOCK_RADIUS * SOFT_BLOCK_RADIUS;

    auto accumulateBlock = [&](const Actor* actor) {
        if (!actor || !actor->isAlive()) return;

        Vec3 away = predicted - actor->getPosition();
        float distSq = away.lengthSq();
        if (distSq >= radiusSq) return;

        float dist = std::sqrt(std::max(distSq, 1e-6f));
        float t = 1.f - (dist / SOFT_BLOCK_RADIUS);
        if (t > strongest) strongest = t;

        Vec3 pushDir = (dist > 1e-3f) ? (away / dist) : desiredMove.normalized() * -1.f;
        push += pushDir * t;
    };

    for (const auto& [id, npc] : npcs_)
        accumulateBlock(npc.get());
    for (const auto& [id, tnpc] : tacticalNpcs_)
        accumulateBlock(tnpc.get());

    float speedScale = 1.f - strongest * (1.f - SOFT_BLOCK_MIN_SPEED);
    Vec3 adjusted = desiredMove * speedScale;

    float pushLen = push.length();
    if (pushLen > 1e-3f) {
        float maxPush = SOFT_BLOCK_PUSH_SPEED * dt;
        adjusted += (push / pushLen) * std::min(pushLen * maxPush, maxPush);
    }

    if (applyShieldWallHardBlock && !shieldWallBlockerIds_.empty()) {
        Vec3 blockedPos = playerPos + adjusted;
        const float hardRadiusSq =
            SHIELD_WALL_HARD_BLOCK_RADIUS * SHIELD_WALL_HARD_BLOCK_RADIUS;

        for (int iter = 0; iter < SHIELD_WALL_HARD_BLOCK_ITERATIONS; ++iter) {
            bool changed = false;

            for (uint32_t blockerId : shieldWallBlockerIds_) {
                const Actor* blocker = findActorById(blockerId);
                if (!blocker || !blocker->isAlive())
                    continue;

                Vec3 fromBlocker = playerPos - blocker->getPosition();
                Vec3 toBlocker = blockedPos - blocker->getPosition();
                fromBlocker.y = 0.f;
                toBlocker.y = 0.f;

                Vec3 move = toBlocker - fromBlocker;
                float moveLenSq = move.lengthSq();
                float t = 1.f;
                if (moveLenSq > 1e-6f)
                    t = std::max(0.f, std::min(1.f, (-fromBlocker.dot(move)) / moveLenSq));

                Vec3 closest = fromBlocker + move * t;
                if (closest.lengthSq() >= hardRadiusSq)
                    continue;

                Vec3 pushDir;
                if (fromBlocker.lengthSq() >= hardRadiusSq && fromBlocker.lengthSq() > 1e-6f) {
                    pushDir = fromBlocker.normalized();
                } else if (toBlocker.lengthSq() > 1e-6f) {
                    pushDir = toBlocker.normalized();
                } else if (desiredMove.lengthSq() > 1e-6f) {
                    pushDir = desiredMove.normalized() * -1.f;
                } else {
                    pushDir = Vec3{ 1.f, 0.f, 0.f };
                }

                blockedPos = blocker->getPosition() +
                    pushDir * SHIELD_WALL_HARD_BLOCK_RADIUS;
                blockedPos.y = playerPos.y;
                changed = true;
            }

            if (!changed)
                break;
        }

        adjusted = blockedPos - playerPos;
    }

    return adjusted;
}

void Room::setShieldWallBlockers(const std::vector<uint32_t>& blockerIds) {
    shieldWallBlockerIds_ = blockerIds;
}

void Room::clearShieldWallBlockers() {
    shieldWallBlockerIds_.clear();
}

void Room::knockPlayersOutOfShieldWall(const Vec3& center, float ringRadius) {
    float safeRadius = ringRadius + SHIELD_WALL_HARD_BLOCK_RADIUS +
        SHIELD_WALL_KNOCKBACK_PADDING;
    float safeRadiusSq = safeRadius * safeRadius;

    for (auto& [id, player] : players_) {
        if (!player || !player->isAlive())
            continue;

        Vec3 offset = player->getPosition() - center;
        offset.y = 0.f;
        float distSq = offset.lengthSq();
        if (distSq >= safeRadiusSq)
            continue;

        float dist = std::sqrt(std::max(distSq, 1e-6f));
        Vec3 outDir = (distSq > 1e-6f) ? (offset / dist) : Vec3{ 1.f, 0.f, 0.f };
        float requiredSpeed = (safeRadius - dist) / SHIELD_WALL_KNOCKBACK_DURATION;
        float knockbackSpeed = std::max(SHIELD_WALL_KNOCKBACK_SPEED, requiredSpeed);

        Vec3 targetOffset = player->getMoveTarget() - center;
        targetOffset.y = 0.f;
        if (targetOffset.lengthSq() < safeRadiusSq)
            player->setMoveTarget(center + outDir * safeRadius);

        player->applyKnockback(outDir, knockbackSpeed, SHIELD_WALL_KNOCKBACK_DURATION);
    }
}

// ─── applyDamageToActorsInRange ──────────────────────────────────────────────

int Room::applyDamageToActorsInRange(const Vec3& center, float radius, float damage) {
    int   hits     = 0;
    float radiusSq = radius * radius;

    for (auto& [id, npc] : npcs_) {
        if (!npc->isAlive()) continue;
        if (Vec3::distanceSq(center, npc->getPosition()) <= radiusSq) {
            npc->takeDamage(damage);
            ++hits;
        }
    }
    for (auto& [id, tnpc] : tacticalNpcs_) {
        if (!tnpc->isAlive()) continue;
        if (Vec3::distanceSq(center, tnpc->getPosition()) <= radiusSq) {
            tnpc->takeDamage(damage);
            ++hits;
        }
    }
    return hits;
}

int Room::applyDamageToPlayersInRange(const Vec3& center, float radius, float damage) {
    int   hits     = 0;
    float radiusSq = radius * radius;

    for (auto& [id, player] : players_) {
        if (!player || !player->isAlive())
            continue;
        if (Vec3::distanceSq(center, player->getPosition()) <= radiusSq) {
            player->takeDamage(damage);
            ++hits;
        }
    }
    return hits;
}

void Room::addDebugTelegraph(const DebugTelegraphEntry& telegraph) {
    debugTelegraphs_.push_back(telegraph);
}

// ─── countNpcsTargeting ──────────────────────────────────────────────────────
// B: aggroCount_ 캐시 조회 O(1) (rebuildAggroCount()에서 틱당 1회 재구성)

// WedgeCharge hit registry.

uint32_t Room::beginWedgeCharge() {
    uint32_t chargeId = nextWedgeChargeId_++;
    if (nextWedgeChargeId_ == 0)
        nextWedgeChargeId_ = 1;
    wedgeChargeHits_[chargeId];
    return chargeId;
}

bool Room::tryApplyWedgeChargeHit(uint32_t chargeId, Player& player, float damage) {
    if (chargeId == 0 || !player.isAlive())
        return false;

    auto& hits = wedgeChargeHits_[chargeId];
    uint32_t playerId = player.getId();
    if (hits.find(playerId) != hits.end())
        return false;

    player.takeDamage(damage);
    hits.insert(playerId);
    return true;
}

void Room::endWedgeCharge(uint32_t chargeId) {
    if (chargeId == 0)
        return;
    wedgeChargeHits_.erase(chargeId);
}

// countNpcsTargeting.

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
    for (const auto& [id, npc] : tacticalNpcs_) {
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
    snap.telegraphs = debugTelegraphs_;

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
        e.hp             = p->getHp();
        e.maxHp          = p->getMaxHp();
        e.alive          = p->isAlive();
        e.aggroCount     = countNpcsTargeting(p->getId());
        e.attackState    = p->getAttackState();
        e.attackProgress = p->getAttackProgress();
        e.attackRange    = p->getAttackRange();
        e.isDummy        = dummyCtrl_.hasControl(p->getId());
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

    for (const auto& [id, tnpc] : tacticalNpcs_) {
        Vec3 pos    = tnpc->getPosition();
        Vec3 facing = tnpc->getFacing();
        Vec3 slot   = tnpc->getAssignedSlot();
        auto* leader = dynamic_cast<PlatoonLeader*>(tnpc.get());
        DebugTacticalNpcEntry e;
        e.id             = static_cast<int>(tnpc->getId());
        e.x              = pos.x;
        e.z              = pos.z;
        e.dirX           = facing.x;
        e.dirZ           = facing.z;
        e.state          = static_cast<int>(tnpc->getState());
        e.targetId       = static_cast<int>(tnpc->getTargetId());
        e.name           = tnpc->getName();
        e.hp             = tnpc->getHp();
        e.maxHp          = tnpc->getMaxHp();
        e.attackRange    = tnpc->getAttackRange();
        e.alive          = tnpc->isAlive();
        e.homeX          = tnpc->getSpawnPos().x;
        e.homeZ          = tnpc->getSpawnPos().z;
        e.windupProgress = tnpc->getWindupProgress();
        e.recoverProgress= tnpc->getRecoverProgress();
        e.squadId        = tnpc->getSquadId();
        e.isLeader       = (leader != nullptr);
        e.slotX          = slot.x;
        e.slotZ          = slot.z;
        snap.tacticalNpcs.push_back(e);
    }

    return snap;
}

} // namespace sim
