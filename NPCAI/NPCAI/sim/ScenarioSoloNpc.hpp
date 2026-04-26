#pragma once
#include "Scenario.hpp"

namespace sim {

// 독립 NPC만 있는 시나리오 — 스쿼드/플래툰 없음, 플레이어 1명 직접 조작
class ScenarioSoloNpc : public Scenario {
public:
	ScenarioSoloNpc() = default;
	virtual ~ScenarioSoloNpc() = default;

    virtual void setup(Room& room) override;
};

} // namespace sim
