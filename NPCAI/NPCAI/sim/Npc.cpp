#include "Npc.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "NpcGroup.hpp"
#include "Logger.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace sim {

// ─── 파일 범위 헬퍼 ──────────────────────────────────────────────────────────

static const char* npcStateStr(NpcState s) {
    switch (s) {
        case NpcState::Idle:           return "Idle";
        case NpcState::Chase:          return "Chase";
        case NpcState::AttackWindup:   return "AttackWindup";
        case NpcState::AttackRecover:  return "AttackRecover";
        case NpcState::Return:         return "Return";
        case NpcState::Reposition:     return "Reposition";
        case NpcState::Dead:           return "Dead";
        case NpcState::Investigate:    return "Investigate";
    }
    return "?";
}

// ─── 생성자 ───────────────────────────────────────────────────────────────────

Npc::Npc(const std::string& name, const Vec3& pos, const NpcConfig& cfg)
    : Actor(name, pos, cfg.maxHp)
    , spawnPos_(pos)
    , detectionRange_(cfg.detectionRange)
    , attackRange_(cfg.attackRange)
    , moveSpeed_(cfg.moveSpeed)
    , attackDamage_(cfg.attackDamage)
    , attackWindupTime_(cfg.attackWindupTime)
    , attackRecoverTime_(cfg.attackRecoverTime)
    , separationRadius_(cfg.separationRadius)
    , separationWeight_(cfg.separationWeight)
    , canReAggroOnReturn_(cfg.canReAggroOnReturn)
    , overlapThreshold_(cfg.overlapThreshold)
    , returnSpeedMult_(cfg.returnSpeedMult)
    , activityZoneCenter_(pos)
    , activityZoneRadius_(cfg.activityZoneRadius)
    , logPrefix_("NPC:" + name)
{
    nearbyCache_.reserve(16);
}

// ─── 접근자 ───────────────────────────────────────────────────────────────────

float Npc::getWindupProgress() const {
    return (attackWindupTime_ > 0.f)
        ? std::min(1.f, windupTimer_ / attackWindupTime_)
        : 0.f;
}

float Npc::getRecoverProgress() const {
    return (attackRecoverTime_ > 0.f)
        ? std::min(1.f, recoverTimer_ / attackRecoverTime_)
        : 0.f;
}

// ─── 메인 업데이트 분기 ──────────────────────────────────────────────────────

void Npc::update(float dt, Room& room) {
    if (!alive_) {
        updateDead();
        return;
    }

    // Chase 중이고 그룹 메모리 위치가 활동 구역 밖이면 귀환
    // clearMemory()는 호출하지 않음 — 업데이트 순서와 무관하게 각 상태가 독자 판단
    if (groupId_ >= 0 && state_ == NpcState::Chase && targetId_ != 0) {
        NpcGroup* group = room.getNpcGroup(groupId_);
        if (group) {
            const SharedTargetMemory* mem = group->getBestMemory(room.getTickCount());
            if (mem && !group->isInsideActivityArea(mem->lastKnownPosition)) {
                targetId_ = 0;
                transitionTo(NpcState::Return, "그룹 메모리 구역 이탈");
                return;
            }
        }
    }

    switch (state_) {
        case NpcState::Idle:           updateIdle          (dt, room); break;
        case NpcState::Chase:          updateChase         (dt, room); break;
        case NpcState::AttackWindup:   updateAttackWindup  (dt, room); break;
        case NpcState::AttackRecover:  updateAttackRecover (dt, room); break;
        case NpcState::Return:         updateReturn        (dt, room); break;
        case NpcState::Reposition:     updateReposition    (dt, room); break;
        case NpcState::Dead:           /* 종료 상태 */                  break;
        case NpcState::Investigate:    updateInvestigate   (dt, room); break;
    }
}

// ─── transitionTo ─────────────────────────────────────────────────────────────

void Npc::transitionTo(NpcState next, const char* reason) {
    if (state_ == next) return;
    Logger::get().logTransition(name_, npcStateStr(state_), npcStateStr(next), reason);
    if (next == NpcState::AttackWindup)  windupTimer_       = 0.f;
    if (next == NpcState::AttackRecover) recoverTimer_      = 0.f;
    if (next == NpcState::Reposition)    repositionTimer_   = 0.f;
    state_ = next;
}

// ─── Idle ─────────────────────────────────────────────────────────────────────

