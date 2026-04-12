#include "Npc.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace sim {

// ---- File-scope helpers ------------------------------------------------------
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

// ---- Constructor -------------------------------------------------------------
Npc::Npc(const std::string& name, const Vec3& pos, const NpcConfig& cfg)
    : Actor(name, pos, cfg.maxHp)
    , spawnPos_(pos)
    , detectionRange_(cfg.detectionRange)
    , attackRange_(cfg.attackRange)
    , chaseRange_(cfg.chaseRange)
    , maxChaseDistance_(cfg.maxChaseDistance)
    , moveSpeed_(cfg.moveSpeed)
    , attackDamage_(cfg.attackDamage)
    , attackWindupTime_(cfg.attackWindupTime)
    , attackRecoverTime_(cfg.attackRecoverTime)
    , separationRadius_(cfg.separationRadius)
    , separationWeight_(cfg.separationWeight)
    , canReAggroOnReturn_(cfg.canReAggroOnReturn)
    , repositionRadius_(cfg.repositionRadius)
    , overlapThreshold_(cfg.overlapThreshold)
{}

// ---- Accessors ---------------------------------------------------------------
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

// ---- Main update dispatch ----------------------------------------------------
void Npc::update(float dt, Room& room) {
    if (!alive_) {
        updateDead();
        return;
    }
    switch (state_) {
        case NpcState::Idle:           updateIdle         (dt, room); break;
        case NpcState::Chase:          updateChase        (dt, room); break;
        case NpcState::AttackWindup:   updateAttackWindup (dt, room); break;
        case NpcState::AttackRecover:  updateAttackRecover(dt, room); break;
        case NpcState::Return:         updateReturn       (dt, room); break;
        case NpcState::Reposition:     updateReposition   (dt, room); break;
        case NpcState::Dead:           /* terminal */                  break;
    }
}

// ---- transitionTo ------------------------------------------------------------
void Npc::transitionTo(NpcState next, const char* reason) {
    if (state_ == next) return;
    Logger::get().logTransition(name_, npcStateStr(state_), npcStateStr(next), reason);
    // Reset entry timers
    if (next == NpcState::AttackWindup)  windupTimer_  = 0.f;
    if (next == NpcState::AttackRecover) recoverTimer_ = 0.f;
    state_ = next;
}

// ---- Idle --------------------------------------------------------------------
// Score-based target selection; only consider players within detectionRange.
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

// ---- Chase -------------------------------------------------------------------
// Pursue + separation + periodic target re-evaluation.
void Npc::updateChase(float dt, Room& room) {
    // Periodic target re-evaluation
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
    if (isTooFarFromHome()) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "too far from home");
        return;
    }

    float dist = Vec3::distance(position_, target->getPosition());
    if (dist > chaseRange_) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "target beyond leash range");
        return;
    }
    if (dist <= attackRange_) {
        transitionTo(NpcState::AttackWindup, "in attack range");
        return;
    }

    // Chase direction + separation composite
    Vec3 chaseDir = (target->getPosition() - position_).normalized();
    Vec3 sepForce = calcSeparationForce(room);
    Vec3 moveDir  = (chaseDir + sepForce * separationWeight_).normalized();
    facing_   = moveDir;
    position_ += moveDir * (moveSpeed_ * dt);
}

// ---- AttackWindup ------------------------------------------------------------
// No position movement; separation adjusts facing only.
void Npc::updateAttackWindup(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "target lost during windup");
        return;
    }
    if (isTooFarFromHome()) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "too far from home");
        return;
    }

    float dist = Vec3::distance(position_, target->getPosition());
    if (dist > attackRange_ * 1.2f) {
        transitionTo(NpcState::Chase, "target escaped during windup");
        return;
    }

    // Separation: update facing only, no position change
    Vec3 sep = calcSeparationForce(room);
    if (sep.length() > 0.1f) {
        facing_ = (facing_ + sep * 0.3f).normalized();
    }

    windupTimer_ += dt;
    if (windupTimer_ >= attackWindupTime_) {
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
}

// ---- AttackRecover -----------------------------------------------------------
// Weak separation drift allowed during recovery.
void Npc::updateAttackRecover(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "target lost during recover");
        return;
    }
    if (isTooFarFromHome()) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "too far from home");
        return;
    }

    // Gentle separation drift during recover
    Vec3 sep = calcSeparationForce(room);
    if (sep.length() > 0.1f) {
        position_ += sep * (separationWeight_ * 0.3f * moveSpeed_ * dt);
    }

    recoverTimer_ += dt;
    if (recoverTimer_ >= attackRecoverTime_) {
        if (isOvercrowded(room)) {
            repositionTarget_    = calcRepositionTarget(target->getPosition());
            hasRepositionTarget_ = true;
            transitionTo(NpcState::Reposition, "overcrowded after recover");
            return;
        }
        float dist = Vec3::distance(position_, target->getPosition());
        if (dist <= attackRange_) {
            transitionTo(NpcState::AttackWindup, "recover done, in range");
        } else {
            transitionTo(NpcState::Chase, "recover done, out of range");
        }
    }
}

