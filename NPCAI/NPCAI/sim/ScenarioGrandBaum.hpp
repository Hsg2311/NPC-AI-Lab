#pragma once
#include "Scenario.hpp"

namespace sim {

class ScenarioGrandBaum : public Scenario {
public:
    void setup(Room& room) override;
};

} // namespace sim
