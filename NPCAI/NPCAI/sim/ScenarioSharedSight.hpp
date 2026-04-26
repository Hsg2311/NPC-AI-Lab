#pragma once
#include "Scenario.hpp"

namespace sim {

// 시야 공유 그룹 NPC 시나리오
// - 그룹 A (청록): 고블린 3마리, 중심 (13, 0, 2), 반경 16
// - 그룹 B (황금): 고블린 3마리, 중심 (32, 0, -4), 반경 14
// - 플레이어가 한 고블린의 감지 범위에 들어오면 그룹 전체가 반응한다.
class ScenarioSharedSight : public Scenario {
public:
    ScenarioSharedSight() = default;
    virtual ~ScenarioSharedSight() = default;

    virtual void setup(Room& room) override;
};

} // namespace sim
