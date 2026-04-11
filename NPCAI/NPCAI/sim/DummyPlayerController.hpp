#pragma once
#include "Vec3.hpp"
#include <vector>
#include <cstdint>

namespace sim {

class Room; // forward declaration

// Controls dummy players by assigning them waypoint routes.
// Call addControl() once per player, then update() each tick.
class DummyPlayerController {
public:
    struct PlayerControl {
        uint32_t playerId = 0;
        std::vector<Vec3> waypoints;
        int currentWaypoint = 0;
        bool loop = true;   // restart from waypoint[0] at end
    };

    void addControl(uint32_t playerId, std::vector<Vec3> waypoints, bool loop = true);
    void update(float dt, Room& room);

private:
    std::vector<PlayerControl> controls_{};
};

} // namespace sim
