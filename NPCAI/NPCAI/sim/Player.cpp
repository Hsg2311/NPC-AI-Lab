#include "Player.hpp"
#include "Room.hpp"

namespace sim {

Player::Player(const std::string& name, const Vec3& pos, float maxHp, float moveSpeed)
    : Actor(name, pos, maxHp), moveTarget_(pos), moveSpeed_(moveSpeed)
{}

void Player::update(float dt, Room& /*room*/) {
    if (!alive_) return;

    Vec3  dir  = moveTarget_ - position_;
    float dist = dir.length();
    if (dist < 0.05f) return;

    facing_ = dir.normalized();

    float step = moveSpeed_ * dt;
    if (step >= dist) {
        position_ = moveTarget_;
    } else {
        position_ += facing_ * step;
    }
}

} // namespace sim