void Npc::updateIdle(float dt, Room& room) {
    Player* best = selectBestVisibleTarget(room);
    if (best) {
        targetId_ = best->getId();
        // 그룹에 시야 보고
        if (groupId_ >= 0) {
            NpcGroup* group = room.getNpcGroup(groupId_);
            if (group)
                group->reportSight(id_, best->getId(), best->getPosition(),
                                   room.getTickCount());
        }
        char buf[80];
        std::snprintf(buf, sizeof(buf), "target=%s dist=%.1f",
            best->getName().c_str(),
            Vec3::distance(position_, best->getPosition()));
        transitionTo(NpcState::Chase, buf);
        return;
    }

    // 직접 감지 실패 시 공유 메모리 위치 조사
    if (groupId_ >= 0) {
        NpcGroup* group = room.getNpcGroup(groupId_);
        if (group) {
            if (group->getBestMemoryInsideActivityArea(room.getTickCount())) {
                transitionTo(NpcState::Investigate, "공유 메모리 조사");
                return;
            }
            if (group->hasValidMemory(room.getTickCount()))
                return;  // 구역 밖 메모리만 있음 — 자연 만료 대기
        }
    }
}

// ─── Chase ────────────────────────────────────────────────────────────────────

void Npc::updateChase(float dt, Room& room) {
    targetEvalTimer_ -= dt;
    if (targetEvalTimer_ <= 0.f) {
        targetEvalTimer_ = TARGET_EVAL_INTERVAL;
        Player* newBest = selectBestVisibleTarget(room);
        if (newBest && newBest->getId() != targetId_) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "retarget -> %s", newBest->getName().c_str());
            Logger::get().log(logPrefix_, buf);
            targetId_ = newBest->getId();
        }
    }

    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        // 그룹 메모리 위치가 활동 구역 내에 있으면 조사로 전환
        if (!isOutsideActivityZone() && groupId_ >= 0) {
            NpcGroup* group = room.getNpcGroup(groupId_);
            if (group && group->getBestMemoryInsideActivityArea(room.getTickCount())) {
                transitionTo(NpcState::Investigate, "target lost, 조사 시작");
                return;
            }
        }
        transitionTo(NpcState::Return, "target lost");
        return;
    }
    if (isOutsideActivityZone()) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "outside activity zone");
        return;
    }

    // 추격 중 그룹에 위치 보고
    if (groupId_ >= 0) {
        NpcGroup* group = room.getNpcGroup(groupId_);
        if (group) {
            if (auto* p = dynamic_cast<Player*>(target))
                group->reportSight(id_, p->getId(), p->getPosition(),
                                   room.getTickCount());
        }
    }

    if (Vec3::distanceSq(position_, target->getPosition()) <= attackRange_ * attackRange_) {
        transitionTo(NpcState::AttackWindup, "in attack range");
        return;
    }

    Vec3 chaseDir = (target->getPosition() - position_).normalized();
    nearbyCache_.clear();
    room.findNearbyNpcPositions(position_, separationRadius_, id_, nearbyCache_);
    Vec3 sepForce = calcSeparationForce(nearbyCache_);
    Vec3 moveDir  = (chaseDir + sepForce * separationWeight_).normalized();
    facing_   = moveDir;
    position_ += moveDir * (moveSpeed_ * dt);
}

// ─── AttackWindup ─────────────────────────────────────────────────────────────

void Npc::updateAttackWindup(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "target lost during windup");
        return;
    }
    if (isOutsideActivityZone()) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "활동 구역 이탈");
        return;
    }

    nearbyCache_.clear();
    room.findNearbyNpcPositions(position_, separationRadius_, id_, nearbyCache_);
    Vec3 sep = calcSeparationForce(nearbyCache_);
    if (sep.length() > 0.1f)
        facing_ = (facing_ + sep * 0.3f).normalized();

    windupTimer_ += dt;
    if (windupTimer_ >= attackWindupTime_) {
        float dist = Vec3::distance(position_, target->getPosition());
        if (dist <= attackRange_) {
            target->takeDamage(attackDamage_);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "hit %s for %.0f  (hp=%.1f)",
                target->getName().c_str(), attackDamage_, target->getHp());
            Logger::get().log(logPrefix_, buf);

            if (!target->isAlive()) {
                targetId_ = 0;
                transitionTo(NpcState::Return, "target killed");
                return;
            }
            transitionTo(NpcState::AttackRecover, "windup complete");
        }
        else {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "missed %s (dist=%.1f > range=%.1f)",
                target->getName().c_str(), dist, attackRange_);
            Logger::get().log(logPrefix_, buf);
            transitionTo(NpcState::AttackRecover, "swung and missed");
        }
    }
}

// ─── AttackRecover ────────────────────────────────────────────────────────────

