#include "Actor.hpp"
#include "Logger.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <vector>

namespace sim {

uint32_t Actor::nextId_ = 1;

Actor::Actor(const std::string& name, const Vec3& pos, float maxHp)
    : id_(nextId_++), name_(name), position_(pos), hp_(maxHp), maxHp_(maxHp)
{}

void Actor::takeDamage(float dmg) {
    if (!alive_) return;

    hp_ -= dmg * damageTakenMultiplier_;
    if (hp_ <= 0.f) {
        hp_    = 0.f;
        alive_ = false;
        Logger::get().log(std::string(typeName()) + ":" + name_, "DEAD (hp depleted)");
    }
}

// ─── calcSeparationForce ──────────────────────────────────────────────────────

Vec3 Actor::calcSeparationForce(float separationRadius,
                                 const std::vector<Vec3>& nearby) const {
    Vec3 force{ 0.f, 0.f, 0.f };
    for (const Vec3& op : nearby) {
        Vec3  away = position_ - op;
        float d    = away.length();
        if (d < 1e-4f) {
            float a = static_cast<float>(id_) * 1.2f;
            force += Vec3{ std::cosf(a), 0.f, std::sinf(a) };
            continue;
        }
        float strength = 1.f - (d / separationRadius);
        force += (away / d) * strength;
    }
    return force;
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