// ---- Return ------------------------------------------------------------------
void Npc::updateReturn(float dt, Room& room) {
    if (canReAggroOnReturn_) {
        // Re-aggro only within detectionRange (not the wider chaseRange)
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
    // Separation is weaker during return
    Vec3 moveDir = (homeDir + sep * (separationWeight_ * 0.25f)).normalized();
    facing_   = moveDir;
    position_ += moveDir * (moveSpeed_ * dt);
}

// ---- Reposition --------------------------------------------------------------
void Npc::updateReposition(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        hasRepositionTarget_ = false;
        transitionTo(NpcState::Return, "target lost during reposition");
        return;
    }
    if (isTooFarFromHome()) {
        targetId_ = 0;
        hasRepositionTarget_ = false;
        transitionTo(NpcState::Return, "too far from home");
        return;
    }

    float distToSlot = Vec3::distance(position_, repositionTarget_);
    if (distToSlot < 0.4f) {
        hasRepositionTarget_ = false;
        float distToTarget = Vec3::distance(position_, target->getPosition());
        if (distToTarget <= attackRange_) {
            transitionTo(NpcState::AttackWindup, "reposition done, in range");
        } else {
            transitionTo(NpcState::Chase, "reposition done, chasing");
        }
        return;
    }

    Vec3 dir     = (repositionTarget_ - position_).normalized();
    Vec3 sep     = calcSeparationForce(room);
    Vec3 moveDir = (dir + sep * separationWeight_).normalized();
    facing_   = moveDir;
    position_ += moveDir * (moveSpeed_ * dt);
}

// ---- Dead --------------------------------------------------------------------
void Npc::updateDead() {
    if (state_ != NpcState::Dead) {
        transitionTo(NpcState::Dead, "hp reached 0");
    }
    // Respawn timer logic goes here in v3
}

// ---- resolveTarget -----------------------------------------------------------
Actor* Npc::resolveTarget(Room& room) const {
    if (targetId_ == 0) return nullptr;
    Actor* a = room.findActorById(targetId_);
    if (!a || !a->isAlive()) return nullptr;
    return a;
}

// ---- evaluateTargetScore -----------------------------------------------------
float Npc::evaluateTargetScore(const Player* p, Room& room) const {
    float dist = Vec3::distance(position_, p->getPosition());
    if (dist > chaseRange_) return -1000.f;

    float score = (1.f - dist / chaseRange_) * 50.f;   // distance score (max 50)
    if (p->getId() == targetId_) score += 20.f;          // current-target hysteresis
    if (dist <= attackRange_)    score += 15.f;          // in attack range bonus

    // Aggro distribution penalty (Chase/Windup/Recover/Reposition)
    int aggro = room.countNpcsTargeting(p->getId());
    if (p->getId() == targetId_ && aggro > 0) --aggro;  // exclude self
    score -= static_cast<float>(aggro) * 8.f;

    return score;
}

// ---- selectBestTarget --------------------------------------------------------
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

// ---- calcSeparationForce -----------------------------------------------------
Vec3 Npc::calcSeparationForce(Room& room) const {
    std::vector<Vec3> nearby;
    room.findNearbyNpcPositions(position_, separationRadius_, id_, nearby);

    Vec3 force{ 0.f, 0.f, 0.f };
    for (const Vec3& op : nearby) {
        Vec3  away = position_ - op;
        float d    = away.length();
        if (d < 1e-4f) {
            // Perfect overlap: deterministic push using id-based angle
            float a = static_cast<float>(id_) * 1.2f;
            force += Vec3{ std::cosf(a), 0.f, std::sinf(a) };
            continue;
        }
        float strength = 1.f - (d / separationRadius_);
        force += away.normalized() * strength;
    }
    return force;
}

// ---- calcRepositionTarget ----------------------------------------------------
// Golden angle (~137.5 deg) distributes IDs evenly around the target.
Vec3 Npc::calcRepositionTarget(const Vec3& targetPos) const {
    float angle  = static_cast<float>(id_) * 2.399963f;
    float radius = repositionRadius_;
    return Vec3{
        targetPos.x + std::cosf(angle) * radius,
        0.f,
        targetPos.z + std::sinf(angle) * radius
    };
}

// ---- isTooFarFromHome --------------------------------------------------------
bool Npc::isTooFarFromHome() const {
    return Vec3::distance(position_, spawnPos_) > maxChaseDistance_;
}

// ---- isOvercrowded -----------------------------------------------------------
bool Npc::isOvercrowded(Room& room) const {
    std::vector<Vec3> nearby;
    room.findNearbyNpcPositions(position_, separationRadius_ * 0.7f, id_, nearby);
    return static_cast<int>(nearby.size()) >= overlapThreshold_;
}

// ---- dump --------------------------------------------------------------------
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
