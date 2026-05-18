#pragma once
#include "Scenario.hpp"

namespace sim {

class ScenarioIsis : public Scenario {
public:
    void setup(Room& room) override;
};

} // namespace sim
