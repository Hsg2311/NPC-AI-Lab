#include "DummyPlayerController.hpp"
#include "Room.hpp"
#include "Player.hpp"

namespace sim {

void DummyPlayerController::addControl(uint32_t playerId, std::vector<Vec3> waypoints, bool loop) {
    PlayerControl ctrl;
    ctrl.playerId  = playerId;
    ctrl.waypoints = std::move(waypoints);
    ctrl.loop      = loop;
    controls_.push_back(std::move(ctrl));
}

void DummyPlayerController::update(float dt, Room& room) {
    for (auto& ctrl : controls_) {
        if (ctrl.waypoints.empty()) continue;

        Actor* actor = room.findActorById(ctrl.playerId);
        if (!actor || !actor->isAlive()) continue;

        auto player = dynamic_cast<Player*>(actor);
        if (!player) continue;

        const Vec3& target = ctrl.waypoints[ctrl.currentWaypoint];
        player->setMoveTarget(target);

        // 플레이어가 충분히 가까워지면 다음 웨이포인트로 진행
        if (Vec3::distanceSq(player->getPosition(), target) < 0.5f * 0.5f) {
            int next = ctrl.currentWaypoint + 1;
            if (next >= static_cast<int>(ctrl.waypoints.size())) {
                next = ctrl.loop ? 0 : ctrl.currentWaypoint;
            }
            ctrl.currentWaypoint = next;
        }
    }
}

} // namespace sim
