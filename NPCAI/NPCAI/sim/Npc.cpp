#include "Npc.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <sstream>
#include <iomanip>
#include <cstdio>

namespace sim {

Npc::Npc(const std::string& name, const Vec3& pos, const NpcConfig& cfg)
    : Actor(name, pos, cfg.maxHp)
    , spawnPos_(pos)
    , detectionRange_(cfg.detectionRange)
    , attackRange_(cfg.attackRange)
    , chaseRange_(cfg.chaseRange)
    , moveSpeed_(cfg.moveSpeed)
    , attackDamage_(cfg.attackDamage)
    , attackCooldown_(cfg.attackCooldown)
{}

// ─── Main update dispatch ───────────────────────────────────────────────────

void Npc::update(float dt, Room& room) {
    // If alive_ was cleared by takeDamage(), drive to Dead state
    if (!alive_) {
        updateDead();
        return;
    }

    switch (state_) {
        case NpcState::Idle:   updateIdle  (dt, room); break;
        case NpcState::Chase:  updateChase (dt, room); break;
        case NpcState::Attack: updateAttack(dt, room); break;
        case NpcState::Return: updateReturn(dt, room); break;
        case NpcState::Dead:   /* terminal */          break;
    }
}

// ─── Transition helper ──────────────────────────────────────────────────────

void Npc::transitionTo(NpcState newState, const std::string& reason) {
    if (state_ == newState) return;
    Logger::get().logTransition(name_,
        npcStateStr(state_), npcStateStr(newState), reason);
    state_ = newState;
}

// ─── Target resolution ──────────────────────────────────────────────────────

Actor* Npc::resolveTarget(Room& room) const {
    if (targetId_ == 0) return nullptr;
    Actor* a = room.findActorById(targetId_);
    if (!a || !a->isAlive()) return nullptr;
    return a;
}

// ─── Idle ───────────────────────────────────────────────────────────────────
//   Scan for the nearest living player within detectionRange.
//   If found → Chase.

void Npc::updateIdle(float /*dt*/, Room& room) {
    Player* p = room.findNearestLivingPlayer(position_, detectionRange_);
    if (!p) return;

    targetId_ = p->getId();

    char buf[128];
    std::snprintf(buf, sizeof(buf), "target=%s dist=%.1f",
        p->getName().c_str(),
        Vec3::distance(position_, p->getPosition()));
    transitionTo(NpcState::Chase, buf);
}

// ─── Chase ──────────────────────────────────────────────────────────────────
//   Move toward target.
//   → Attack  if within attackRange
//   → Return  if target gone / dist > chaseRange

void Npc::updateChase(float dt, Room& room) {
    Actor* target = resolveTarget(room);

    if (!target) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "target lost or dead");
        return;
    }

    float dist = Vec3::distance(position_, target->getPosition());

    if (dist > chaseRange_) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "target exceeded leash range");
        return;
    }

    if (dist <= attackRange_) {
        transitionTo(NpcState::Attack, "in attack range");
        return;
    }

    // Step toward target
    Vec3 dir = (target->getPosition() - position_).normalized();
    facing_   = dir;
    position_ += dir * (moveSpeed_ * dt);
}

// ─── Attack ─────────────────────────────────────────────────────────────────
//   Swing on cooldown.
//   → Chase   if gap opened (dist > attackRange * 1.5)
//   → Return  if target gone

void Npc::updateAttack(float dt, Room& room) {
    Actor* target = resolveTarget(room);

    if (!target) {
        targetId_ = 0;
        transitionTo(NpcState::Return, "target lost or dead during attack");
        return;
    }

    float dist = Vec3::distance(position_, target->getPosition());

    if (dist > attackRange_ * 1.5f) {
        transitionTo(NpcState::Chase, "gap opened, re-chasing");
        return;
    }

    attackTimer_ -= dt;
    if (attackTimer_ <= 0.f) {
        attackTimer_ = attackCooldown_;

        target->takeDamage(attackDamage_);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "hit %s for %.1f  (target hp=%.1f)",
            target->getName().c_str(),
            attackDamage_,
            target->getHp());
        Logger::get().log("NPC:" + name_, buf);

        if (!target->isAlive()) {
            targetId_ = 0;
            transitionTo(NpcState::Return, "target killed");
        }
    }
}

// ─── Return ─────────────────────────────────────────────────────────────────
//   Walk back to spawnPos.
//   → Chase  if a new player enters detectionRange while returning
//   → Idle   once back at spawn

void Npc::updateReturn(float dt, Room& room) {
    // Opportunistic re-aggro during return
    Player* p = room.findNearestLivingPlayer(position_, detectionRange_);
    if (p) {
        targetId_ = p->getId();
        char buf[128];
        std::snprintf(buf, sizeof(buf), "re-aggro on %s dist=%.1f",
            p->getName().c_str(),
            Vec3::distance(position_, p->getPosition()));
        transitionTo(NpcState::Chase, buf);
        return;
    }

    float distToSpawn = Vec3::distance(position_, spawnPos_);
    if (distToSpawn < 0.3f) {
        position_ = spawnPos_;
        transitionTo(NpcState::Idle, "reached spawn point");
        return;
    }

    Vec3 dir = (spawnPos_ - position_).normalized();
    facing_  = dir;
    position_ += dir * (moveSpeed_ * dt);
}

// ─── Dead ────────────────────────────────────────────────────────────────────
//   Terminal state. Ensure we only log the transition once.

void Npc::updateDead() {
    if (state_ != NpcState::Dead) {
        transitionTo(NpcState::Dead, "hp reached 0");
    }
    // Respawn timer logic goes here in v2
}

// ─── Dump ─────────────────────────────────────────────────────────────────────

std::string Npc::dump() const {
    std::ostringstream oss;
    oss << Actor::dump()
        << "  state=" << std::left << std::setw(7) << npcStateStr(state_)
        << " target=" << (targetId_ ? std::to_string(targetId_) : "none");
    return oss.str();
}

} // namespace sim
