#pragma once
#include "Vec3.hpp"
#include <string>
#include <cstdint>

namespace sim {

class Room;

class Actor {
public:
    Actor(const std::string& name, const Vec3& pos, float maxHp);
    virtual ~Actor() = default;

    // 핵심 인터페이스
    virtual void update(float dt, Room& room) = 0;
    virtual std::string dump() const;
    virtual const char* typeName() const = 0;

    // 피해 / 사망
    void takeDamage(float dmg);

    // 접근자
    uint32_t getId() const { return id_; }
    const std::string& getName() const { return name_; }
    Vec3 getPosition() const { return position_; }
    void setPosition(const Vec3& p) { position_ = p; }
    float getHp() const { return hp_; }
    float getMaxHp() const { return maxHp_; }
    bool isAlive() const { return alive_; }
    Vec3 getFacing() const { return facing_; }

protected:
    uint32_t id_;
    std::string name_;
    Vec3 position_;
    Vec3 facing_ = { 1.f, 0.f, 0.f };  // XZ 평면 단위 방향 벡터
    float hp_;
    float maxHp_;
    bool alive_{ true };

private:
    static uint32_t nextId_;
};

} // namespace sim
