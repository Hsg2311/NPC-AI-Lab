#include "Npc.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace sim {

// ─── File-scope helpers ───────────────────────────────────────────────────────

static const char* npcStateStr(NpcState s) {
    switch (s) {
        case NpcState::Idle:           return "Idle";
        case NpcState::Chase:          return "Chase";
        case NpcState::AttackWindup:   return "AttackWindup";
        case NpcState::AttackRecover:  return "AttackRecover";
        case NpcState::Return:         return "Return";
        case NpcState::Reposition:     return "Reposition";
        case NpcState::Regroup:        return "Regroup";
        case NpcState::Confused:       return "Confused";
        case NpcState::MoveToSlot:     return "MoveToSlot";
        case NpcState::Retreat:        return "Retreat";
        case NpcState::Dead:           return "Dead";
    }
    return "?";
}

// ─── Constructor ──────────────────────────────────────────────────────────────

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
    , overlapThreshold_(cfg.overlapThreshold)
{}

// ─── Accessors ────────────────────────────────────────────────────────────────

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

// ─── applyCommand ─────────────────────────────────────────────────────────────
// Called by Squad::update() BEFORE this NPC's own update() runs each tick.
// Directly modifies state or sets parameters consumed by per-state updates.

void Npc::applyCommand(const NpcCommand& cmd) {
    switch (cmd.type) {
        case NpcCommandType::EngageTarget:
            squadTargetId_ = cmd.targetId;
            if (cmd.targetId == 0) return;
            // Interrupt only if not already in active combat with this target
            if (targetId_ != cmd.targetId ||
                (state_ != NpcState::Chase      &&
                 state_ != NpcState::AttackWindup  &&
                 state_ != NpcState::AttackRecover &&
                 state_ != NpcState::Reposition)) {
                targetId_ = cmd.targetId;
                transitionTo(NpcState::Chase, "EngageTarget cmd");
            }
            break;

        case NpcCommandType::HoldSlot:
            squadTargetId_ = 0;
            targetId_      = 0;
            transitionTo(NpcState::Idle, "HoldSlot cmd");
            break;

        case NpcCommandType::Retreat:
            retreatDestination_ = cmd.destination;
            commandSpeedMult_   = cmd.speedMultiplier;
            squadTargetId_      = 0;
            targetId_           = 0;
            transitionTo(NpcState::Retreat, "Retreat cmd");
            break;

        case NpcCommandType::Confused:
            targetId_      = 0;
            squadTargetId_ = 0;
            confusedTimer_ = 0.f;
            // Deterministic direction: golden-angle seed from NPC id
            confusedDir_ = Vec3{
                std::cosf(static_cast<float>(id_) * 1.618f),
                0.f,
                std::sinf(static_cast<float>(id_) * 1.618f)
            };
            transitionTo(NpcState::Confused, "leader died");
            break;

        case NpcCommandType::Idle:
            squadTargetId_ = 0;
            targetId_      = 0;
            transitionTo(NpcState::Idle, "Idle cmd");
            break;
    }
}

// ─── buildReport ──────────────────────────────────────────────────────────────

NpcReport Npc::buildReport() const {
    NpcReport r;
    r.npcId         = id_;
    r.state         = static_cast<int>(state_);
    r.position      = position_;
    r.currentTarget = targetId_;
    r.hpRatio       = (maxHp_ > 0.f) ? (hp_ / maxHp_) : 0.f;
    r.alive         = alive_;
    r.isLeader      = isLeader_;
    return r;
}

// ─── Main update dispatch ─────────────────────────────────────────────────────

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
        case NpcState::Regroup:        updateRegroup       (dt, room); break;
        case NpcState::Confused:       updateConfused      (dt, room); break;
        case NpcState::MoveToSlot:     updateMoveToSlot    (dt, room); break;
        case NpcState::Retreat:        updateRetreat       (dt, room); break;
        case NpcState::Dead:           /* terminal */                   break;
    }
}

// ─── transitionTo ─────────────────────────────────────────────────────────────

void Npc::transitionTo(NpcState next, const char* reason) {
    if (state_ == next) return;
    Logger::get().logTransition(name_, npcStateStr(state_), npcStateStr(next), reason);
    if (next == NpcState::AttackWindup)  windupTimer_   = 0.f;
    if (next == NpcState::AttackRecover) recoverTimer_  = 0.f;
    if (next == NpcState::Reposition)    repositionTimer_ = 0.f;
    state_ = next;
}

