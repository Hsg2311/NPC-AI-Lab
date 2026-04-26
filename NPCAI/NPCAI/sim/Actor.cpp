#include "Actor.hpp"
#include "Logger.hpp"
#include <sstream>
#include <iomanip>

namespace sim {

uint32_t Actor::nextId_ = 1;

Actor::Actor(const std::string& name, const Vec3& pos, float maxHp)
    : id_(nextId_++), name_(name), position_(pos), hp_(maxHp), maxHp_(maxHp)
{}

void Actor::takeDamage(float dmg) {
    if (!alive_) return;

    hp_ -= dmg;
    if (hp_ <= 0.f) {
        hp_    = 0.f;
        alive_ = false;
        Logger::get().log(std::string(typeName()) + ":" + name_, "DEAD (hp depleted)");
    }
}

std::string Actor::dump() const {
    std::ostringstream oss;
    char hpBuf[64];
    std::snprintf(hpBuf, sizeof(hpBuf), "hp=%5.1f/%-5.1f", hp_, maxHp_);
    oss << std::left << std::setw(12) << name_
        << " " << hpBuf
        << " pos=" << position_.toString()
        << (alive_ ? "" : " [DEAD]");
    return oss.str();
}

} // namespace sim
