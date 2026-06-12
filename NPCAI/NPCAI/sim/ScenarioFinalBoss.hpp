#pragma once
#include "Scenario.hpp"

namespace sim {

// 최종보스 1대1 검증 시나리오. 플레이어 1명(HumanControl) + FinalBoss 1기.
class ScenarioFinalBoss : public Scenario {
public:
    void setup(Room& room) override;
};

} // namespace sim
