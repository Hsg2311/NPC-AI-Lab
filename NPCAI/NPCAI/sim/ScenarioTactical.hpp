#pragma once
#include "Scenario.hpp"

namespace sim {

// PlatoonLeader 1명 + Squad A(2명) + Squad B(2명)
// FlankLeft / FlankRight 협공 검증용 시나리오
class ScenarioTactical : public Scenario {
public:
    void setup(Room& room) override;
};

} // namespace sim