// ─── Idle ─────────────────────────────────────────────────────────────────────
// Squad members: wait for EngageTarget command — no autonomous aggro.
// Standalone NPCs (squadId_ == -1): legacy score-based self-targeting.

void Npc::updateIdle(float /*dt*/, Room& room) {
    // Squad member: EngageTarget command handled via applyCommand().
    // Legacy squad-target override still supported for backward compat.
    if (squadId_ != -1) {
        if (squadTargetId_ != 0) {
            Actor* t = room.findActorById(squadTargetId_);
            if (t && t->isAlive()) {
                targetId_ = squadTargetId_;
                char buf[80];
                std::snprintf(buf, sizeof(buf), "squad-override target=%s",
                    t->getName().c_str());
                transitionTo(NpcState::Chase, buf);
            }
        }
        return;  // squad member: no autonomous target selection
    }

    // Standalone NPC: legacy self-targeting
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
        // Squad members keep assigned target; standalone NPCs re-evaluate
        if (squadId_ == -1) {
            Player* newBest = selectBestTarget(room);
            if (newBest && newBest->getId() != targetId_) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "retarget -> %s", newBest->getName().c_str());
                Logger::get().log("NPC:" + name_, buf);
                targetId_ = newBest->getId();
            }
        }
    }

    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        // Squad member: go Idle and wait for new command; standalone: Return
        transitionTo((squadId_ != -1) ? NpcState::Idle : NpcState::Return,
                     "target lost");
        return;
    }
    if (isTooFarFromHome()) {
        targetId_ = 0;
        leashBreak_ = true;
        transitionTo((squadId_ != -1) ? NpcState::Idle : NpcState::Return,
                     "too far from home");
        return;
    }

    float dist = Vec3::distance(position_, target->getPosition());
    if (dist > chaseRange_) {
        targetId_ = 0;
        transitionTo((squadId_ != -1) ? NpcState::Idle : NpcState::Return,
                     "target beyond leash range");
        return;
    }
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
        transitionTo((squadId_ != -1) ? NpcState::Idle : NpcState::Return,
                     "target lost during windup");
        return;
    }
    if (isTooFarFromHome()) {
        targetId_ = 0;
        leashBreak_ = true;
        transitionTo((squadId_ != -1) ? NpcState::Idle : NpcState::Return,
                     "too far from home");
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
                transitionTo((squadId_ != -1) ? NpcState::Idle : NpcState::Return,
                             "target killed");
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
        transitionTo((squadId_ != -1) ? NpcState::Idle : NpcState::Return,
                     "target lost during recover");
        return;
    }
    if (isTooFarFromHome()) {
        targetId_ = 0;
        leashBreak_ = true;
        transitionTo((squadId_ != -1) ? NpcState::Idle : NpcState::Return,
                     "too far from home");
        return;
    }

    Vec3 sep = calcSeparationForce(room);
    if (sep.length() > 0.1f)
        position_ += sep * (separationWeight_ * 0.3f * moveSpeed_ * dt);

    recoverTimer_ += dt;
    if (recoverTimer_ >= attackRecoverTime_) {
        if (isOvercrowded(room)) {
            Vec3 toTarget  = (target->getPosition() - position_).normalized();
            // Perpendicular sidestep direction: odd id → left, even id → right
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
// Used by standalone NPCs (squadId_ == -1) heading back to spawn.
// Squad members reach Idle instead (they wait for the next EngageTarget).

void Npc::updateReturn(float dt, Room& room) {
    if (canReAggroOnReturn_ && squadId_ == -1 && !leashBreak_) {
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
        position_   = spawnPos_;
        leashBreak_ = false;
        transitionTo(NpcState::Idle, "reached home");
        return;
    }

    Vec3 homeDir = (spawnPos_ - position_).normalized();
    Vec3 sep     = calcSeparationForce(room);
    Vec3 moveDir = (homeDir + sep * (separationWeight_ * 0.25f)).normalized();
    facing_   = moveDir;
    position_ += moveDir * (moveSpeed_ * dt);
}

// ─── Regroup ──────────────────────────────────────────────────────────────────
// Legacy: standalone NPC rally behavior. Squad NPCs use Idle instead.

void Npc::updateRegroup(float dt, Room& room) {
    if (squadTargetId_ == 0) {
        transitionTo(NpcState::Return, "squad disengaged during regroup");
        return;
    }

    Actor* target = room.findActorById(squadTargetId_);
    if (!target || !target->isAlive()) return;

    if (Vec3::distance(position_, target->getPosition()) <= chaseRange_) {
        targetId_ = squadTargetId_;
        transitionTo(NpcState::Chase, "regroup complete");
        return;
    }

    Vec3 dir     = (target->getPosition() - position_).normalized();
    Vec3 sep     = calcSeparationForce(room);
    Vec3 moveDir = (dir + sep * separationWeight_).normalized();
    facing_   = moveDir;
    position_ += moveDir * (moveSpeed_ * dt);
}

// ─── Reposition ───────────────────────────────────────────────────────────────
// Sidestep away from crowded position: blend toward target + perpendicular dir.
// Exits when no longer overcrowded, or after REPOSITION_TIMEOUT seconds.

void Npc::updateReposition(float dt, Room& room) {
    Actor* target = resolveTarget(room);
    if (!target) {
        targetId_ = 0;
        transitionTo((squadId_ != -1) ? NpcState::Idle : NpcState::Return,
                     "target lost during reposition");
        return;
    }
    if (isTooFarFromHome()) {
        targetId_ = 0;
        leashBreak_ = true;
        transitionTo((squadId_ != -1) ? NpcState::Idle : NpcState::Return,
                     "too far from home");
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

// ─── Confused ─────────────────────────────────────────────────────────────────
// Triggered by NpcCommand::Confused (Squad leader death).
// Randomly wanders for CONFUSION_DURATION, then transitions to Idle.

void Npc::updateConfused(float dt, Room& /*room*/) {
    confusedTimer_ += dt;

    // Oscillate direction slowly — "two-step" pattern makes it look frantic
    float angle = std::sinf(confusedTimer_ * 2.5f) * 0.9f;
    float ca = std::cosf(angle);
    float sa = std::sinf(angle);
    Vec3 dir = {
        ca * confusedDir_.x - sa * confusedDir_.z,
        0.f,
        sa * confusedDir_.x + ca * confusedDir_.z
    };

    position_ += dir * (moveSpeed_ * 0.5f * dt);
    facing_    = dir;

    if (confusedTimer_ >= CONFUSION_DURATION)
        transitionTo(NpcState::Idle, "confusion ended");
}

// ─── MoveToSlot ───────────────────────────────────────────────────────────────
// Move to assigned formation slot position (future formation use).

void Npc::updateMoveToSlot(float dt, Room& room) {
    float distToSlot = Vec3::distance(position_, slotDestination_);
    if (distToSlot < 0.5f) {
        transitionTo(NpcState::Idle, "slot reached");
        return;
    }

    Vec3 dir     = (slotDestination_ - position_).normalized();
    Vec3 sep     = calcSeparationForce(room);
    Vec3 moveDir = (dir + sep * separationWeight_).normalized();
    float speed  = moveSpeed_ * commandSpeedMult_;
    facing_   = moveDir;
    position_ += moveDir * (speed * dt);
}

// ─── Retreat ──────────────────────────────────────────────────────────────────
// Command-driven retreat toward retreatDestination_.
// Used by Broken squads (slow speed) and Platoon Retreat orders (full speed).

void Npc::updateRetreat(float dt, Room& room) {
    float dist = Vec3::distance(position_, retreatDestination_);
    if (dist < 0.3f) {
        position_ = retreatDestination_;
        transitionTo(NpcState::Idle, "retreat destination reached");
        return;
    }

    Vec3 dir     = (retreatDestination_ - position_).normalized();
    Vec3 sep     = calcSeparationForce(room);
    Vec3 moveDir = (dir + sep * (separationWeight_ * 0.25f)).normalized();
    float speed  = moveSpeed_ * commandSpeedMult_;
    facing_   = moveDir;
    position_ += moveDir * (speed * dt);
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
    if (dist > chaseRange_) return -1000.f;

    float score = (1.f - dist / chaseRange_) * 50.f;
    if (p->getId() == targetId_)      score += 20.f;
    if (dist <= attackRange_)         score += 15.f;
    if (p->getId() == squadTargetId_) score += 40.f;

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

// ─── isTooFarFromHome ────────────────────────────────────────────────────────

bool Npc::isTooFarFromHome() const {
    return Vec3::distance(position_, spawnPos_) > maxChaseDistance_;
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
