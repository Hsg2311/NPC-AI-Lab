#pragma once
#include "Room.hpp"
#include "Player.hpp"

namespace sim {

class Scenario {
public:
	Scenario() = default;
    virtual ~Scenario() = default;

    virtual void setup(Room& room) = 0;

    Player* controlledPlayer() const { return controlledPlayer_; }

protected:
    Player* controlledPlayer_{ nullptr };
};

} // namespace sim