void Npc::updateAttackRecover(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "target lost during recover");
        return;
    }
    if (isOutsideActivityZone()) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "활동 구역 이탈");
        return;
    }

    nearbyCache_.clear();
    room.findNearbyNpcPositions(position_, separationRadius_, id_, nearbyCache_);
    Vec3 sep = calcSeparationForce(nearbyCache_);
    if (sep.length() > 0.1f)
        position_ += sep * (separationWeight_ * 0.3f * moveSpeed_ * dt);

    recoverTimer_ += dt;
    if (recoverTimer_ >= attackRecoverTime_) {
        if (isOvercrowded(nearbyCache_)) {
            Vec3 toTarget  = (target->getPosition() - position_).normalized();
            repositionDir_ = (id_ % 2 == 0)
                ? Vec3{  toTarget.z, 0.f, -toTarget.x }
                : Vec3{ -toTarget.z, 0.f,  toTarget.x };
            transitionTo(NpcState::Reposition, "overcrowded after recover");
            return;
        }
        if (Vec3::distanceSq(position_, target->getPosition()) <= attackRange_ * attackRange_)
            transitionTo(NpcState::AttackWindup, "recover done, in range");
        else
            transitionTo(NpcState::Chase, "recover done, out of range");
    }
}

// ─── Return ───────────────────────────────────────────────────────────────────

void Npc::updateReturn(float dt, Room& room) {
    if (canReAggroOnReturn_ && !isOutsideActivityZone()) {
        Player* candidate = selectBestVisibleTarget(room);
        if (candidate) {
            // 재어그로 시 메모리 갱신 — 다음 틱 프리앰블에서 구역 재진입 인식
            if (groupId_ >= 0) {
                NpcGroup* group = room.getNpcGroup(groupId_);
                if (group)
                    group->reportSight(id_, candidate->getId(), candidate->getPosition(),
                                       room.getTickCount());
            }
            targetId_ = candidate->getId();
            char buf[64];
            std::snprintf(buf, sizeof(buf), "re-aggro on %s dist=%.1f",
                candidate->getName().c_str(),
                Vec3::distance(position_, candidate->getPosition()));
            transitionTo(NpcState::Chase, buf);
            return;
        }
    }

    if (Vec3::distanceSq(position_, spawnPos_) < 0.3f * 0.3f) {
        position_ = spawnPos_;
        transitionTo(NpcState::Idle, "reached home");
        return;
    }

    Vec3 homeDir = (spawnPos_ - position_).normalized();
    nearbyCache_.clear();
    room.findNearbyNpcPositions(position_, separationRadius_, id_, nearbyCache_);
    Vec3 sep     = calcSeparationForce(nearbyCache_);
    Vec3 moveDir = (homeDir + sep * (separationWeight_ * 0.25f)).normalized();
    facing_   = moveDir;
    position_ += moveDir * (moveSpeed_ * returnSpeedMult_ * dt);
}

// ─── Reposition ───────────────────────────────────────────────────────────────

void Npc::updateReposition(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "target lost during reposition");
        return;
    }
    if (isOutsideActivityZone()) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "활동 구역 이탈");
        return;
    }

    repositionTimer_ += dt;
    if (repositionTimer_ >= REPOSITION_TIMEOUT) {
        transitionTo(NpcState::Chase, "reposition timeout");
        return;
    }

    nearbyCache_.clear();
    room.findNearbyNpcPositions(position_, separationRadius_, id_, nearbyCache_);

    if (!isOvercrowded(nearbyCache_)) {
        if (Vec3::distanceSq(position_, target->getPosition()) <= attackRange_ * attackRange_)
            transitionTo(NpcState::AttackWindup, "reposition done, in range");
        else
            transitionTo(NpcState::Chase, "reposition done, chasing");
        return;
    }

    Vec3 toTarget = (target->getPosition() - position_).normalized();
    Vec3 sep      = calcSeparationForce(nearbyCache_);
    Vec3 moveDir  = (toTarget + repositionDir_ * 0.8f + sep * separationWeight_).normalized();
    facing_    = moveDir;
    position_ += moveDir * (moveSpeed_ * dt);
}

// ─── Investigate ─────────────────────────────────────────────────────────────

