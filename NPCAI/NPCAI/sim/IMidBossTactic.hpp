#pragma once

namespace sim {

class PlatoonLeader;
class Room;

class IMidBossTactic {
public:
    virtual ~IMidBossTactic() = default;

    virtual const char* name() const = 0;
    virtual void update(float dt, Room& room, PlatoonLeader& leader) = 0;
    virtual void onLeaderDead(Room& room, PlatoonLeader& leader) = 0;
};

} // namespace sim
