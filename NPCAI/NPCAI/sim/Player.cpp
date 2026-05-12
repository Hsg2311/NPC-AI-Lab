#include "Player.hpp"
#include "Room.hpp"
#include "Logger.hpp"
#include <string>

namespace sim {

Player::Player(const std::string& name, const Vec3& pos,
               float maxHp, float moveSpeed, PlayerAttackConfig attackCfg)
    : Actor(name, pos, maxHp)
    , moveTarget_(pos)
    , moveSpeed_(moveSpeed)
    , attackCfg_(attackCfg)
{}

// ─── requestAttack ────────────────────────────────────────────────────────────

void Player::requestAttack() {
    if (!alive_ || attackState_ != AttackState::None) return;
    attackState_ = AttackState::Windup;
    attackTimer_ = 0.f;
}

// ─── getAttackProgress ───────────────────────────────────────────────────────

float Player::getAttackProgress() const {
    if (attackState_ == AttackState::Windup && attackCfg_.windupTime > 0.f)
        return attackTimer_ / attackCfg_.windupTime;
    if (attackState_ == AttackState::Recover && attackCfg_.recoverTime > 0.f)
        return attackTimer_ / attackCfg_.recoverTime;
    return 0.f;
}

// ─── update ───────────────────────────────────────────────────────────────────

void Player::update(float dt, Room& room) {
    if (!alive_) return;

    Vec3  dir  = moveTarget_ - position_;
    float dist = dir.length();
    if (dist >= 0.05f) {
        Vec3 moveDir = dir.normalized();
        float step = moveSpeed_ * dt;
        Vec3 desiredMove = moveDir * ((step >= dist) ? dist : step);
        Vec3 adjustedMove = room.adjustPlayerMoveForNpcSoftBlock(position_, desiredMove, dt);
        if (adjustedMove.lengthSq() > 1e-6f) {
            facing_ = adjustedMove.normalized();
            position_ += adjustedMove;
        } else {
            facing_ = moveDir;
        }
    }

    updateAttack(dt, room);
}

// ─── updateAttack ─────────────────────────────────────────────────────────────

void Player::updateAttack(float dt, Room& room) {
    if (!alive_) {
        attackState_ = AttackState::None;
        return;
    }

    switch (attackState_) {
        case AttackState::None: return;

        case AttackState::Windup:
            attackTimer_ += dt;
            if (attackTimer_ >= attackCfg_.windupTime) {
                int hits = room.applyDamageToActorsInRange(
                    position_, attackCfg_.range, attackCfg_.damage);
                Logger::get().log("Player:" + name_,
                    "공격 — " + std::to_string(hits) + "명 피격");
                attackState_ = AttackState::Recover;
                attackTimer_ = 0.f;
            }
            break;

        case AttackState::Recover:
            attackTimer_ += dt;
            if (attackTimer_ >= attackCfg_.recoverTime) {
                attackState_ = AttackState::None;
                attackTimer_ = 0.f;
            }
            break;
    }
}

} // namespace sim