void Npc::updateInvestigate(float dt, Room& room) {
    // 직접 감지 성공 시 Chase로 전환
    {
        Player* best = selectBestVisibleTarget(room);
        if (best) {
            targetId_ = best->getId();
            if (groupId_ >= 0) {
                NpcGroup* group = room.getNpcGroup(groupId_);
                if (group)
                    group->reportSight(id_, best->getId(), best->getPosition(),
                                       room.getTickCount());
            }
            char buf[80];
            std::snprintf(buf, sizeof(buf), "target=%s dist=%.1f",
                best->getName().c_str(),
                Vec3::distance(position_, best->getPosition()));
            transitionTo(NpcState::Chase, buf);
            return;
        }
    }

    if (isOutsideActivityZone()) {
        transitionTo(NpcState::Return, "활동 구역 이탈 (조사 중단)");
        return;
    }

    NpcGroup* group = room.getNpcGroup(groupId_);
    const SharedTargetMemory* mem = group
        ? group->getBestMemoryInsideActivityArea(room.getTickCount())
        : nullptr;
    if (!mem) {
        transitionTo(NpcState::Return, group ? "메모리 만료, 귀환" : "그룹 해체, 귀환");
        return;
    }

    Vec3  diff = mem->lastKnownPosition - position_;
    float dist = diff.length();
    if (dist > 0.5f) {
        facing_    = diff.normalized();
        position_ += facing_ * (moveSpeed_ * dt);
    } else {
        transitionTo(NpcState::Return, "조사 완료, 플레이어 없음");
    }
}

// ─── Dead ─────────────────────────────────────────────────────────────────────

void Npc::updateDead() {
    if (state_ != NpcState::Dead)
        transitionTo(NpcState::Dead, "hp reached 0");
}

// ─── resolveTarget ────────────────────────────────────────────────────────────

Actor* Npc::resolveTarget(Room& room) const {
    if (targetId_ == 0) return nullptr;
    Actor* a = room.findActorById(targetId_);
    if (!a || !a->isAlive()) return nullptr;
    return a;
}

// ─── evaluateTargetScore ──────────────────────────────────────────────────────

float Npc::evaluateTargetScore(const Player* p, Room& room) const {
    float dist = Vec3::distance(position_, p->getPosition());
    float score = std::max(0.f, (1.f - dist / (activityZoneRadius_ * 2.f))) * 50.f;
    if (p->getId() == targetId_) score += 20.f;
    if (dist <= attackRange_)    score += 15.f;

    int aggro = room.countNpcsTargeting(p->getId());
    if (p->getId() == targetId_ && aggro > 0) --aggro;
    score -= static_cast<float>(aggro) * 8.f;

    return score;
}

// ─── selectBestVisibleTarget ─────────────────────────────────────────────────

Player* Npc::selectBestVisibleTarget(Room& room) const {
    NpcGroup* group   = (groupId_ >= 0) ? room.getNpcGroup(groupId_) : nullptr;
    Player*   best    = nullptr;
    float     bestScore = -999.f;
    for (Player* p : room.getLivingPlayers()) {
        if (Vec3::distanceSq(position_, p->getPosition()) > detectionRange_ * detectionRange_) continue;
        if (group && !group->isInsideActivityArea(p->getPosition())) continue;
        float s = evaluateTargetScore(p, room);
        if (s > bestScore) { bestScore = s; best = p; }
    }
    return best;
}

// ─── calcSeparationForce ──────────────────────────────────────────────────────

Vec3 Npc::calcSeparationForce(const std::vector<Vec3>& nearby) const {
    Vec3 force{ 0.f, 0.f, 0.f };
    for (const Vec3& op : nearby) {
        Vec3  away = position_ - op;
        float d    = away.length();
        if (d < 1e-4f) {
            float a = static_cast<float>(id_) * 1.2f;
            force += Vec3{ std::cosf(a), 0.f, std::sinf(a) };
            continue;
        }
        float strength = 1.f - (d / separationRadius_);
        force += (away / d) * strength;
    }
    return force;
}

// ─── isOutsideActivityZone ───────────────────────────────────────────────────

bool Npc::isOutsideActivityZone() const {
    return Vec3::distanceSq(position_, activityZoneCenter_) > activityZoneRadius_ * activityZoneRadius_;
}

// ─── isOvercrowded ────────────────────────────────────────────────────────────

bool Npc::isOvercrowded(const std::vector<Vec3>& nearby) const {
	float checkRadius = separationRadius_ * 0.7f;
    int   count       = 0;
    for ( const Vec3& pos : nearby )
        if ( Vec3::distanceSq( position_, pos ) < checkRadius * checkRadius )
            ++count;
    return count >= overlapThreshold_;
}

// ─── dump ─────────────────────────────────────────────────────────────────────

std::string Npc::dump() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "%-12s hp=%5.1f/%-5.1f pos=%s state=%s%s",
        name_.c_str(), hp_, maxHp_,
        position_.toString().c_str(),
        npcStateStr(state_),
        alive_ ? "" : " [DEAD]");
    return buf;
}

} // namespace sim
