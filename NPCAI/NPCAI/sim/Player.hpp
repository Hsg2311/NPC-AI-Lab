#pragma once
#include "Actor.hpp"

namespace sim {

class Player : public Actor {
public:
    Player(const std::string& name, const Vec3& pos, float maxHp = 100.f, float moveSpeed = 5.f);

    void update(float dt, Room& room) override;
    const char* typeName() const override { return "Player"; }

    void setMoveTarget(const Vec3& target) { moveTarget_ = target; }
    Vec3 getMoveTarget() const { return moveTarget_; }

private:
    Vec3 moveTarget_;
    float moveSpeed_;
};

} // namespace sim
