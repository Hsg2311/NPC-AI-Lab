#include "Npc.hpp"
#include "Room.hpp"
#include "Player.hpp"
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
{}

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
    switch (state_) {
        case NpcState::Idle:           updateIdle          (dt, room); break;
        case NpcState::Chase:          updateChase         (dt, room); break;
        case NpcState::AttackWindup:   updateAttackWindup  (dt, room); break;
        case NpcState::AttackRecover:  updateAttackRecover (dt, room); break;
        case NpcState::Return:         updateReturn        (dt, room); break;
        case NpcState::Reposition:     updateReposition    (dt, room); break;
        case NpcState::Dead:           /* 종료 상태 */                  break;
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

void Npc::updateIdle(float /*dt*/, Room& room) {
    auto    players   = room.getLivingPlayers();
    Player* best      = nullptr;
    float   bestScore = -999.f;

    for (Player* p : players) {
        if (Vec3::distance(position_, p->getPosition()) > detectionRange_) continue;
        float s = evaluateTargetScore(p, room);
        if (s > bestScore) { bestScore = s; best = p; }
    }
    if (!best) return;

    targetId_ = best->getId();
    char buf[80];
    std::snprintf(buf, sizeof(buf), "target=%s dist=%.1f",
        best->getName().c_str(),
        Vec3::distance(position_, best->getPosition()));
    transitionTo(NpcState::Chase, buf);
}

// ─── Chase ────────────────────────────────────────────────────────────────────

void Npc::updateChase(float dt, Room& room) {
    targetEvalTimer_ -= dt;
    if (targetEvalTimer_ <= 0.f) {
        targetEvalTimer_ = TARGET_EVAL_INTERVAL;
        Player* newBest = selectBestTarget(room);
        if (newBest && newBest->getId() != targetId_) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "retarget -> %s", newBest->getName().c_str());
            Logger::get().log("NPC:" + name_, buf);
            targetId_ = newBest->getId();
        }
    }

    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "target lost");
        return;
    }
    if (isOutsideActivityZone()) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "outside activity zone");
        return;
    }

    float dist = Vec3::distance(position_, target->getPosition());
    if (dist <= attackRange_) {
        transitionTo(NpcState::AttackWindup, "in attack range");
        return;
    }

    Vec3 chaseDir = (target->getPosition() - position_).normalized();
    Vec3 sepForce = calcSeparationForce(room);
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

    Vec3 sep = calcSeparationForce(room);
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
            Logger::get().log("NPC:" + name_, buf);

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
            Logger::get().log("NPC:" + name_, buf);
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

    Vec3 sep = calcSeparationForce(room);
    if (sep.length() > 0.1f)
        position_ += sep * (separationWeight_ * 0.3f * moveSpeed_ * dt);

    recoverTimer_ += dt;
    if (recoverTimer_ >= attackRecoverTime_) {
        if (isOvercrowded(room)) {
            Vec3 toTarget  = (target->getPosition() - position_).normalized();
            repositionDir_ = (id_ % 2 == 0)
                ? Vec3{  toTarget.z, 0.f, -toTarget.x }
                : Vec3{ -toTarget.z, 0.f,  toTarget.x };
            transitionTo(NpcState::Reposition, "overcrowded after recover");
            return;
        }
        float dist = Vec3::distance(position_, target->getPosition());
        if (dist <= attackRange_)
            transitionTo(NpcState::AttackWindup, "recover done, in range");
        else
            transitionTo(NpcState::Chase, "recover done, out of range");
    }
}

// ─── Return ───────────────────────────────────────────────────────────────────

void Npc::updateReturn(float dt, Room& room) {
    if (canReAggroOnReturn_ && !isOutsideActivityZone()) {
        Player* candidate = selectBestTarget(room);
        if (candidate &&
            Vec3::distance(position_, candidate->getPosition()) <= detectionRange_) {
            targetId_ = candidate->getId();
            char buf[64];
            std::snprintf(buf, sizeof(buf), "re-aggro on %s dist=%.1f",
                candidate->getName().c_str(),
                Vec3::distance(position_, candidate->getPosition()));
            transitionTo(NpcState::Chase, buf);
            return;
        }
    }

    float distToHome = Vec3::distance(position_, spawnPos_);
    if (distToHome < 0.3f) {
        position_ = spawnPos_;
        transitionTo(NpcState::Idle, "reached home");
        return;
    }

    Vec3 homeDir = (spawnPos_ - position_).normalized();
    Vec3 sep     = calcSeparationForce(room);
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

    if (!isOvercrowded(room)) {
        float dist = Vec3::distance(position_, target->getPosition());
        if (dist <= attackRange_)
            transitionTo(NpcState::AttackWindup, "reposition done, in range");
        else
            transitionTo(NpcState::Chase, "reposition done, chasing");
        return;
    }

    Vec3 toTarget = (target->getPosition() - position_).normalized();
    Vec3 sep      = calcSeparationForce(room);
    Vec3 moveDir  = (toTarget + repositionDir_ * 0.8f + sep * separationWeight_).normalized();
    facing_    = moveDir;
    position_ += moveDir * (moveSpeed_ * dt);
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

// ─── selectBestTarget ─────────────────────────────────────────────────────────

Player* Npc::selectBestTarget(Room& room) const {
    auto    players   = room.getLivingPlayers();
    Player* best      = nullptr;
    float   bestScore = -999.f;
    for (Player* p : players) {
        float s = evaluateTargetScore(p, room);
        if (s > bestScore) { bestScore = s; best = p; }
    }
    return best;
}

// ─── calcSeparationForce ──────────────────────────────────────────────────────

Vec3 Npc::calcSeparationForce(Room& room) const {
    std::vector<Vec3> nearby;
    room.findNearbyNpcPositions(position_, separationRadius_, id_, nearby);

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
        force += away.normalized() * strength;
    }
    return force;
}

// ─── isOutsideActivityZone ───────────────────────────────────────────────────

bool Npc::isOutsideActivityZone() const {
    return Vec3::distance(position_, activityZoneCenter_) > activityZoneRadius_;
}

// ─── isOvercrowded ────────────────────────────────────────────────────────────

bool Npc::isOvercrowded(Room& room) const {
    std::vector<Vec3> nearby;
    room.findNearbyNpcPositions(position_, separationRadius_ * 0.7f, id_, nearby);
    return static_cast<int>(nearby.size()) >= overlapThreshold_;
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
