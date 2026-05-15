#pragma once
#include "TacticalNpc.hpp"
#include "TacticalSquad.hpp"
#include <memory>
#include <vector>

namespace sim {

class IMidBossTactic;
class Room;

class PlatoonLeader : public TacticalNpc {
public:
    PlatoonLeader(const std::string& name, const Vec3& pos,
                  const TacticalNpcConfig& cfg = {});
    PlatoonLeader(const std::string& name, const Vec3& pos,
                  const TacticalNpcConfig& cfg,
                  std::unique_ptr<IMidBossTactic> tactic);
    ~PlatoonLeader() override;

    void update(float dt, Room& room) override;
    const char* typeName() const override { return "PlatoonLeader"; }

    void addSquad(TacticalSquad* squad);
    const std::vector<TacticalSquad*>& getSquads() const { return squads_; }
    void setTactic(std::unique_ptr<IMidBossTactic> tactic);

    void removeDeadMembersFromSquads(Room& room);
    void pushConfusedToSquads(Room& room);
    void setTacticalTarget(uint32_t targetId) { targetId_ = targetId; }
    void transitionTacticalState(TacticalNpcState next, const char* reason) {
        transitionTo(next, reason);
    }
    float getLeaderMoveSpeed() const { return moveSpeed_; }

private:
    std::vector<TacticalSquad*> squads_;
    std::unique_ptr<IMidBossTactic> tactic_{};
    bool deathReported_{ false };
};

} // namespace sim
