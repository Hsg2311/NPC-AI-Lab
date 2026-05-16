#pragma once
#include "Actor.hpp"

namespace sim {

struct PlayerAttackConfig {
    float damage      = 25.f;
    float range       = 5.5f;
    float windupTime  = 0.2f;
    float recoverTime = 0.3f;
};

class Player : public Actor {
public:
    Player(const std::string& name, const Vec3& pos,
           float maxHp = 100.f, float moveSpeed = 5.f,
           PlayerAttackConfig attackCfg = {});

    void update(float dt, Room& room) override;
    const char* typeName() const override { return "Player"; }

    void setMoveTarget(const Vec3& target) { moveTarget_ = target; }
    Vec3 getMoveTarget() const { return moveTarget_; }
    void applyKnockback(const Vec3& direction, float speed, float duration);

    // 공격 요청 — 현재 공격 중이거나 사망이면 무시
    void requestAttack();

    // ── 공격 상태 접근자 (스냅샷용) ──────────────────────────────────────────
    int   getAttackState()    const { return static_cast<int>(attackState_); }
    float getAttackProgress() const;
    float getAttackRange()    const { return attackCfg_.range; }

private:
    // ── 상태 ─────────────────────────────────────────────────────────────────
    enum class AttackState { None, Windup, Recover };

    Vec3               moveTarget_;
    float              moveSpeed_;
    PlayerAttackConfig attackCfg_;
    AttackState        attackState_{ AttackState::None };
    float              attackTimer_{ 0.f };
    Vec3               knockbackDir_{};
    float              knockbackSpeed_{ 0.f };
    float              knockbackTimer_{ 0.f };
    float              postKnockbackMoveLockTimer_{ 0.f };

    void updateAttack(float dt, Room& room);
};

} // namespace sim
