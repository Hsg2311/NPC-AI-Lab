#include "Platoon.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <cstdio>
#include <limits>
#include <algorithm>

namespace sim {

// ─── Constructor ──────────────────────────────────────────────────────────────

Platoon::Platoon(int platoonId) : platoonId_(platoonId) {}

// ─── addSquad / removeSquad ──────────────────────────────────────────────────

void Platoon::addSquad(int squadId) {
    squadIds_.push_back(squadId);
}

void Platoon::removeSquad(int squadId) {
    squadIds_.erase(
        std::remove(squadIds_.begin(), squadIds_.end(), squadId),
        squadIds_.end());
    squadReports_.erase(squadId);
}

bool Platoon::ownsSquad(int squadId) const {
    return std::find(squadIds_.begin(), squadIds_.end(), squadId) != squadIds_.end();
}

// ─── receiveReport ───────────────────────────────────────────────────────────
// Called by Room after each Squad::update() completes.

void Platoon::receiveReport(const SquadReport& report) {
    squadReports_[report.squadId] = report;
}

// ─── update ──────────────────────────────────────────────────────────────────
// Called by Room::updatePlatoons(), BEFORE Room::updateSquads() each tick.
// Reads last tick's SquadReports → decides orders → calls Squad::setPlatoonOrder.

void Platoon::update(float dt, Room& room) {
    // Clean up squads that have disbanded (isEmpty)
    squadIds_.erase(
        std::remove_if(squadIds_.begin(), squadIds_.end(),
            [&](int sid) {
                Squad* sq = room.findSquadById(sid);
                return !sq || sq->isEmpty();
            }),
        squadIds_.end());

    if (squadIds_.empty()) return;

    // ── Target selection (rate-limited) ───────────────────────────────────────
    targetChangeTimer_ += dt;
    if (targetChangeTimer_ >= TARGET_CHANGE_COOLDOWN) {
        uint32_t newTarget = selectPrimaryTarget(room);
        if (newTarget != blackboard_.primaryTargetId) {
            blackboard_.primaryTargetId = newTarget;
            char buf[80];
            std::snprintf(buf, sizeof(buf),
                "platoon #%d new primary target id=%u", platoonId_, newTarget);
            Logger::get().log("Platoon", buf);
        }
        targetChangeTimer_ = 0.f;
    }

    // ── Tactical evaluation ───────────────────────────────────────────────────
    evaluateTactics(room);

    // ── Issue orders to squads ────────────────────────────────────────────────
    issueOrderToAll(currentOrder_, blackboard_.primaryTargetId, retreatPoint_, room);

    // ── Clear reports (consume once per tick) ─────────────────────────────────
    squadReports_.clear();
}

// ─── evaluateTactics ─────────────────────────────────────────────────────────
// Simple decision: if overall combat efficiency drops below threshold, retreat.

void Platoon::evaluateTactics(Room& room) {
    if (squadReports_.empty()) return;

    float totalEfficiency = 0.f;
    int   normalCount     = 0;

    for (auto& [id, rep] : squadReports_) {
        if (rep.status == SquadStatus::Normal) {
            totalEfficiency += rep.combatEfficiency;
            ++normalCount;
        }
    }

    if (normalCount == 0) return;

    float avgEfficiency = totalEfficiency / static_cast<float>(normalCount);
    blackboard_.threatLevel = 1.f - avgEfficiency;

    if (currentOrder_ != SquadOrderType::Retreat &&
        avgEfficiency < RETREAT_EFFICIENCY_THRESHOLD) {
        currentOrder_ = SquadOrderType::Retreat;
        char buf[64];
        std::snprintf(buf, sizeof(buf),
            "platoon #%d efficiency=%.2f → Retreat", platoonId_, avgEfficiency);
        Logger::get().log("Platoon", buf);
    } 
    else if (currentOrder_ == SquadOrderType::Retreat &&
               avgEfficiency >= RETREAT_EFFICIENCY_THRESHOLD + 0.1f) {
        // Hysteresis: recover back to Attack only when above threshold+0.1
        currentOrder_ = SquadOrderType::Attack;
        Logger::get().log("Platoon",
            "platoon #" + std::to_string(platoonId_) + " recovered → Attack");
    }
}

// ─── selectPrimaryTarget ────────────────────────────────────────────────────
// Pick the living player that most squads are already targeting (focus fire),
// with a hysteresis bonus for the current primary target.

uint32_t Platoon::selectPrimaryTarget(Room& room) const {
    auto players = room.getLivingPlayers();
    if (players.empty()) return 0;

    uint32_t bestId   = 0;
    float    bestScore = -1.f;

    for (Player* p : players) {
        float score = 0.f;
        // Count squads already targeting this player
        for (auto& [sid, rep] : squadReports_) {
            if (rep.engagedTarget == p->getId()) score += 10.f;
        }
        // Hysteresis: keep current target
        if (p->getId() == blackboard_.primaryTargetId) score += 30.f;
        // Prefer low-HP targets
        float hpRatio = (p->getMaxHp() > 0.f) ? (p->getHp() / p->getMaxHp()) : 1.f;
        score += (1.f - hpRatio) * 20.f;

        if (score > bestScore) { bestScore = score; bestId = p->getId(); }
    }
    return bestId;
}

// ─── issueOrderToAll ─────────────────────────────────────────────────────────

void Platoon::issueOrderToAll(SquadOrderType type, uint32_t targetId,
                               const Vec3& destination, Room& room) {
    for (int sid : squadIds_) {
        Squad* sq = room.findSquadById(sid);
        if (!sq) continue;
        sq->setPlatoonOrder(type, targetId, destination);
    }
}

} // namespace sim
