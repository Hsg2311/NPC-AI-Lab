#include "PlatoonLeader.hpp"
#include "MidBossTactics.hpp"

namespace sim {

PlatoonLeader::PlatoonLeader(const std::string& name, const Vec3& pos,
                             const TacticalNpcConfig& cfg)
    : TacticalNpc(name, pos, cfg)
    , tactic_(std::make_unique<GoblinMidBossTactic>())
{}

PlatoonLeader::PlatoonLeader(const std::string& name, const Vec3& pos,
                             const TacticalNpcConfig& cfg,
                             std::unique_ptr<IMidBossTactic> tactic)
    : TacticalNpc(name, pos, cfg)
    , tactic_(std::move(tactic))
{}

PlatoonLeader::~PlatoonLeader() = default;

void PlatoonLeader::addSquad(TacticalSquad* squad) {
    squads_.push_back(squad);
}

void PlatoonLeader::setTactic(std::unique_ptr<IMidBossTactic> tactic) {
    tactic_ = std::move(tactic);
}

void PlatoonLeader::removeDeadMembersFromSquads(Room& room) {
    for (auto* sq : squads_)
        sq->removeDeadMembers(room);
}

void PlatoonLeader::pushConfusedToSquads(Room& room) {
    for (auto* sq : squads_)
        sq->pushConfusedToMembers(room);
}

void PlatoonLeader::update(float dt, Room& room) {
    if (!isAlive()) {
        if (!deathReported_) {
            deathReported_ = true;
            if (tactic_)
                tactic_->onLeaderDead(room, *this);
            else
                pushConfusedToSquads(room);
        }
        updateDead(room);
        return;
    }

    if (tactic_)
        tactic_->update(dt, room, *this);
}

} // namespace sim
