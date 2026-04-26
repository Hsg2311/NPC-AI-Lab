#pragma once
#include "Vec3.hpp"
#include <vector>
#include <cstdint>

namespace sim {

class Room; // forward declaration

// 웨이포인트 경로를 할당해 더미 플레이어를 조종한다.
// 플레이어당 addControl()을 한 번 호출하고 매 틱 update()를 호출한다.
class DummyPlayerController {
public:
    struct PlayerControl {
        uint32_t playerId = 0;
        std::vector<Vec3> waypoints;
        int currentWaypoint = 0;
        bool loop = true;   // 끝에 도달하면 waypoint[0]부터 재시작
    };

    void addControl(uint32_t playerId, std::vector<Vec3> waypoints, bool loop = true);
    void update(float dt, Room& room);

private:
    std::vector<PlayerControl> controls_{};
};

} // namespace sim
