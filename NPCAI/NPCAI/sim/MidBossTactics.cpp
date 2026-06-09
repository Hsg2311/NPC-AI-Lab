#include "MidBossTactics.hpp"
#include "Actor.hpp"
#include "PlatoonLeader.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "TacticalSquad.hpp"
#include "TacticalNpc.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <memory>
#include <random>
#include <string>

namespace sim {

namespace {

std::mt19937& isisCooldownRng() {
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}

} // namespace

void MidBossTacticBase::onLeaderDead(Room& room, PlatoonLeader& leader) {
    leader.pushConfusedToSquads(room);
    Logger::get().log(leader.getName(), "dead - issued Confused to all squads");
}

std::vector<TacticalSquad*>
MidBossTacticBase::collectLiveSquads(PlatoonLeader& leader) const {
    std::vector<TacticalSquad*> liveSquads;
    for (TacticalSquad* squad : leader.getSquads()) {
        if (squad && !squad->isEmpty())
            liveSquads.push_back(squad);
    }
    return liveSquads;
}

std::vector<MidBossTacticBase::PlayerCluster>
MidBossTacticBase::buildPlayerClusters(const Room& room,
                                       float clusterRadius) const {
    const auto& players = room.getLivingPlayers();
    int count = static_cast<int>(players.size());
    std::vector<PlayerCluster> result;
    if (count <= 0)
        return result;

    std::vector<bool> visited(static_cast<size_t>(count), false);
    float clusterRadiusSq = clusterRadius * clusterRadius;

    for (int i = 0; i < count; ++i) {
        if (visited[static_cast<size_t>(i)])
            continue;

        PlayerCluster cluster;
        std::vector<int> stack;
        stack.push_back(i);
        visited[static_cast<size_t>(i)] = true;

        while (!stack.empty()) {
            int current = stack.back();
            stack.pop_back();

            Player* currentPlayer = players[static_cast<size_t>(current)];
            Vec3 currentPos = currentPlayer->getPosition();
            cluster.centroid += currentPos;
            cluster.playerIds.push_back(currentPlayer->getId());
            if (cluster.representativeId == 0)
                cluster.representativeId = currentPlayer->getId();

            for (int j = 0; j < count; ++j) {
                if (visited[static_cast<size_t>(j)])
                    continue;
                Vec3 otherPos = players[static_cast<size_t>(j)]->getPosition();
                if (Vec3::distanceSq(currentPos, otherPos) <= clusterRadiusSq) {
                    visited[static_cast<size_t>(j)] = true;
                    stack.push_back(j);
                }
            }
        }

        if (!cluster.playerIds.empty()) {
            cluster.centroid = cluster.centroid /
                               static_cast<float>(cluster.playerIds.size());
            result.push_back(cluster);
        }
    }

    return result;
}

Vec3 MidBossTacticBase::calcPlayerCentroid(const Room& room,
                                           const Vec3& fallback) const {
    const auto& players = room.getLivingPlayers();
    if (players.empty())
        return fallback;

    Vec3 sum{};
    for (Player* p : players)
        sum += p->getPosition();
    return sum / static_cast<float>(players.size());
}

Vec3 MidBossTacticBase::calcAveragePlayerFacing(
    const Room& room, const Vec3& fallbackDir) const {
    Vec3 sum{};
    for (Player* p : room.getLivingPlayers())
        sum += p->getFacing();

    if (sum.lengthSq() > 0.01f)
        return sum.normalized();
    if (fallbackDir.lengthSq() > 0.01f)
        return fallbackDir.normalized();
    return Vec3{};
}

Player* MidBossTacticBase::selectNearestPlayer(Room& room,
                                               const Vec3& from) const {
    Player* best = nullptr;
    float bestDistSq = -1.f;
    for (Player* p : room.getLivingPlayers()) {
        float dSq = Vec3::distanceSq(from, p->getPosition());
        if (bestDistSq < 0.f || dSq < bestDistSq) {
            bestDistSq = dSq;
            best = p;
        }
    }
    return best;
}

uint32_t MidBossTacticBase::selectNearestPlayerId(Room& room,
                                                  const Vec3& from) const {
    Player* player = selectNearestPlayer(room, from);
    return player ? player->getId() : 0;
}

void MidBossTacticBase::issueEngageAll(PlatoonLeader& leader,
                                       uint32_t targetId) const {
    if (targetId == 0)
        return;

    for (TacticalSquad* squad : leader.getSquads()) {
        if (!squad || squad->isEmpty())
            continue;
        SquadOrder ord;
        ord.type = SquadOrderType::Engage;
        ord.targetId = targetId;
        squad->receiveOrder(ord);
    }
}

void MidBossTacticBase::issueIdleAll(PlatoonLeader& leader) const {
    for (TacticalSquad* squad : leader.getSquads()) {
        if (!squad || squad->isEmpty())
            continue;
        SquadOrder ord;
        ord.type = SquadOrderType::Idle;
        squad->receiveOrder(ord);
    }
}

void MidBossTacticBase::assignSquadsToPlayers(
    const Room& room, const PlatoonLeader& leader,
    const std::vector<TacticalSquad*>& liveSquads,
    std::vector<uint32_t>& outTargetIds) const
{
    const auto& players = room.getLivingPlayers();
    int numSquads = static_cast<int>(liveSquads.size());
    int numPlayers = static_cast<int>(players.size());

    if (numPlayers <= 1) return;

    int maxPerPlayer = (numSquads + numPlayers - 1) / numPlayers;

    struct DistEntry { float dist; int squadIdx; int playerIdx; };
    std::vector<DistEntry> entries;
    entries.reserve(static_cast<size_t>(numSquads * numPlayers));

    for (int si = 0; si < numSquads; ++si) {
        Vec3 centroid{}; int cnt = 0;
        for (uint32_t mid : liveSquads[static_cast<size_t>(si)]->getMembers()) {
            Actor* a = room.findActorById(mid);
            if (a && a->isAlive()) { centroid += a->getPosition(); ++cnt; }
        }
        if (cnt > 0) centroid = centroid / static_cast<float>(cnt);
        else         centroid = leader.getPosition();

        for (int pi = 0; pi < numPlayers; ++pi) {
            float dSq = Vec3::distanceSq(centroid,
                players[static_cast<size_t>(pi)]->getPosition());
            entries.push_back({ dSq, si, pi });
        }
    }

    std::sort(entries.begin(), entries.end(),
        [](const DistEntry& a, const DistEntry& b) { return a.dist < b.dist; });

    std::vector<bool> squadDone(static_cast<size_t>(numSquads), false);
    std::vector<int> playerCount(static_cast<size_t>(numPlayers), 0);

    for (const auto& e : entries) {
        if (squadDone[static_cast<size_t>(e.squadIdx)]) continue;
        if (playerCount[static_cast<size_t>(e.playerIdx)] >= maxPerPlayer) continue;
        outTargetIds[static_cast<size_t>(e.squadIdx)] =
            players[static_cast<size_t>(e.playerIdx)]->getId();
        squadDone[static_cast<size_t>(e.squadIdx)] = true;
        playerCount[static_cast<size_t>(e.playerIdx)]++;
    }
}

void GoblinMidBossTactic::update(float dt, Room& room, PlatoonLeader& leader) {
    auto& squads = leader.getSquads();
    if (!initialSizesSet_) {
        initialSizesSet_ = true;
        for (auto* sq : squads)
            initialSquadSizes_.push_back(static_cast<int>(sq->getMembers().size()));
    }

    leader.removeDeadMembersFromSquads(room);

    bool hasLiveSquad = false;
    for (auto* sq : squads) {
        if (sq && !sq->isEmpty()) {
            hasLiveSquad = true;
            break;
        }
    }
    if (!hasLiveSquad && leaderPhase_ != LeaderPhase::BossSolo) {
        tacticCooldown_ = 0.f;
        enterPhase(LeaderPhase::BossSolo, "Goblin squads wiped - boss solo", leader);
    }

    if (leaderPhase_ == LeaderPhase::Cooldown) {
        tacticCooldown_ -= dt;
        if (tacticCooldown_ <= 0.f)
            enterPhase(tacticsUnlocked_ ? LeaderPhase::TacticalRetreat
                                         : LeaderPhase::BoxAdvance,
                       "전술 쿨타임 종료", leader);
    } else if (leaderPhase_ == LeaderPhase::Encircle) {
        auto liveSquads = collectLiveSquads(leader);
        auto clusters = buildPlayerClusters(room, leader);
        if (clusters.size() != 1 ||
            !canStartEncircle(liveSquads, clusters.front())) {
            enterTacticFailCooldown(room, leader, "포위 중단 - 생존 부대 부족");
        } else {
            int liveMembers = countLiveMembers(liveSquads);
            if (phaseOrderIssued_ && liveMembers != encircleIssuedLiveMembers_) {
                phaseOrderIssued_ = false;
                tacticTimer_ = 0.f;
            }
        }

        if (phaseOrderIssued_ && allMembersArrived(room, leader)) {
            auto liveSquads = collectLiveSquads(leader);
            issueStableEngage(room, liveSquads, true);
            tacticCooldown_ = TACTIC_COOLDOWN_DURATION;
            enterPhase(LeaderPhase::Cooldown, "포위 완성 - Engage 후 쿨타임 진입", leader);
        }
    } else if (leaderPhase_ == LeaderPhase::DivideAndConquer) {
        updateDivideAndConquer(dt, room, leader);
    }

    Player* primary = selectPrimaryTarget(room, leader);

    if (leaderPhase_ != LeaderPhase::BossSolo &&
        !tacticsUnlocked_ && primary && checkTacticsConditions(leader)) {
        tacticsUnlocked_ = true;
        enterPhase(LeaderPhase::TacticalRetreat, "전술 활성화 - 공통 후퇴 시작", leader);
    }

    if (leaderPhase_ == LeaderPhase::BoxAdvance &&
        primary && allMembersArrived(room, leader)) {
        if (!tacticsUnlocked_) {
            enterPhase(LeaderPhase::Engage, "박스 대형 완성 - 일반 교전 전환", leader);
            auto liveSquads = collectLiveSquads(leader);
            issueStableEngage(room, liveSquads, true);
        } else {
            auto clusters = buildPlayerClusters(room, leader);
            auto liveSquads = collectLiveSquads(leader);
            if (clusters.size() == 1 &&
                canStartEncircle(liveSquads, clusters.front())) {
                enterPhase(LeaderPhase::Encircle, "박스 대형 완성 - 플레이어 군집 포위", leader);
            } else if (clusters.size() == 1) {
                enterTacticFailCooldown(room, leader, "포위 취소 - 생존 부대 부족");
            } else {
                enterPhase(LeaderPhase::Vigilance, "박스 대형 완성 - 플레이어 분산 경계", leader);
            }
        }
    }

    if (leaderPhase_ == LeaderPhase::Vigilance &&
        phaseOrderIssued_ && allMembersArrived(room, leader)) {
        auto clusters = buildPlayerClusters(room, leader);
        auto liveSquads = collectLiveSquads(leader);
        if (clusters.size() <= 1 && !clusters.empty() &&
            canStartEncircle(liveSquads, clusters.front()))
            enterPhase(LeaderPhase::Encircle, "경계 완료 - 플레이어 재집결 포위", leader);
        else if (clusters.size() <= 1)
            enterTacticFailCooldown(room, leader, "포위 취소 - 생존 부대 부족");
        else
            enterPhase(LeaderPhase::DivideAndConquer, "경계 완료 - 각개격파 전환", leader);
    }

    bool leaderAtRetreat = Vec3::distance(leader.getPosition(), retreatTargetPos_) <= 1.5f;
    if (leaderPhase_ == LeaderPhase::TacticalRetreat &&
        phaseOrderIssued_ && allMembersArrived(room, leader) && leaderAtRetreat) {
        enterPhase(LeaderPhase::BoxAdvance, "후퇴 완료 - 박스 대형 전환", leader);
    }

    tacticTimer_ -= dt;
    if (tacticTimer_ <= 0.f) {
        tacticTimer_ = TACTIC_INTERVAL;
        evaluateTactics(room, leader);
    }

    if (leaderPhase_ == LeaderPhase::TacticalRetreat) {
        Vec3 toRetreat = retreatTargetPos_ - leader.getPosition();
        float d = toRetreat.length();
        if (d > 1.f) {
            toRetreat = toRetreat / d;
            leader.setPosition(leader.getPosition() +
                toRetreat * (leader.getLeaderMoveSpeed() * TACTICAL_SPEED_MULT) * dt);
            leader.setFacing(toRetreat);
        }
        return;
    }

    updateBossPersonalCombat(dt, room, leader);
}

void GoblinMidBossTactic::enterPhase(LeaderPhase next, const char* reason,
                                     PlatoonLeader& leader) {
    Logger::get().log(leader.getName(), reason);
    leaderPhase_ = next;
    phaseOrderIssued_ = false;
    if (next != LeaderPhase::Encircle)
        encircleIssuedLiveMembers_ = 0;
    if (next != LeaderPhase::DivideAndConquer) {
        divideTasks_.clear();
        divideTargetPlayerIds_.clear();
        divideStage_ = DivideStage::Preparing;
        divideEngageTimer_ = 0.f;
    }
    if (next == LeaderPhase::BoxAdvance ||
        next == LeaderPhase::TacticalRetreat ||
        next == LeaderPhase::Encircle ||
        next == LeaderPhase::Vigilance ||
        next == LeaderPhase::DivideAndConquer ||
        next == LeaderPhase::BossSolo) {
        engageTargetBySquad_.clear();
    }
    if (next != LeaderPhase::Cooldown)
        tacticTimer_ = 0.f;
}

void GoblinMidBossTactic::enterTacticFailCooldown(Room& room,
                                                  PlatoonLeader& leader,
                                                  const char* reason) {
    leader.removeDeadMembersFromSquads(room);

    tacticCooldown_ = TACTIC_FAIL_COOLDOWN_DURATION;
    enterPhase(LeaderPhase::Cooldown, reason, leader);

    std::vector<TacticalSquad*> liveSquads;
    for (auto* sq : leader.getSquads()) {
        if (!sq->isEmpty()) liveSquads.push_back(sq);
    }

    issueStableEngage(room, liveSquads, true);
}

void GoblinMidBossTactic::evaluateTactics(Room& room, PlatoonLeader& leader) {
    leader.removeDeadMembersFromSquads(room);

    std::vector<TacticalSquad*> liveSquads;
    for (auto* sq : leader.getSquads())
        if (!sq->isEmpty()) liveSquads.push_back(sq);

    if (leaderPhase_ == LeaderPhase::BossSolo)
        return;

    Player* primary = selectPrimaryTarget(room, leader);

    if (!primary || liveSquads.empty()) {
        engageTargetBySquad_.clear();
        for (auto* sq : liveSquads) {
            SquadOrder ord; ord.type = SquadOrderType::Idle;
            sq->receiveOrder(ord);
        }
        if (leader.getState() != TacticalNpcState::Idle) {
            leader.setTacticalTarget(0);
            leader.transitionTacticalState(TacticalNpcState::Idle, "플레이어 없음");
        }
        return;
    }

    if (leader.getTargetId() != primary->getId()) {
        leader.setTacticalTarget(primary->getId());
        if (leader.getState() == TacticalNpcState::Idle) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "전술 평가: 대상 %s",
                primary->getName().c_str());
            leader.transitionTacticalState(TacticalNpcState::Chase, buf);
        }
    }

    int numSquads = static_cast<int>(liveSquads.size());
    primaryTargetId_ = primary->getId();

    if (leaderPhase_ == LeaderPhase::TacticalRetreat) {
        if (phaseOrderIssued_) return;

        Vec3 playerCent = calcPlayerCentroid(room, leader.getPosition());
        Vec3 awayDir = leader.getPosition() - playerCent;
        float awayLen = awayDir.length();
        if (awayLen > 0.01f) awayDir = awayDir / awayLen;
        else                 awayDir = Vec3{ -1.f, 0.f, 0.f };

        retreatTargetPos_ = playerCent + awayDir * REGROUP_DIST;

        for (int i = 0; i < numSquads; ++i) {
            SquadOrder ord;
            ord.type = SquadOrderType::RetreatFormUp;
            ord.targetId = primaryTargetId_;
            ord.leaderPos = leader.getPosition();
            ord.tacticCenter = retreatTargetPos_;
            ord.formationTargetPos = playerCent;
            liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
        }
        phaseOrderIssued_ = true;
        return;
    }

    if (leaderPhase_ == LeaderPhase::BoxAdvance) {
        if (phaseOrderIssued_) return;

        boxAdvanceTargetPos_ = calcPlayerCentroid(room, leader.getPosition());

        Vec3 toTgt = boxAdvanceTargetPos_ - leader.getPosition();
        float len = toTgt.length();
        Vec3 fwd = (len > 0.01f) ? (toTgt / len) : Vec3{ 1.f, 0.f, 0.f };
        Vec3 right{ -fwd.z, 0.f, fwd.x };
        Vec3 boxCenter = leader.getPosition() + fwd * BOX_FRONT_OFFSET;

        std::vector<std::pair<float, TacticalSquad*>> sqByLat;
        sqByLat.reserve(static_cast<size_t>(numSquads));
        for (auto* sq : liveSquads) {
            Vec3 sum{}; int cnt = 0;
            for (uint32_t mid : sq->getMembers()) {
                Actor* ma = room.findActorById(mid);
                if (ma && ma->isAlive()) { sum += ma->getPosition(); ++cnt; }
            }
            Vec3 cen = (cnt > 0) ? (sum / static_cast<float>(cnt))
                                 : leader.getPosition();
            sqByLat.push_back({ cen.dot(right), sq });
        }
        std::sort(sqByLat.begin(), sqByLat.end(),
            [](const std::pair<float, TacticalSquad*>& a,
               const std::pair<float, TacticalSquad*>& b) {
                return a.first < b.first;
            });

        auto offsets = calcSquadBoxOffsets(numSquads);
        for (int i = 0; i < numSquads; ++i) {
            SquadOrder ord;
            ord.type = SquadOrderType::BoxAdvance;
            ord.targetId = primaryTargetId_;
            ord.sectorPos = offsets[static_cast<size_t>(i)];
            ord.leaderPos = leader.getPosition();
            ord.tacticCenter = boxCenter;
            ord.formationTargetPos = boxAdvanceTargetPos_;
            sqByLat[static_cast<size_t>(i)].second->receiveOrder(ord);
        }
        phaseOrderIssued_ = true;
        return;
    }

    if (leaderPhase_ == LeaderPhase::Vigilance) {
        if (phaseOrderIssued_) return;

        Vec3 playerCent = calcPlayerCentroid(room, leader.getPosition());
        Vec3 toPlayers = playerCent - leader.getPosition();
        float len = toPlayers.length();
        Vec3 forward = (len > 0.01f) ? (toPlayers / len) : Vec3{ 1.f, 0.f, 0.f };
        float baseAngle = std::atan2f(forward.z, forward.x);
        constexpr float TWO_PI = 2.f * 3.14159265f;

        for (int i = 0; i < numSquads; ++i) {
            SquadOrder ord;
            ord.type = SquadOrderType::GuardBoss;
            ord.targetId = primaryTargetId_;
            ord.sectorAngle = baseAngle + TWO_PI * static_cast<float>(i)
                                           / static_cast<float>(numSquads);
            ord.approachRadius = VIGILANCE_GUARD_RADIUS;
            ord.tacticCenter = leader.getPosition();
            ord.formationTargetPos = playerCent;
            liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);
        }
        phaseOrderIssued_ = true;
        return;
    }

    if (leaderPhase_ == LeaderPhase::DivideAndConquer) {
        if (phaseOrderIssued_) return;

        auto clusters = buildPlayerClusters(room, leader);
        if (clusters.size() <= 1) {
            if (!clusters.empty() && canStartEncircle(liveSquads, clusters.front()))
                enterPhase(LeaderPhase::Encircle, "각개격파 취소 - 플레이어 재집결", leader);
            else
                enterTacticFailCooldown(room, leader, "포위 취소 - 생존 부대 부족");
            return;
        }

        if (liveSquads.size() < 3) {
            enterTacticFailCooldown(
                room, leader,
                "DivideAndConquer cancelled - fewer than three squads");
            return;
        }
        if (!issueDivideAndConquer(room, leader, liveSquads, clusters)) {
            enterTacticFailCooldown(
                room, leader,
                "DivideAndConquer failed - corridor assignment unavailable");
            return;
        }
        phaseOrderIssued_ = true;
        return;
    }

    if (leaderPhase_ == LeaderPhase::Encircle) {
        if (phaseOrderIssued_) return;

        constexpr float TWO_PI = 2.f * 3.14159265f;
        auto clusters = buildPlayerClusters(room, leader);
        if (clusters.size() != 1 || !canStartEncircle(liveSquads, clusters.front())) {
            enterTacticFailCooldown(room, leader, "포위 취소 - 생존 부대 부족");
            return;
        }

        Vec3 encircleCenter = calcPlayerCentroid(room, leader.getPosition());
        int totalMembers = countLiveMembers(liveSquads);
        if (totalMembers < 1) totalMembers = 1;
        float encircleRadius = calcEncircleRadius(totalMembers);

        float angleAccum = 0.f;
        for (int i = 0; i < numSquads; ++i) {
            int memberCount = static_cast<int>(
                liveSquads[static_cast<size_t>(i)]->getMembers().size());
            float fraction = static_cast<float>(memberCount) /
                             static_cast<float>(totalMembers);
            float sectorSpan = TWO_PI * fraction;
            float sectorAngle = angleAccum + sectorSpan * 0.5f;

            SquadOrder ord;
            ord.type = SquadOrderType::Encircle;
            ord.targetId = primary->getId();
            ord.sectorAngle = sectorAngle;
            ord.sectorSpan = sectorSpan;
            ord.approachRadius = encircleRadius;
            ord.tacticCenter = encircleCenter;
            liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);

            angleAccum += sectorSpan;
        }
        encircleIssuedLiveMembers_ = totalMembers;
        phaseOrderIssued_ = true;
        return;
    }

    issueStableEngage(room, liveSquads, false);
}

bool GoblinMidBossTactic::isLivingPlayerTarget(
    const Room& room, uint32_t targetId) const {
    auto* player =
        dynamic_cast<Player*>(room.findActorById(targetId));
    return player && player->isAlive();
}

void GoblinMidBossTactic::issueStableEngage(
    Room& room,
    const std::vector<TacticalSquad*>& liveSquads,
    bool resetAssignments) {
    if (resetAssignments)
        engageTargetBySquad_.clear();

    for (auto it = engageTargetBySquad_.begin();
         it != engageTargetBySquad_.end();) {
        bool squadAlive = false;
        for (TacticalSquad* squad : liveSquads) {
            if (squad && squad->getSquadId() == it->first) {
                squadAlive = true;
                break;
            }
        }

        if (!squadAlive || !isLivingPlayerTarget(room, it->second))
            it = engageTargetBySquad_.erase(it);
        else
            ++it;
    }

    if (!resetAssignments) {
        for (TacticalSquad* squad : liveSquads) {
            if (!squad || squad->isEmpty())
                continue;
            int squadId = squad->getSquadId();
            if (engageTargetBySquad_.find(squadId) !=
                engageTargetBySquad_.end()) {
                continue;
            }

            uint32_t currentTargetId = squad->getEngageTargetId();
            if (isLivingPlayerTarget(room, currentTargetId))
                engageTargetBySquad_[squadId] = currentTargetId;
        }
    }

    const auto& players = room.getLivingPlayers();
    if (players.empty())
        return;

    std::unordered_map<uint32_t, int> assignmentCounts;
    for (Player* player : players)
        assignmentCounts[player->getId()] = 0;
    for (const auto& [squadId, targetId] : engageTargetBySquad_) {
        auto countIt = assignmentCounts.find(targetId);
        if (countIt != assignmentCounts.end())
            ++countIt->second;
    }

    for (TacticalSquad* squad : liveSquads) {
        if (!squad || squad->isEmpty())
            continue;

        int squadId = squad->getSquadId();
        auto assignedIt = engageTargetBySquad_.find(squadId);
        if (assignedIt == engageTargetBySquad_.end()) {
            Player* best = nullptr;
            int bestAssignmentCount = 0;
            float bestDistSq = 0.f;
            Vec3 squadCenter = squad->calcCentroid();

            for (Player* player : players) {
                int assignedCount = assignmentCounts[player->getId()];
                float distSq = Vec3::distanceSq(
                    squadCenter, player->getPosition());
                if (!best ||
                    assignedCount < bestAssignmentCount ||
                    (assignedCount == bestAssignmentCount &&
                     (distSq < bestDistSq ||
                      (distSq == bestDistSq &&
                       player->getId() < best->getId())))) {
                    best = player;
                    bestAssignmentCount = assignedCount;
                    bestDistSq = distSq;
                }
            }

            if (!best)
                continue;
            assignedIt = engageTargetBySquad_
                .emplace(squadId, best->getId()).first;
            ++assignmentCounts[best->getId()];
        }

        if (squad->getEngageTargetId() == assignedIt->second)
            continue;

        SquadOrder order;
        order.type = SquadOrderType::Engage;
        order.targetId = assignedIt->second;
        squad->receiveOrder(order);
    }
}

int GoblinMidBossTactic::countLiveMembers(
    const std::vector<TacticalSquad*>& liveSquads) const
{
    int total = 0;
    for (const TacticalSquad* squad : liveSquads) {
        if (!squad)
            continue;
        total += static_cast<int>(squad->getMembers().size());
    }
    return total;
}

int GoblinMidBossTactic::minMembersForEncircle(int playerCount) const {
    if (playerCount <= 1) return 6;
    if (playerCount == 2) return 8;
    if (playerCount == 3) return 10;
    return 12;
}

bool GoblinMidBossTactic::canStartEncircle(
    const std::vector<TacticalSquad*>& liveSquads,
    const PlayerCluster& cluster) const
{
    return countLiveMembers(liveSquads) >=
        minMembersForEncircle(static_cast<int>(cluster.playerIds.size()));
}

float GoblinMidBossTactic::calcEncircleRadius(int liveMembers) const {
    constexpr float TWO_PI = 2.f * 3.14159265f;
    float radiusByCount =
        static_cast<float>(std::max(liveMembers, 1)) *
        ENCIRCLE_SLOT_SPACING / TWO_PI;
    return std::clamp(radiusByCount, ENCIRCLE_MIN_RADIUS, ENCIRCLE_RADIUS);
}

bool GoblinMidBossTactic::checkTacticsConditions(const PlatoonLeader& leader) const {
    if (leader.getMaxHp() > 0.f &&
        leader.getHp() / leader.getMaxHp() <= TACTIC_HP_THRESHOLD)
        return true;

    const auto& squads = leader.getSquads();
    for (size_t i = 0; i < squads.size(); ++i) {
        int initial = (i < initialSquadSizes_.size()) ? initialSquadSizes_[i] : 0;
        int current = static_cast<int>(squads[i]->getMembers().size());
        if (initial > 0 &&
            static_cast<float>(current) / static_cast<float>(initial) <=
                TACTIC_SQUAD_RATIO)
            return true;
    }
    return false;
}

int GoblinMidBossTactic::clusterPlayers(const Room& room,
                                        const PlatoonLeader& leader) const {
    return static_cast<int>(buildPlayerClusters(room, leader).size());
}

std::vector<GoblinMidBossTactic::PlayerCluster>
GoblinMidBossTactic::buildPlayerClusters(const Room& room,
                                         const PlatoonLeader& leader) const {
    std::vector<PlayerCluster> result =
        MidBossTacticBase::buildPlayerClusters(room, CLUSTER_RADIUS);

    for (PlayerCluster& cluster : result) {
        cluster.representativeId = 0;
        cluster.score = -1.f;
        for (uint32_t id : cluster.playerIds) {
            const auto* p = dynamic_cast<const Player*>(room.findActorById(id));
            if (!p || !p->isAlive())
                continue;

            float score = evaluatePlayerScore(p, leader);
            if (cluster.representativeId == 0 || score > cluster.score) {
                cluster.representativeId = p->getId();
                cluster.score = score;
            }
        }
    }

    return result;
}

Player* GoblinMidBossTactic::selectPrimaryTarget(Room& room,
                                                 const PlatoonLeader& leader) const {
    Player* best = nullptr;
    float bestScore = -1.f;
    for (Player* p : room.getLivingPlayers()) {
        float s = evaluatePlayerScore(p, leader);
        if (s > bestScore) { bestScore = s; best = p; }
    }
    return best;
}

uint32_t GoblinMidBossTactic::selectReplacementTarget(
    Room& room, const PlatoonLeader& leader,
    const std::vector<uint32_t>& playerIds) const
{
    Player* best = nullptr;
    float bestScore = -1.f;
    for (uint32_t id : playerIds) {
        auto* p = dynamic_cast<Player*>(room.findActorById(id));
        if (!p || !p->isAlive()) continue;
        float score = evaluatePlayerScore(p, leader);
        if (score > bestScore) {
            bestScore = score;
            best = p;
        }
    }
    return best ? best->getId() : 0;
}

bool GoblinMidBossTactic::issueDivideAndConquer(
    Room& room, PlatoonLeader& leader,
    const std::vector<TacticalSquad*>& liveSquads,
    const std::vector<PlayerCluster>& clusters)
{
    divideTasks_.clear();
    divideTargetPlayerIds_.clear();
    divideStage_ = DivideStage::Preparing;
    divideEngageTimer_ = 0.f;
    if (liveSquads.size() < 3 || clusters.size() <= 1)
        return false;

    std::vector<PlayerCluster> sorted = clusters;
    std::sort(sorted.begin(), sorted.end(),
        [](const PlayerCluster& a, const PlayerCluster& b) {
            return a.score > b.score;
        });

    const PlayerCluster& chargeCluster = sorted[0];

    int chargeSquadIdx = 0;
    float bestDist = -1.f;
    for (int i = 0; i < static_cast<int>(liveSquads.size()); ++i) {
        float d = Vec3::distance(liveSquads[static_cast<size_t>(i)]->calcCentroid(),
                                 chargeCluster.centroid);
        if (bestDist < 0.f || d < bestDist) {
            bestDist = d;
            chargeSquadIdx = i;
        }
    }

    TacticalSquad* chargeSquad = liveSquads[static_cast<size_t>(chargeSquadIdx)];
    if (!chargeSquad || chargeSquad->isEmpty())
        return false;

    divideCorridorForward_ =
        chargeCluster.centroid - chargeSquad->calcCentroid();
    if (divideCorridorForward_.lengthSq() <= 0.01f)
        divideCorridorForward_ =
            chargeCluster.centroid - leader.getPosition();
    if (divideCorridorForward_.lengthSq() <= 0.01f)
        divideCorridorForward_ = Vec3{ 1.f, 0.f, 0.f };
    else
        divideCorridorForward_ = divideCorridorForward_.normalized();
    divideCorridorRight_ = Vec3{
        -divideCorridorForward_.z, 0.f, divideCorridorForward_.x
    };
    divideCorridorCenter_ = chargeCluster.centroid;
    divideCorridorHalfWidth_ =
        chargeSquad->estimateWedgeHalfWidth() + CAPTURE_CORRIDOR_CLEARANCE;
    divideTargetPlayerIds_ = chargeCluster.playerIds;

    SquadOrder charge;
    charge.type = SquadOrderType::WedgeCharge;
    charge.targetId = chargeCluster.representativeId;
    charge.targetIds = chargeCluster.playerIds;
    charge.tacticCenter = chargeCluster.centroid;
    charge.waitForChargeRelease = true;
    chargeSquad->receiveOrder(charge);
    divideTasks_.push_back({ chargeSquad, DivideTaskType::Charge,
                             chargeCluster.representativeId, chargeCluster.playerIds });

    std::vector<TacticalSquad*> screenCandidates;
    screenCandidates.reserve(liveSquads.size() - 1);
    for (int i = 0; i < static_cast<int>(liveSquads.size()); ++i) {
        if (i != chargeSquadIdx)
            screenCandidates.push_back(
                liveSquads[static_cast<size_t>(i)]);
    }
    std::sort(screenCandidates.begin(), screenCandidates.end(),
        [&chargeCluster](const TacticalSquad* a, const TacticalSquad* b) {
            return Vec3::distanceSq(a->calcCentroid(),
                                    chargeCluster.centroid) <
                   Vec3::distanceSq(b->calcCentroid(),
                                    chargeCluster.centroid);
        });

    divideCorridorHalfLength_ = -1.f;
    for (int screenIdx = 0; screenIdx < 2; ++screenIdx) {
        TacticalSquad* screenSquad =
            screenCandidates[static_cast<size_t>(screenIdx)];
        if (!screenSquad || screenSquad->isEmpty())
            return false;

        int memberCount =
            static_cast<int>(screenSquad->getMembers().size());
        if (memberCount < 2)
            return false;
        float separationRadius = 3.f;
        if (!screenSquad->getMemberCache().empty() &&
            screenSquad->getMemberCache().front()) {
            separationRadius =
                screenSquad->getMemberCache().front()->getSeparationRadius();
        }
        float spacingScale = CAPTURE_LINE_SPACING_SCALE;
        if (memberCount > 1) {
            float requiredSpacing =
                (CAPTURE_MIN_HALF_LENGTH * 2.f) /
                static_cast<float>(memberCount - 1);
            spacingScale = std::max(
                spacingScale, requiredSpacing / separationRadius);
        }
        float spacing = std::max(
            separationRadius * spacingScale, 1.2f);
        float lineHalfLength =
            static_cast<float>(std::max(memberCount - 1, 0)) *
            spacing * 0.5f;
        if (divideCorridorHalfLength_ < 0.f ||
            lineHalfLength < divideCorridorHalfLength_) {
            divideCorridorHalfLength_ = lineHalfLength;
        }

        float sideSign = (screenIdx == 0) ? 1.f : -1.f;
        SquadOrder screen;
        screen.type = SquadOrderType::FormationGuard;
        screen.targetId = leader.getId();
        screen.slotSpacingScale = spacingScale;
        screen.slotColumnCount = memberCount;
        screen.tacticCenter =
            divideCorridorCenter_ +
            divideCorridorRight_ *
                (divideCorridorHalfWidth_ * sideSign);
        screen.formationTargetPos = divideCorridorCenter_;
        screenSquad->receiveOrder(screen);
        divideTasks_.push_back({
            screenSquad, DivideTaskType::Screen,
            chargeCluster.representativeId, chargeCluster.playerIds
        });
    }

    for (size_t i = 2; i < screenCandidates.size(); ++i) {
        SquadOrder idle;
        idle.type = SquadOrderType::Idle;
        screenCandidates[i]->receiveOrder(idle);
    }

    return divideTasks_.size() == 3;
}

void GoblinMidBossTactic::updateDivideAndConquer(float dt, Room& room,
                                                 PlatoonLeader& leader) {
    if (!phaseOrderIssued_)
        return;
    if (divideTasks_.size() != 3) {
        enterTacticFailCooldown(
            room, leader,
            "DivideAndConquer failed - incomplete corridor assignment");
        return;
    }

    TacticalSquad* chargeSquad = nullptr;
    bool screenSquadsReady = true;
    for (const auto& task : divideTasks_) {
        if (!task.squad || task.squad->isEmpty()) {
            if (task.type == DivideTaskType::Charge && task.squad)
                task.squad->endActiveWedgeCharge(room);
            else if (chargeSquad)
                chargeSquad->endActiveWedgeCharge(room);
            enterTacticFailCooldown(
                room, leader,
                "DivideAndConquer failed - assigned squad wiped");
            return;
        }

        if (task.type == DivideTaskType::Charge)
            chargeSquad = task.squad;
        else if (task.type == DivideTaskType::Screen &&
                 !task.squad->areMembersAtSlots())
            screenSquadsReady = false;
    }
    if (!chargeSquad) {
        enterTacticFailCooldown(
            room, leader,
            "DivideAndConquer failed - charge squad missing");
        return;
    }

    if (divideStage_ == DivideStage::Preparing) {
        if (!isCaptureClusterInsideCorridor(room)) {
            chargeSquad->endActiveWedgeCharge(room);
            enterTacticFailCooldown(
                room, leader,
                "DivideAndConquer failed - target escaped corridor");
            return;
        }

        if (chargeSquad->isWedgePrepared() &&
            screenSquadsReady) {
            chargeSquad->releaseWedgeCharge();
            divideStage_ = DivideStage::Charging;
            Logger::get().log(
                leader.getName(),
                "Capture corridor formed - WedgeCharge released");
        }
        return;
    }

    if (divideStage_ == DivideStage::Charging) {
        if (!chargeSquad->areChargeMembersComplete())
            return;

        chargeSquad->endActiveWedgeCharge(room);
        issueDivideEngage(room, leader);
        divideStage_ = DivideStage::Engaging;
        divideEngageTimer_ = DIVIDE_ENGAGE_DURATION;
        Logger::get().log(
            leader.getName(),
            "Capture corridor charge complete - Engage");
        return;
    }

    divideEngageTimer_ -= dt;
    if (divideEngageTimer_ <= 0.f) {
        tacticCooldown_ = TACTIC_COOLDOWN_DURATION;
        enterPhase(
            LeaderPhase::Cooldown,
            "DivideAndConquer complete - cooldown", leader);
    }
}

bool GoblinMidBossTactic::calcCaptureClusterCentroid(
    Room& room, Vec3& outCentroid) const {
    Vec3 sum{};
    int count = 0;
    for (uint32_t playerId : divideTargetPlayerIds_) {
        auto* player =
            dynamic_cast<Player*>(room.findActorById(playerId));
        if (!player || !player->isAlive())
            continue;
        sum += player->getPosition();
        ++count;
    }

    if (count <= 0)
        return false;
    outCentroid = sum / static_cast<float>(count);
    return true;
}

bool GoblinMidBossTactic::isCaptureClusterInsideCorridor(
    Room& room) const {
    Vec3 clusterCenter{};
    if (!calcCaptureClusterCentroid(room, clusterCenter))
        return false;

    Vec3 delta = clusterCenter - divideCorridorCenter_;
    float lateral =
        std::fabs(delta.dot(divideCorridorRight_));
    float longitudinal =
        std::fabs(delta.dot(divideCorridorForward_));
    return lateral <=
               divideCorridorHalfWidth_ + CAPTURE_ESCAPE_TOLERANCE &&
           longitudinal <=
               divideCorridorHalfLength_ + CAPTURE_ESCAPE_TOLERANCE;
}

void GoblinMidBossTactic::issueDivideEngage(
    Room& room, PlatoonLeader& leader) {
    issueStableEngage(room, collectLiveSquads(leader), true);
}

bool GoblinMidBossTactic::updateBossPersonalCombat(
    float dt, Room& room, PlatoonLeader& leader) {
    auto resolveCurrentTarget = [&]() -> Actor* {
        Actor* target = resolveBossPersonalTarget(room, bossPersonalTargetId_);
        if (!target) {
            bossPersonalState_ = BossPersonalState::EvaluateTarget;
            bossPersonalTargetId_ = 0;
        }
        return target;
    };

    if (bossPersonalState_ == BossPersonalState::EvaluateTarget) {
        bossPersonalTargetId_ = selectBossPersonalTarget(room, leader);
        leader.setTacticalTarget(bossPersonalTargetId_);
        bossPersonalTimer_ = 0.f;
        bossTargetEvalTimer_ = BOSS_TARGET_EVAL_INTERVAL;

        if (bossPersonalTargetId_ == 0) {
            leader.transitionTacticalState(TacticalNpcState::Idle,
                                           "Goblin boss no target");
            return true;
        }

        bossPersonalState_ = BossPersonalState::ChaseTarget;
        leader.transitionTacticalState(TacticalNpcState::Chase,
                                       "Goblin boss chase target");
        return true;
    }

    Actor* target = resolveCurrentTarget();
    if (!target)
        return true;

    Vec3 toTarget = target->getPosition() - leader.getPosition();
    float dist = toTarget.length();
    Vec3 dir = (dist > 0.01f) ? toTarget / dist : Vec3{ 1.f, 0.f, 0.f };

    if (bossPersonalState_ == BossPersonalState::ChaseTarget) {
        leader.setTacticalTarget(bossPersonalTargetId_);
        leader.transitionTacticalState(TacticalNpcState::Chase,
                                       "Goblin boss chase target");

        bossTargetEvalTimer_ -= dt;
        if (bossTargetEvalTimer_ <= 0.f) {
            bossTargetEvalTimer_ = BOSS_TARGET_EVAL_INTERVAL;
            BossTargetScore candidate =
                selectBossPersonalTargetScore(room, leader);
            float currentScore = 0.f;
            if (candidate.targetId != 0 &&
                candidate.targetId != bossPersonalTargetId_ &&
                calcBossPersonalTargetScore(room, leader,
                                            bossPersonalTargetId_,
                                            currentScore) &&
                candidate.score > currentScore + BOSS_TARGET_SWITCH_MARGIN) {
                bossPersonalTargetId_ = candidate.targetId;
                leader.setTacticalTarget(bossPersonalTargetId_);
                target = resolveBossPersonalTarget(room, bossPersonalTargetId_);
                if (!target) {
                    bossPersonalState_ = BossPersonalState::EvaluateTarget;
                    bossPersonalTargetId_ = 0;
                    return true;
                }

                toTarget = target->getPosition() - leader.getPosition();
                dist = toTarget.length();
                dir = (dist > 0.01f) ? toTarget / dist : Vec3{ 1.f, 0.f, 0.f };

                char buf[160];
                std::snprintf(buf, sizeof(buf),
                    "Goblin boss retargeted to %s (score %.1f)",
                    target->getName().c_str(), candidate.score);
                Logger::get().log(leader.getName(), buf);
            }
        }

        if (dist <= leader.getAttackRange()) {
            bossPersonalTimer_ = 0.f;
            bossPersonalState_ = BossPersonalState::AttackWindup;
            leader.transitionTacticalState(TacticalNpcState::AttackWindup,
                                           "Goblin boss attack windup");
            return true;
        }

        moveBossToward(leader, target->getPosition(), BOSS_CHASE_SPEED_MULT, dt);
        return true;
    }

    if (bossPersonalState_ == BossPersonalState::AttackWindup) {
        leader.setFacing(dir);
        bossPersonalTimer_ += dt;
        TacticalNpcConfig cfg = leader.getConfig();
        if (bossPersonalTimer_ < cfg.attackWindupTime)
            return true;

        if (dist <= leader.getAttackRange()) {
            target->takeDamage(leader.getAttackDamage());
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "Goblin boss hit %s for %.0f (hp=%.1f)",
                target->getName().c_str(),
                leader.getAttackDamage(),
                target->getHp());
            Logger::get().log(leader.getName(), buf);
        } else {
            Logger::get().log(leader.getName(), "Goblin boss attack missed");
        }

        bossPersonalTimer_ = 0.f;
        bossPersonalState_ = BossPersonalState::AttackRecover;
        leader.transitionTacticalState(TacticalNpcState::AttackRecover,
                                       "Goblin boss attack recover");
        return true;
    }

    if (bossPersonalState_ == BossPersonalState::AttackRecover) {
        leader.setFacing(dir);
        bossPersonalTimer_ += dt;
        TacticalNpcConfig cfg = leader.getConfig();
        if (bossPersonalTimer_ < cfg.attackRecoverTime)
            return true;

        bossPersonalTimer_ = 0.f;
        bossPersonalState_ = BossPersonalState::EvaluateTarget;
        return true;
    }

    return true;
}

uint32_t GoblinMidBossTactic::selectBossPersonalTarget(
    Room& room, const PlatoonLeader& leader) const {
    return selectBossPersonalTargetScore(room, leader).targetId;
}

GoblinMidBossTactic::BossTargetScore
GoblinMidBossTactic::selectBossPersonalTargetScore(
    Room& room, const PlatoonLeader& leader) const {
    std::vector<PlayerCluster> clusters =
        MidBossTacticBase::buildPlayerClusters(room, CLUSTER_RADIUS);
    Vec3 leaderPos = leader.getPosition();
    BossTargetScore best{};
    bool hasBest = false;

    for (const PlayerCluster& cluster : clusters) {
        float clusterBaseScore =
            static_cast<float>(cluster.playerIds.size()) * 1000.f;
        for (uint32_t playerId : cluster.playerIds) {
            auto* player = dynamic_cast<Player*>(room.findActorById(playerId));
            if (!player || !player->isAlive())
                continue;

            float distance = Vec3::distance(leaderPos, player->getPosition());
            float score = clusterBaseScore - distance;
            bool better = !hasBest ||
                score > best.score + 0.001f ||
                (std::fabs(score - best.score) <= 0.001f &&
                 playerId < best.targetId);

            if (!better)
                continue;

            best.targetId = playerId;
            best.score = score;
            hasBest = true;
        }
    }

    return best;
}

bool GoblinMidBossTactic::calcBossPersonalTargetScore(
    Room& room, const PlatoonLeader& leader,
    uint32_t targetId, float& outScore) const {
    if (targetId == 0)
        return false;

    auto* target = dynamic_cast<Player*>(room.findActorById(targetId));
    if (!target || !target->isAlive())
        return false;

    std::vector<PlayerCluster> clusters =
        MidBossTacticBase::buildPlayerClusters(room, CLUSTER_RADIUS);
    Vec3 leaderPos = leader.getPosition();

    for (const PlayerCluster& cluster : clusters) {
        if (std::find(cluster.playerIds.begin(), cluster.playerIds.end(),
                      targetId) == cluster.playerIds.end()) {
            continue;
        }

        float distance = Vec3::distance(leaderPos, target->getPosition());
        outScore = static_cast<float>(cluster.playerIds.size()) * 1000.f -
                   distance;
        return true;
    }

    return false;
}

Actor* GoblinMidBossTactic::resolveBossPersonalTarget(
    Room& room, uint32_t targetId) const {
    if (targetId == 0)
        return nullptr;
    Actor* target = room.findActorById(targetId);
    return (target && target->isAlive()) ? target : nullptr;
}

void GoblinMidBossTactic::moveBossToward(PlatoonLeader& leader,
                                         const Vec3& targetPos,
                                         float speedMult, float dt) const {
    Vec3 toTarget = targetPos - leader.getPosition();
    float dist = toTarget.length();
    if (dist <= 0.01f)
        return;

    Vec3 dir = toTarget / dist;
    leader.setFacing(dir);
    leader.setPosition(leader.getPosition() +
        dir * leader.getLeaderMoveSpeed() * speedMult * dt);
}

float GoblinMidBossTactic::evaluatePlayerScore(
    const Player* p, const PlatoonLeader& leader) const {
    float dist = Vec3::distance(leader.getPosition(), p->getPosition());
    float distScore = 1.f / (1.f + dist);
    float hpScore = 1.f - (p->getHp() / p->getMaxHp());
    return distScore * 0.5f + hpScore * 0.5f;
}

bool GoblinMidBossTactic::allMembersArrived(
    const Room& /*room*/, const PlatoonLeader& leader) const {
    bool anyAlive = false;
    for (auto* sq : leader.getSquads()) {
        if (sq->isEmpty()) continue;
        anyAlive = true;
        if (!sq->areMembersAtSlots()) return false;
    }
    return anyAlive;
}

std::vector<Vec3> GoblinMidBossTactic::calcSquadBoxOffsets(int numSquads) const {
    int rows = static_cast<int>(std::max(1.f,
        std::floorf(std::sqrtf(static_cast<float>(numSquads)))));
    int cols = (numSquads + rows - 1) / rows;

    std::vector<Vec3> offsets;
    offsets.reserve(static_cast<size_t>(numSquads));
    for (int i = 0; i < numSquads; ++i) {
        int col = i % cols;
        int row = i / cols;
        float colOff = (static_cast<float>(col) -
                        static_cast<float>(cols - 1) * 0.5f) * BOX_SQUAD_SPACING;
        float rowOff = (static_cast<float>(row) -
                        static_cast<float>(rows - 1) * 0.5f) * BOX_SQUAD_SPACING;
        float halfCols = static_cast<float>(cols - 1) * 0.5f;
        float latFrac = (cols > 1)
            ? std::abs(static_cast<float>(col) - halfCols) / halfCols
            : 0.f;
        float arcZ = rowOff - BOX_ARC_DEPTH * latFrac;
        offsets.push_back(Vec3{ colOff, 0.f, arcZ });
    }
    return offsets;
}

IsisMidBossTactic::IsisMidBossTactic()
    = default;

void IsisMidBossTactic::update(float dt, Room& room, PlatoonLeader& leader) {
    captureInitialSquadSizes(leader);
    leader.removeDeadMembersFromSquads(room);
    updateBossDamageReaction(dt, leader);

    if (!tacticsUnlocked_ && checkUnlockCondition(leader)) {
        tacticsUnlocked_ = true;
        cooldownTimer_ = 0.f;
        enterPhase(Phase::Cooldown, "Isis tactic unlocked by squad losses", leader);
    }

    if (phase_ == Phase::Engage) {
        if (!engageIssued_) {
            issueEngage(room, leader);
            engageIssued_ = true;
        }
        if (tacticsUnlocked_) {
            cooldownTimer_ -= dt;
            if (cooldownTimer_ <= 0.f && hasLiveBomberSquad(leader)) {
                enterPhase(Phase::RetreatForPincer,
                           "Isis retreating before two-stage wedge strike", leader);
                return;
            }
        }
    } else if (phase_ == Phase::Cooldown) {
        cooldownTimer_ -= dt;
        if (cooldownTimer_ <= 0.f)
            enterPhase(Phase::Engage, "Isis cooldown finished", leader);
    } else if (phase_ == Phase::RetreatForPincer) {
        phaseTimer_ += dt;
        if (!pincerIssued_) {
            issueRetreatForPincer(room, leader);
            pincerIssued_ = true;
        }

        bool leaderAtRetreat =
            Vec3::distance(leader.getPosition(), retreatTargetPos_) <= 1.5f;
        if (leaderAtRetreat && allLiveSquadsAtSlots(room, leader)) {
            enterPhase(Phase::RegroupBombers,
                       "Isis retreat complete - regrouping Bombers", leader);
        } else if (phaseTimer_ >= RETREAT_TIMEOUT) {
            enterPhase(Phase::RegroupBombers,
                       "Isis retreat timeout - regrouping Bombers", leader);
        }
    } else if (phase_ == Phase::RegroupBombers) {
        phaseTimer_ += dt;
        if (!pincerIssued_) {
            issueRegroupBombers(room, leader);
            pincerIssued_ = true;
        }

        if (activeStrikeSquadsAtSlots(room)) {
            enterPhase(Phase::FirstBomberWedge,
                       "Isis Bomber line formed - first WedgeCharge started", leader);
        } else if (phaseTimer_ >= REGROUP_TIMEOUT) {
            enterPhase(Phase::FirstBomberWedge,
                       "Isis Bomber regroup timeout - first WedgeCharge started", leader);
        }
    } else if (phase_ == Phase::FirstBomberWedge) {
        phaseTimer_ += dt;
        if (!pincerIssued_) {
            issueWedgeStrike(room, leader,
                             false, false, true, "first Bomber wedge");
            pincerIssued_ = true;
            if (hasLiveBuddySquad(leader))
                issueRegroupBuddies(room, leader);
        }
        updateActiveStrikeEngage(room, leader, false);

        if (activeStrikeTasksEngaged()) {
            if (hasLiveBuddySquad(leader)) {
                enterPhase(Phase::RegroupBuddies,
                           "Isis first wedge completed - waiting for Buddy boss join", leader);
            } else {
                enterCooldown(leader, "Isis first wedge completed - no live Buddies");
                issueEngage(room, leader);
            }
        } else if (phaseTimer_ >= PINCER_TIMEOUT) {
            updateActiveStrikeEngage(room, leader, true);
            if (hasLiveBuddySquad(leader)) {
                enterPhase(Phase::RegroupBuddies,
                           "Isis first wedge timeout - waiting for Buddy boss join", leader);
            } else {
                enterCooldown(leader, "Isis first wedge timeout - no live Buddies");
                issueEngage(room, leader);
            }
        }
    } else if (phase_ == Phase::RegroupBuddies) {
        phaseTimer_ += dt;
        if (!secondStrikePrepIssued_)
            issueRegroupBuddies(room, leader);

        if (!hasLiveBuddySquad(leader)) {
            enterCooldown(leader, "Isis Buddy join cancelled - no live Buddies");
            issueEngage(room, leader);
            return;
        }

        if (!ensureBossBuddyWedgeJoin(room, leader)) {
            enterCooldown(leader, "Isis Buddy join cancelled - no available boss wedge");
            issueEngage(room, leader);
            return;
        }

        updateBossBuddyWedgeJoin(dt, leader);
        if (isSecondStrikePrepReady(room, leader)) {
            enterPhase(Phase::SecondBuddyWedge,
                       "Isis Buddy line and boss joined - second WedgeCharge started", leader);
        }
    } else if (phase_ == Phase::SecondBuddyWedge) {
        phaseTimer_ += dt;
        if (!pincerIssued_) {
            issueWedgeStrike(room, leader,
                             true, true, false, "second Buddy wedge");
            pincerIssued_ = true;
        }
        syncBossBuddyWedgeChargeStart(leader);
        updateBossBuddyWedgeJoin(dt, leader);
        updateActiveStrikeEngage(room, leader, false);

        if (activeStrikeTasksEngaged() &&
            (!bossBuddyWedgeJoinActive_ || bossBuddyWedgeChargeComplete_)) {
            enterCooldown(leader, "Isis two-stage WedgeStrike completed");
            issueEngage(room, leader);
        } else if (phaseTimer_ >= PINCER_TIMEOUT) {
            updateActiveStrikeEngage(room, leader, true);
            enterCooldown(leader, "Isis second WedgeStrike timeout");
            issueEngage(room, leader);
        }
    }

    if (phase_ == Phase::RegroupBombers ||
        phase_ == Phase::FirstBomberWedge ||
        phase_ == Phase::RegroupBuddies ||
        phase_ == Phase::SecondBuddyWedge) {
        if (bossBuddyWedgeJoinActive_ &&
            bossBuddyWedgeDir_.lengthSq() > 0.01f) {
            leader.setFacing(bossBuddyWedgeDir_);
        }

        if (phase_ == Phase::SecondBuddyWedge && bossBuddyWedgeJoinActive_ &&
            bossBuddyWedgeChargeStarted_ && !bossBuddyWedgeChargeComplete_) {
            leader.transitionTacticalState(TacticalNpcState::ChargeThrough,
                                           "Isis boss joined Buddy wedge");
        } else {
            leader.transitionTacticalState(TacticalNpcState::HoldSlot,
                                           "Isis tactical command");
        }
        return;
    }

    Player* primary = selectPrimaryTarget(room, leader);
    if (!primary)
        return;

    if (phase_ == Phase::RetreatForPincer) {
        leader.transitionTacticalState(TacticalNpcState::Chase,
                                       "Isis tactical retreat");
        Vec3 toRetreat = retreatTargetPos_ - leader.getPosition();
        float retreatDist = toRetreat.length();
        if (retreatDist > 1.f) {
            Vec3 retreatDir = toRetreat / retreatDist;
            leader.setPosition(leader.getPosition() +
                retreatDir * leader.getLeaderMoveSpeed() *
                RETREAT_LEADER_SPEED_MULT * dt);
            leader.setFacing(retreatDir);
        } else {
            Vec3 toPrimary = primary->getPosition() - leader.getPosition();
            if (toPrimary.lengthSq() > 0.01f)
                leader.setFacing(toPrimary.normalized());
        }
        return;
    }

    if (updateBossPersonalCombat(dt, room, leader))
        return;

    Vec3 toPlayer = primary->getPosition() - leader.getPosition();
    float dist = toPlayer.length();
    if (dist <= 0.01f)
        return;

    Vec3 dir = toPlayer / dist;
    if (phase_ == Phase::Engage || phase_ == Phase::Cooldown) {
        if (dist > LEADER_KEEP_DIST + LEADER_KEEP_TOL)
            leader.setPosition(leader.getPosition() +
                dir * leader.getLeaderMoveSpeed() * dt);
        else if (dist < LEADER_KEEP_DIST - LEADER_KEEP_TOL)
            leader.setPosition(leader.getPosition() -
                dir * leader.getLeaderMoveSpeed() * dt);
    }
    leader.setFacing(dir);
}

void IsisMidBossTactic::captureInitialSquadSizes(const PlatoonLeader& leader) {
    if (initialSizesSet_)
        return;

    initialSizesSet_ = true;
    initialSquadSizes_.clear();
    for (auto* squad : leader.getSquads())
        initialSquadSizes_.push_back(
            squad ? static_cast<int>(squad->getMembers().size()) : 0);
}

bool IsisMidBossTactic::checkUnlockCondition(const PlatoonLeader& leader) const {
    const auto& squads = leader.getSquads();
    for (size_t i = 0; i < squads.size(); ++i) {
        int initial = (i < initialSquadSizes_.size())
            ? initialSquadSizes_[i] : 0;
        int current = squads[i]
            ? static_cast<int>(squads[i]->getMembers().size()) : 0;
        if (initial > 0 &&
            static_cast<float>(current) / static_cast<float>(initial) <
                UNLOCK_SQUAD_RATIO) {
            return true;
        }
    }
    return false;
}

void IsisMidBossTactic::enterPhase(Phase next, const char* reason,
                                   PlatoonLeader& leader) {
    Logger::get().log(leader.getName(), reason);
    phase_ = next;
    phaseTimer_ = 0.f;
    buddyRefreshTimer_ = 0.f;
    engageIssued_ = false;
    pincerIssued_ = false;
    activeStrikeSquads_.clear();
    activeStrikeTasks_.clear();
    bool preserveSecondPrep =
        next == Phase::FirstBomberWedge ||
        next == Phase::RegroupBuddies ||
        next == Phase::SecondBuddyWedge;
    if (!preserveSecondPrep) {
        secondStrikePrepIssued_ = false;
        secondStrikePrepSquads_.clear();
        secondStrikeClusters_.clear();
        resetBossBuddyWedgeJoin();
    }
    if (next == Phase::RetreatForPincer || next == Phase::Engage)
        firstStrikeTargetIds_.clear();
    resetBossPersonalCombat(leader,
        (next == Phase::Engage || next == Phase::Cooldown)
            ? "Isis boss personal loop reset"
            : "Isis boss personal paused for squad tactic");
    if (next == Phase::RetreatForPincer) {
        leader.transitionTacticalState(TacticalNpcState::Chase,
                                       "Isis tactical retreat");
    } else if (next == Phase::RegroupBombers ||
               next == Phase::FirstBomberWedge ||
               next == Phase::RegroupBuddies ||
               next == Phase::SecondBuddyWedge) {
        leader.transitionTacticalState(TacticalNpcState::HoldSlot,
                                       "Isis tactical command");
    }
}

void IsisMidBossTactic::enterCooldown(PlatoonLeader& leader, const char* reason) {
    cooldownTimer_ = rollCooldown();
    char msg[128];
    std::snprintf(msg, sizeof(msg), "%s - cooldown %.1fs", reason, cooldownTimer_);
    enterPhase(Phase::Cooldown, msg, leader);
}

void IsisMidBossTactic::issueEngage(Room& room, PlatoonLeader& leader) {
    Player* primary = selectPrimaryTarget(room, leader);
    if (!primary)
        return;

    std::vector<TacticalSquad*> liveSquads = collectLiveSquads(leader);
    if (liveSquads.empty())
        return;

    std::vector<uint32_t> targets(liveSquads.size(), primary->getId());
    assignSquadsToPlayers(room, leader, liveSquads, targets);

    for (size_t i = 0; i < liveSquads.size(); ++i) {
        SquadOrder ord;
        ord.type = SquadOrderType::Engage;
        ord.targetId = targets[i];
        liveSquads[i]->receiveOrder(ord);
    }
}

void IsisMidBossTactic::issueRetreatForPincer(Room& room,
                                              PlatoonLeader& leader) {
    Player* primary = selectPrimaryTarget(room, leader);
    if (!primary) {
        issueEngage(room, leader);
        return;
    }

    Vec3 playerCentroid = calcPlayerCentroid(room, leader.getPosition());
    Vec3 awayDir = leader.getPosition() - playerCentroid;
    float awayLen = awayDir.length();
    if (awayLen > 0.01f)
        awayDir = awayDir / awayLen;
    else
        awayDir = Vec3{ -1.f, 0.f, 0.f };

    float currentBossDist = Vec3::distance(leader.getPosition(), playerCentroid);
    float retreatDist = std::max(currentBossDist + ISIS_RETREAT_EXTRA_DIST,
                                 ISIS_RETREAT_MIN_DIST);
    retreatTargetPos_ = playerCentroid + awayDir * retreatDist;

    Vec3 forward = playerCentroid - retreatTargetPos_;
    float forwardLen = forward.length();
    if (forwardLen > 0.01f)
        forward = forward / forwardLen;
    else
        forward = Vec3{ 1.f, 0.f, 0.f };
    Vec3 right{ -forward.z, 0.f, forward.x };
    if (right.lengthSq() > 0.01f)
        right = right.normalized();
    else
        right = Vec3{ 0.f, 0.f, 1.f };

    const auto& squads = leader.getSquads();
    auto issueRetreatHold = [&](size_t squadIndex, const Vec3& center,
                                float spacingScale, float columnScale,
                                int columnCount) {
        if (squadIndex >= squads.size())
            return;
        TacticalSquad* squad = squads[squadIndex];
        if (!squad || squad->isEmpty())
            return;

        SquadOrder ord;
        ord.type = SquadOrderType::FormationHold;
        ord.targetId = primary->getId();
        ord.tacticCenter = center;
        ord.formationTargetPos = playerCentroid;
        ord.slotSpacingScale = spacingScale;
        ord.slotColumnScale = columnScale;
        ord.slotColumnCount = columnCount;
        ord.speedMult = RETREAT_SPEED_MULT;
        squad->receiveOrder(ord);
    };

    issueRetreatHold(0,
        retreatTargetPos_ - forward * RETREAT_BUDDY_BACK_OFFSET -
            right * RETREAT_BUDDY_SIDE_OFFSET,
        BUDDY_COLUMN_SPACING_SCALE, BUDDY_COLUMN_SCALE, BUDDY_COLUMN_COUNT);
    issueRetreatHold(1,
        retreatTargetPos_ - forward * RETREAT_BUDDY_BACK_OFFSET +
            right * RETREAT_BUDDY_SIDE_OFFSET,
        BUDDY_COLUMN_SPACING_SCALE, BUDDY_COLUMN_SCALE, BUDDY_COLUMN_COUNT);
    issueRetreatHold(2,
        retreatTargetPos_ + forward * RETREAT_BOMBER_FRONT_OFFSET -
            right * RETREAT_BOMBER_SIDE_OFFSET,
        BOMBER_REGROUP_SPACING_SCALE, BOMBER_REGROUP_COLUMN_SCALE,
        BOMBER_REGROUP_COLUMN_COUNT);
    issueRetreatHold(3,
        retreatTargetPos_ + forward * RETREAT_BOMBER_FRONT_OFFSET +
            right * RETREAT_BOMBER_SIDE_OFFSET,
        BOMBER_REGROUP_SPACING_SCALE, BOMBER_REGROUP_COLUMN_SCALE,
        BOMBER_REGROUP_COLUMN_COUNT);
}

void IsisMidBossTactic::issueRegroupBombers(Room& room,
                                            PlatoonLeader& leader) {
    std::vector<StrikeCluster> clusters = selectStrikeClusters(room, leader);
    if (clusters.empty()) {
        issueEngage(room, leader);
        return;
    }

    const auto& squads = leader.getSquads();
    TacticalSquad* bomberSquads[2] = {
        squads.size() > 2 ? squads[2] : nullptr,
        squads.size() > 3 ? squads[3] : nullptr
    };

    activeStrikeSquads_.clear();
    int assignedBomber = 0;
    for (int i = 0; i < 2; ++i) {
        TacticalSquad* bomber = bomberSquads[i];
        if (!bomber || bomber->isEmpty())
            continue;

        int clusterIdx = (clusters.size() == 1)
            ? 0
            : std::min(assignedBomber, 1);
        issueBomberRegroup(room, bomber, clusters[clusterIdx],
                           assignedBomber == 0 ? -1.f : 1.f);
        activeStrikeSquads_.push_back(bomber);
        ++assignedBomber;
    }
}

void IsisMidBossTactic::issueRegroupBuddies(Room& room,
                                            PlatoonLeader& leader) {
    if (secondStrikePrepIssued_)
        return;

    std::vector<StrikeCluster> clusters =
        selectStrikeClusters(room, leader, true);
    if (clusters.empty()) {
        issueEngage(room, leader);
        return;
    }

    secondStrikePrepIssued_ = true;
    secondStrikePrepSquads_.clear();
    secondStrikeClusters_ = clusters;

    const auto& squads = leader.getSquads();
    TacticalSquad* buddySquads[2] = {
        squads.size() > 0 ? squads[0] : nullptr,
        squads.size() > 1 ? squads[1] : nullptr
    };

    for (int i = 0; i < 2; ++i) {
        if (!buddySquads[i] || buddySquads[i]->isEmpty())
            continue;
        int clusterIdx = (clusters.size() == 1) ? 0 : i;
        issueBuddyColumn(room, buddySquads[i], clusters[clusterIdx],
                         i == 0 ? -1.f : 1.f);
        secondStrikePrepSquads_.push_back(buddySquads[i]);
    }
}

void IsisMidBossTactic::issueWedgeStrike(Room& room, PlatoonLeader& leader,
                                         bool useBuddySquads,
                                         bool applyRepeatPenalty,
                                         bool rememberTargets,
                                         const char* strikeLabel) {
    std::vector<StrikeCluster> clusters =
        (useBuddySquads && !secondStrikeClusters_.empty())
            ? secondStrikeClusters_
            : selectStrikeClusters(room, leader, applyRepeatPenalty);
    if (clusters.empty()) {
        if (useBuddySquads)
            resetBossBuddyWedgeJoin();
        issueEngage(room, leader);
        return;
    }

    const auto& squads = leader.getSquads();
    TacticalSquad* strikeSquads[2] = {
        useBuddySquads
            ? (squads.size() > 0 ? squads[0] : nullptr)
            : (squads.size() > 2 ? squads[2] : nullptr),
        useBuddySquads
            ? (squads.size() > 1 ? squads[1] : nullptr)
            : (squads.size() > 3 ? squads[3] : nullptr)
    };

    activeStrikeSquads_.clear();
    activeStrikeTasks_.clear();
    if (rememberTargets)
        firstStrikeTargetIds_.clear();
    if (useBuddySquads) {
        bossBuddyWedgeChargeStarted_ = false;
        bossBuddyWedgeChargeComplete_ = false;
    }

    int assignedSquad = 0;
    bool bossJoinedStrikeIssued = false;
    for (int i = 0; i < 2; ++i) {
        TacticalSquad* squad = strikeSquads[i];
        if (!squad || squad->isEmpty())
            continue;

        int clusterIdx = 0;
        if (clusters.size() > 1)
            clusterIdx = std::min(assignedSquad, 1);

        const StrikeCluster& strikeCluster = clusters[clusterIdx];
        SquadOrder ord;
        // Isis squads intentionally reuse the same shared WedgeCharge execution
        // path as Goblin: TacticalSquad prepares the wedge and TacticalNpc runs
        // ChargeThrough with the common impact/damage rules.
        ord.type = SquadOrderType::WedgeCharge;
        ord.targetId = strikeCluster.cluster.representativeId;
        ord.targetIds = strikeCluster.cluster.playerIds;
        ord.tacticCenter = strikeCluster.cluster.centroid;
        ord.chargeSpeedMult = ISIS_WEDGE_SPEED_MULT;
        if (useBuddySquads) {
            ord.wedgeSpacingMult = ISIS_BUDDY_WEDGE_SPACING_MULT;
            if (isBossJoinedBuddySquad(squad)) {
                ord.wedgeDamageMult = ISIS_BOSS_JOINED_WEDGE_DAMAGE_MULT;
                ord.reserveWedgeApex = true;
                setupBossBuddyWedgeJoin(room, squad, strikeCluster,
                                        squad->calcCentroid());
                bossBuddyWedgeChargeComplete_ = false;
                bossJoinedStrikeIssued = true;
            }
        }
        squad->receiveOrder(ord);
        activeStrikeSquads_.push_back(squad);
        activeStrikeTasks_.push_back(
            StrikeTask{ squad, strikeCluster.cluster.playerIds, false });

        if (rememberTargets) {
            for (uint32_t playerId : strikeCluster.cluster.playerIds) {
                if (std::find(firstStrikeTargetIds_.begin(),
                              firstStrikeTargetIds_.end(),
                              playerId) == firstStrikeTargetIds_.end()) {
                    firstStrikeTargetIds_.push_back(playerId);
                }
            }
        }

        std::string msg = std::string("Isis ") + strikeLabel +
            " target rep=" +
            std::to_string(strikeCluster.cluster.representativeId) +
            " players=";
        for (size_t idIdx = 0; idIdx < strikeCluster.cluster.playerIds.size();
             ++idIdx) {
            if (idIdx > 0)
                msg += ",";
            msg += std::to_string(strikeCluster.cluster.playerIds[idIdx]);
        }
        Logger::get().log(leader.getName(), msg);

        ++assignedSquad;
    }

    if (useBuddySquads && bossBuddyWedgeJoinActive_ &&
        !bossJoinedStrikeIssued) {
        resetBossBuddyWedgeJoin();
    }
}

void IsisMidBossTactic::issueBomberRegroup(Room& room, TacticalSquad* squad,
                                           const StrikeCluster& strikeCluster,
                                           float sideSign) {
    if (!squad || squad->isEmpty())
        return;

    Vec3 fallbackDir = strikeCluster.cluster.centroid - squad->calcCentroid();
    if (fallbackDir.lengthSq() <= 0.01f)
        fallbackDir = Vec3{ 1.f, 0.f, 0.f };

    Vec3 playerFacing = calcAveragePlayerFacing(room, fallbackDir);
    if (playerFacing.lengthSq() <= 0.01f)
        playerFacing = fallbackDir.normalized();

    Vec3 forward = playerFacing.normalized();
    Vec3 right{ -forward.z, 0.f, forward.x };
    if (right.lengthSq() <= 0.01f)
        right = Vec3{ 0.f, 0.f, 1.f };
    else
        right = right.normalized();

    Vec3 center = strikeCluster.cluster.centroid
        - forward * BOMBER_REGROUP_BACK_OFFSET
        + right * (BOMBER_REGROUP_SIDE_OFFSET * sideSign);

    SquadOrder ord;
    ord.type = SquadOrderType::FormationHold;
    ord.targetId = strikeCluster.cluster.representativeId;
    ord.tacticCenter = center;
    ord.formationTargetPos = strikeCluster.cluster.centroid;
    ord.slotSpacingScale = BOMBER_REGROUP_SPACING_SCALE;
    ord.slotColumnScale = BOMBER_REGROUP_COLUMN_SCALE;
    ord.slotColumnCount = BOMBER_REGROUP_COLUMN_COUNT;
    ord.speedMult = BOMBER_REGROUP_SPEED_MULT;
    squad->receiveOrder(ord);
}

Vec3 IsisMidBossTactic::issueBuddyColumn(Room& room, TacticalSquad* squad,
                                         const StrikeCluster& strikeCluster,
                                         float sideSign) {
    if (!squad || squad->isEmpty())
        return {};

    Vec3 attackDir = strikeCluster.cluster.centroid - retreatTargetPos_;
    if (attackDir.lengthSq() > 0.01f)
        attackDir = attackDir.normalized();
    else
        attackDir = Vec3{ 1.f, 0.f, 0.f };

    Vec3 right{ -attackDir.z, 0.f, attackDir.x };
    if (right.lengthSq() <= 0.01f)
        right = Vec3{ 0.f, 0.f, 1.f };
    else
        right = right.normalized();

    Vec3 center = retreatTargetPos_
        + attackDir * RETREAT_BOMBER_FRONT_OFFSET
        + right * (RETREAT_BUDDY_SIDE_OFFSET * sideSign);

    SquadOrder ord;
    ord.type = SquadOrderType::FormationHold;
    ord.targetId = strikeCluster.cluster.representativeId;
    ord.tacticCenter = center;
    ord.formationTargetPos = strikeCluster.cluster.centroid;
    ord.slotSpacingScale = BUDDY_COLUMN_SPACING_SCALE;
    ord.slotColumnScale = BUDDY_COLUMN_SCALE;
    ord.slotColumnCount = BUDDY_COLUMN_COUNT;
    ord.speedMult = BUDDY_SPEED_MULT;
    squad->receiveOrder(ord);
    return center;
}

void IsisMidBossTactic::selectBossJoinedBuddySquad(
    const PlatoonLeader& leader) {
    resetBossBuddyWedgeJoin();

    const auto& squads = leader.getSquads();
    std::vector<int> candidates;
    for (int i = 0; i < 2; ++i) {
        if (static_cast<size_t>(i) >= squads.size())
            continue;
        TacticalSquad* squad = squads[static_cast<size_t>(i)];
        if (squad && !squad->isEmpty())
            candidates.push_back(i);
    }

    if (candidates.empty())
        return;

    std::uniform_int_distribution<int> dist(
        0, static_cast<int>(candidates.size()) - 1);
    bossJoinedBuddySquadIndex_ = candidates[dist(isisCooldownRng())];
}

void IsisMidBossTactic::resetBossBuddyWedgeJoin() {
    bossJoinedBuddySquadIndex_ = -1;
    bossBuddyWedgeJoinActive_ = false;
    bossBuddyWedgeChargeStarted_ = false;
    bossBuddyWedgeChargeComplete_ = false;
    bossBuddyWedgePreparePos_ = {};
    bossBuddyWedgeExitPos_ = {};
    bossBuddyWedgeDir_ = Vec3{ 1.f, 0.f, 0.f };
}

bool IsisMidBossTactic::isBossJoinedBuddySquad(
    const TacticalSquad* squad) const {
    return squad && bossJoinedBuddySquadIndex_ >= 0 &&
        squad->getSquadId() == bossJoinedBuddySquadIndex_;
}

bool IsisMidBossTactic::ensureBossBuddyWedgeJoin(
    Room& room, const PlatoonLeader& leader) {
    const auto& squads = leader.getSquads();
    if (bossBuddyWedgeJoinActive_) {
        bool selectedAlive = false;
        for (int i = 0; i < 2; ++i) {
            if (static_cast<size_t>(i) >= squads.size())
                continue;
            TacticalSquad* squad = squads[static_cast<size_t>(i)];
            if (squad && !squad->isEmpty() &&
                isBossJoinedBuddySquad(squad)) {
                selectedAlive = true;
                break;
            }
        }
        if (selectedAlive)
            return true;
        resetBossBuddyWedgeJoin();
    }

    if (secondStrikeClusters_.empty())
        return false;

    if (bossJoinedBuddySquadIndex_ < 0)
        selectBossJoinedBuddySquad(leader);
    if (bossJoinedBuddySquadIndex_ < 0)
        return false;

    bool selectedAlive = false;
    for (int i = 0; i < 2; ++i) {
        if (static_cast<size_t>(i) >= squads.size())
            continue;
        TacticalSquad* squad = squads[static_cast<size_t>(i)];
        if (squad && !squad->isEmpty() && isBossJoinedBuddySquad(squad)) {
            selectedAlive = true;
            break;
        }
    }
    if (!selectedAlive) {
        selectBossJoinedBuddySquad(leader);
        if (bossJoinedBuddySquadIndex_ < 0)
            return false;
    }

    for (int i = 0; i < 2; ++i) {
        if (static_cast<size_t>(i) >= squads.size())
            continue;

        TacticalSquad* squad = squads[static_cast<size_t>(i)];
        if (!squad || squad->isEmpty() || !isBossJoinedBuddySquad(squad))
            continue;

        if (!squad->areMembersAtSlots())
            return true;

        int clusterIdx = 0;
        if (secondStrikeClusters_.size() > 1)
            clusterIdx = std::min(i, 1);

        setupBossBuddyWedgeJoin(
            room, squad, secondStrikeClusters_[static_cast<size_t>(clusterIdx)],
            squad->calcCentroid());
        return bossBuddyWedgeJoinActive_;
    }

    resetBossBuddyWedgeJoin();
    return false;
}

bool IsisMidBossTactic::isBossBuddyWedgeJoinReady(
    const PlatoonLeader& leader) const {
    if (!bossBuddyWedgeJoinActive_)
        return bossJoinedBuddySquadIndex_ < 0;
    if (bossBuddyWedgeChargeStarted_)
        return true;
    return Vec3::distance(leader.getPosition(), bossBuddyWedgePreparePos_) <=
        ISIS_BOSS_WEDGE_JOIN_READY_DIST;
}

bool IsisMidBossTactic::areSecondStrikePrepSquadsAtSlots(Room& room) const {
    if (secondStrikePrepSquads_.empty())
        return false;

    for (TacticalSquad* squad : secondStrikePrepSquads_) {
        if (squad && !squad->isEmpty() && !squad->areMembersAtSlots())
            return false;
    }
    return true;
}

bool IsisMidBossTactic::isSecondStrikePrepReady(
    Room& room, const PlatoonLeader& leader) const {
    return secondStrikePrepIssued_ &&
        areSecondStrikePrepSquadsAtSlots(room) &&
        isBossBuddyWedgeJoinReady(leader);
}

void IsisMidBossTactic::setupBossBuddyWedgeJoin(
    Room& /*room*/, TacticalSquad* squad,
    const StrikeCluster& strikeCluster, const Vec3& squadCenter) {
    if (!isBossJoinedBuddySquad(squad))
        return;

    Vec3 forward = strikeCluster.cluster.centroid - squadCenter;
    if (forward.lengthSq() > 0.01f)
        forward = forward.normalized();
    else
        forward = Vec3{ 1.f, 0.f, 0.f };

    bossBuddyWedgeJoinActive_ = true;
    bossBuddyWedgeDir_ = forward;
    bossBuddyWedgePreparePos_ =
        squadCenter + forward * TacticalSquad::WEDGE_PREP_APEX_DISTANCE;
    bossBuddyWedgeExitPos_ =
        strikeCluster.cluster.centroid +
        forward * TacticalSquad::WEDGE_EXIT_DISTANCE;
}

void IsisMidBossTactic::syncBossBuddyWedgeChargeStart(
    const PlatoonLeader& leader) {
    if (!bossBuddyWedgeJoinActive_ || bossBuddyWedgeChargeStarted_ ||
        bossJoinedBuddySquadIndex_ < 0)
        return;

    const auto& squads = leader.getSquads();
    for (int i = 0; i < 2; ++i) {
        if (static_cast<size_t>(i) >= squads.size())
            continue;

        TacticalSquad* squad = squads[static_cast<size_t>(i)];
        if (!squad || !isBossJoinedBuddySquad(squad))
            continue;

        if (squad->isWedgeChargeActive())
            bossBuddyWedgeChargeStarted_ = true;
        return;
    }
}

void IsisMidBossTactic::updateBossBuddyWedgeJoin(
    float dt, PlatoonLeader& leader) {
    if (!bossBuddyWedgeJoinActive_)
        return;

    Vec3 targetPos = bossBuddyWedgeChargeStarted_
        ? bossBuddyWedgeExitPos_
        : bossBuddyWedgePreparePos_;
    float arriveDist = bossBuddyWedgeChargeStarted_ ? 0.75f : 1.0f;

    Vec3 toTarget = targetPos - leader.getPosition();
    float dist = toTarget.length();
    if (dist <= arriveDist) {
        if (bossBuddyWedgeChargeStarted_)
            bossBuddyWedgeChargeComplete_ = true;
        if (bossBuddyWedgeDir_.lengthSq() > 0.01f)
            leader.setFacing(bossBuddyWedgeDir_);
        return;
    }

    float speedMult = bossBuddyWedgeChargeStarted_
        ? ISIS_BOSS_WEDGE_CHARGE_SPEED_MULT
        : ISIS_BOSS_WEDGE_JOIN_SPEED_MULT;
    moveBossToward(leader, targetPos, speedMult, dt);
}

std::vector<IsisMidBossTactic::StrikeCluster>
IsisMidBossTactic::selectStrikeClusters(const Room& room,
                                        const PlatoonLeader& leader,
                                        bool applyRepeatPenalty) const {
    std::vector<PlayerCluster> baseClusters =
        buildPlayerClusters(room, CLUSTER_RADIUS);
    std::vector<StrikeCluster> result;
    result.reserve(baseClusters.size());

    Vec3 leaderPos = leader.getPosition();
    for (PlayerCluster& cluster : baseClusters) {
        if (cluster.playerIds.empty())
            continue;

        if (cluster.representativeId == 0)
            cluster.representativeId = cluster.playerIds.front();

        float distance = Vec3::distance(cluster.centroid, leaderPos);
        bool overlapsFirstStrike = false;
        if (applyRepeatPenalty) {
            for (uint32_t playerId : cluster.playerIds) {
                if (std::find(firstStrikeTargetIds_.begin(),
                              firstStrikeTargetIds_.end(),
                              playerId) != firstStrikeTargetIds_.end()) {
                    overlapsFirstStrike = true;
                    break;
                }
            }
        }

        float score = static_cast<float>(cluster.playerIds.size()) * 1000.f -
                      distance;
        if (overlapsFirstStrike)
            score -= SECOND_STRIKE_REPEAT_PENALTY;
        cluster.score = score;
        result.push_back({ cluster, score });
    }

    std::sort(result.begin(), result.end(),
        [](const StrikeCluster& a, const StrikeCluster& b) {
            if (a.cluster.playerIds.size() != b.cluster.playerIds.size())
                return a.cluster.playerIds.size() > b.cluster.playerIds.size();
            float aDistRank = -a.score;
            float bDistRank = -b.score;
            if (std::fabs(aDistRank - bDistRank) > 0.001f)
                return a.score > b.score;
            return a.cluster.representativeId < b.cluster.representativeId;
        });

    if (result.size() > 2)
        result.resize(2);
    return result;
}

Player* IsisMidBossTactic::selectPrimaryTarget(
    Room& room, const PlatoonLeader& leader) const {
    std::vector<StrikeCluster> clusters = selectStrikeClusters(room, leader);
    if (!clusters.empty()) {
        auto* p = dynamic_cast<Player*>(
            room.findActorById(clusters.front().cluster.representativeId));
        if (p && p->isAlive())
            return p;
    }
    return selectNearestPlayer(room, leader.getPosition());
}

uint32_t IsisMidBossTactic::selectStrikeEngageTarget(
    Room& room, const PlatoonLeader& leader, const StrikeTask& task) const {
    Vec3 from = task.squad ? task.squad->calcCentroid()
                           : leader.getPosition();

    uint32_t bestTargetId = 0;
    float bestDistSq = -1.f;
    for (uint32_t targetId : task.targetIds) {
        auto* player = dynamic_cast<Player*>(room.findActorById(targetId));
        if (!player || !player->isAlive())
            continue;

        float distSq = Vec3::distanceSq(from, player->getPosition());
        if (bestDistSq < 0.f || distSq < bestDistSq) {
            bestDistSq = distSq;
            bestTargetId = targetId;
        }
    }

    if (bestTargetId != 0)
        return bestTargetId;

    Player* fallback = selectPrimaryTarget(room, leader);
    return fallback ? fallback->getId() : 0;
}

void IsisMidBossTactic::updateActiveStrikeEngage(
    Room& room, PlatoonLeader& leader, bool forceAll) {
    for (StrikeTask& task : activeStrikeTasks_) {
        if (task.engageIssued)
            continue;

        if (!task.squad || task.squad->isEmpty()) {
            task.engageIssued = true;
            continue;
        }

        if (!forceAll && !task.squad->areChargeMembersComplete())
            continue;

        task.squad->endActiveWedgeCharge(room);

        uint32_t targetId = selectStrikeEngageTarget(room, leader, task);
        if (targetId != 0) {
            SquadOrder ord;
            ord.type = SquadOrderType::Engage;
            ord.targetId = targetId;
            task.squad->receiveOrder(ord);

            Logger::get().log(leader.getName(),
                "Isis strike squad returned to Engage target=" +
                std::to_string(targetId));
        }
        task.engageIssued = true;
    }
}

void IsisMidBossTactic::updateBossDamageReaction(
    float dt, const PlatoonLeader& leader) {
    if (!bossHpTracked_) {
        bossHpTracked_ = true;
        previousBossHp_ = leader.getHp();
        return;
    }

    float currentHp = leader.getHp();
    float damageDelta = previousBossHp_ - currentHp;
    if (damageDelta > 0.f)
        bossDamageSinceBackstep_ += damageDelta;
    previousBossHp_ = currentHp;

    if (bossBackstepCooldownTimer_ > 0.f)
        bossBackstepCooldownTimer_ =
            std::max(0.f, bossBackstepCooldownTimer_ - dt);
}

bool IsisMidBossTactic::updateBossPersonalCombat(
    float dt, Room& room, PlatoonLeader& leader) {
    if (phase_ != Phase::Engage && phase_ != Phase::Cooldown)
        return false;

    if (bossPersonalState_ != BossPersonalState::Backstep &&
        bossPersonalState_ != BossPersonalState::Retreat &&
        bossDamageSinceBackstep_ >= BOSS_DAMAGE_REACTION_THRESHOLD &&
        bossBackstepCooldownTimer_ <= 0.f) {
        beginBossBackstep(room, leader);
        return true;
    }

    auto resolveCurrentTarget = [&]() -> Actor* {
        Actor* target = resolveBossPersonalTarget(room, bossPersonalTargetId_);
        if (!target) {
            bossPersonalState_ = BossPersonalState::EvaluateTarget;
            bossPersonalTargetId_ = 0;
        }
        return target;
    };

    if (bossPersonalState_ == BossPersonalState::EvaluateTarget) {
        bossPersonalTargetId_ = selectBossPersonalTarget(room, leader);
        leader.setTacticalTarget(bossPersonalTargetId_);
        bossPersonalTimer_ = 0.f;
        bossTargetEvalTimer_ = BOSS_TARGET_EVAL_INTERVAL;
        if (bossPersonalTargetId_ == 0) {
            leader.transitionTacticalState(TacticalNpcState::Idle,
                                           "Isis boss no target");
            return true;
        }

        bossPersonalState_ = BossPersonalState::ChaseTarget;
        leader.transitionTacticalState(TacticalNpcState::Chase,
                                       "Isis boss chase target");
        return true;
    }

    Actor* target = resolveCurrentTarget();
    if (!target)
        return true;

    Vec3 toTarget = target->getPosition() - leader.getPosition();
    float dist = toTarget.length();
    Vec3 dir = (dist > 0.01f) ? toTarget / dist : Vec3{ 1.f, 0.f, 0.f };

    if (bossPersonalState_ == BossPersonalState::ChaseTarget) {
        leader.setTacticalTarget(bossPersonalTargetId_);
        leader.transitionTacticalState(TacticalNpcState::Chase,
                                       "Isis boss chase target");
        bossTargetEvalTimer_ -= dt;
        if (bossTargetEvalTimer_ <= 0.f) {
            bossTargetEvalTimer_ = BOSS_TARGET_EVAL_INTERVAL;
            BossTargetScore candidate =
                selectBossPersonalTargetScore(room, leader);
            float currentScore = 0.f;
            if (candidate.targetId != 0 &&
                candidate.targetId != bossPersonalTargetId_ &&
                calcBossPersonalTargetScore(room, leader,
                                            bossPersonalTargetId_,
                                            currentScore) &&
                candidate.score > currentScore + BOSS_TARGET_SWITCH_MARGIN) {
                bossPersonalTargetId_ = candidate.targetId;
                leader.setTacticalTarget(bossPersonalTargetId_);
                target = resolveBossPersonalTarget(room, bossPersonalTargetId_);
                if (!target) {
                    bossPersonalState_ = BossPersonalState::EvaluateTarget;
                    bossPersonalTargetId_ = 0;
                    return true;
                }

                toTarget = target->getPosition() - leader.getPosition();
                dist = toTarget.length();
                dir = (dist > 0.01f) ? toTarget / dist
                                     : Vec3{ 1.f, 0.f, 0.f };

                char buf[160];
                std::snprintf(buf, sizeof(buf),
                    "Isis boss retargeted to %s (score %.1f)",
                    target->getName().c_str(), candidate.score);
                Logger::get().log(leader.getName(), buf);
            }
        }

        if (dist <= leader.getAttackRange()) {
            bossPersonalTimer_ = 0.f;
            bossPersonalState_ = BossPersonalState::AttackWindup;
            leader.transitionTacticalState(TacticalNpcState::AttackWindup,
                                           "Isis boss attack windup");
            return true;
        }

        moveBossToward(leader, target->getPosition(), BOSS_CHASE_SPEED_MULT, dt);
        return true;
    }

    if (bossPersonalState_ == BossPersonalState::AttackWindup) {
        leader.setFacing(dir);
        bossPersonalTimer_ += dt;
        TacticalNpcConfig cfg = leader.getConfig();
        if (bossPersonalTimer_ < cfg.attackWindupTime)
            return true;

        if (dist <= leader.getAttackRange()) {
            target->takeDamage(leader.getAttackDamage());
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "Isis boss hit %s for %.0f (hp=%.1f)",
                target->getName().c_str(),
                leader.getAttackDamage(),
                target->getHp());
            Logger::get().log(leader.getName(), buf);
        } else {
            Logger::get().log(leader.getName(), "Isis boss attack missed");
        }

        bossPersonalTimer_ = 0.f;
        bossPersonalState_ = BossPersonalState::AttackRecover;
        leader.transitionTacticalState(TacticalNpcState::AttackRecover,
                                       "Isis boss attack recover");
        return true;
    }

    if (bossPersonalState_ == BossPersonalState::AttackRecover) {
        leader.setFacing(dir);
        bossPersonalTimer_ += dt;
        TacticalNpcConfig cfg = leader.getConfig();
        if (bossPersonalTimer_ < cfg.attackRecoverTime)
            return true;

        bossPersonalTimer_ = 0.f;
        bossPersonalState_ = BossPersonalState::EvaluateTarget;
        return true;
    }

    if (bossPersonalState_ == BossPersonalState::Backstep) {
        leader.transitionTacticalState(TacticalNpcState::Chase,
                                       "Isis boss backstep");
        Vec3 toBackstep = bossBackstepTargetPos_ - leader.getPosition();
        float backstepDist = toBackstep.length();
        if (backstepDist <= 1.f) {
            bossPersonalState_ = BossPersonalState::Retreat;
            return true;
        }

        Vec3 backstepDir = toBackstep / backstepDist;
        leader.setPosition(leader.getPosition() +
            backstepDir * leader.getLeaderMoveSpeed() *
            BOSS_BACKSTEP_SPEED_MULT * dt);
        leader.setFacing(dir);
        return true;
    }

    if (bossPersonalState_ == BossPersonalState::Retreat) {
        leader.transitionTacticalState(TacticalNpcState::Chase,
                                       "Isis boss retreat");
        if (dist >= BOSS_RETREAT_DIST) {
            bossPersonalTimer_ = 0.f;
            bossPersonalState_ = BossPersonalState::EvaluateTarget;
            return true;
        }

        Vec3 awayDir = (dist > 0.01f) ? dir * -1.f : Vec3{ -1.f, 0.f, 0.f };
        leader.setPosition(leader.getPosition() +
            awayDir * leader.getLeaderMoveSpeed() *
            BOSS_RETREAT_SPEED_MULT * dt);
        leader.setFacing(dir);
        return true;
    }

    return true;
}

void IsisMidBossTactic::resetBossPersonalCombat(PlatoonLeader& leader,
                                                const char* reason) {
    bossPersonalState_ = BossPersonalState::EvaluateTarget;
    bossPersonalTimer_ = 0.f;
    bossTargetEvalTimer_ = 0.f;
    bossPersonalTargetId_ = 0;
    leader.setTacticalTarget(0);
    leader.transitionTacticalState(TacticalNpcState::Idle, reason);
}

uint32_t IsisMidBossTactic::selectBossPersonalTarget(
    Room& room, const PlatoonLeader& leader) const {
    return selectBossPersonalTargetScore(room, leader).targetId;
}

IsisMidBossTactic::BossTargetScore
IsisMidBossTactic::selectBossPersonalTargetScore(
    Room& room, const PlatoonLeader& leader) const {
    std::vector<PlayerCluster> clusters = buildPlayerClusters(room, CLUSTER_RADIUS);
    if (clusters.empty())
        return {};

    Vec3 leaderPos = leader.getPosition();
    BossTargetScore best{};
    bool hasBest = false;

    for (const PlayerCluster& cluster : clusters) {
        float clusterBaseScore =
            static_cast<float>(cluster.playerIds.size()) * 1000.f;
        for (uint32_t playerId : cluster.playerIds) {
            auto* player = dynamic_cast<Player*>(room.findActorById(playerId));
            if (!player || !player->isAlive())
                continue;

            float distance = Vec3::distance(leaderPos, player->getPosition());
            float score = clusterBaseScore - distance;
            bool better = !hasBest ||
                score > best.score + 0.001f ||
                (std::fabs(score - best.score) <= 0.001f &&
                 playerId < best.targetId);

            if (!better)
                continue;

            best.targetId = playerId;
            best.score = score;
            hasBest = true;
        }
    }

    return best;
}

bool IsisMidBossTactic::calcBossPersonalTargetScore(
    Room& room, const PlatoonLeader& leader,
    uint32_t targetId, float& outScore) const {
    if (targetId == 0)
        return false;

    auto* target = dynamic_cast<Player*>(room.findActorById(targetId));
    if (!target || !target->isAlive())
        return false;

    std::vector<PlayerCluster> clusters = buildPlayerClusters(room, CLUSTER_RADIUS);
    Vec3 leaderPos = leader.getPosition();
    for (const PlayerCluster& cluster : clusters) {
        if (std::find(cluster.playerIds.begin(), cluster.playerIds.end(),
                      targetId) == cluster.playerIds.end()) {
            continue;
        }

        float distance = Vec3::distance(leaderPos, target->getPosition());
        outScore = static_cast<float>(cluster.playerIds.size()) * 1000.f -
                   distance;
        return true;
    }

    return false;
}

Actor* IsisMidBossTactic::resolveBossPersonalTarget(
    Room& room, uint32_t targetId) const {
    if (targetId == 0)
        return nullptr;
    Actor* target = room.findActorById(targetId);
    return (target && target->isAlive()) ? target : nullptr;
}

void IsisMidBossTactic::beginBossBackstep(Room& room, PlatoonLeader& leader) {
    if (bossPersonalTargetId_ == 0)
        bossPersonalTargetId_ = selectBossPersonalTarget(room, leader);

    Actor* target = resolveBossPersonalTarget(room, bossPersonalTargetId_);
    Vec3 awayDir{ -1.f, 0.f, 0.f };
    if (target) {
        Vec3 fromTarget = leader.getPosition() - target->getPosition();
        if (fromTarget.lengthSq() > 0.01f)
            awayDir = fromTarget.normalized();
    }

    bossBackstepTargetPos_ =
        leader.getPosition() + awayDir * BOSS_BACKSTEP_DIST;
    bossDamageSinceBackstep_ = 0.f;
    bossBackstepCooldownTimer_ = BOSS_BACKSTEP_COOLDOWN;
    bossPersonalTimer_ = 0.f;
    bossPersonalState_ = BossPersonalState::Backstep;
    leader.setTacticalTarget(bossPersonalTargetId_);
    leader.transitionTacticalState(TacticalNpcState::Chase,
                                   "Isis boss damage backstep");
}

void IsisMidBossTactic::moveBossToward(PlatoonLeader& leader,
                                       const Vec3& targetPos,
                                       float speedMult, float dt) const {
    Vec3 toTarget = targetPos - leader.getPosition();
    float dist = toTarget.length();
    if (dist <= 0.01f)
        return;

    Vec3 dir = toTarget / dist;
    leader.setFacing(dir);
    leader.setPosition(leader.getPosition() +
        dir * leader.getLeaderMoveSpeed() * speedMult * dt);
}

float IsisMidBossTactic::rollCooldown() {
    std::uniform_real_distribution<float> dist(MIN_COOLDOWN, MAX_COOLDOWN);
    return dist(isisCooldownRng());
}

bool IsisMidBossTactic::hasLiveBomberSquad(const PlatoonLeader& leader) const {
    const auto& squads = leader.getSquads();
    for (size_t i = 2; i < squads.size() && i < 4; ++i) {
        if (squads[i] && !squads[i]->isEmpty())
            return true;
    }
    return false;
}

bool IsisMidBossTactic::hasLiveBuddySquad(const PlatoonLeader& leader) const {
    const auto& squads = leader.getSquads();
    for (size_t i = 0; i < squads.size() && i < 2; ++i) {
        if (squads[i] && !squads[i]->isEmpty())
            return true;
    }
    return false;
}

bool IsisMidBossTactic::allLiveSquadsAtSlots(
    Room& room, const PlatoonLeader& leader) const {
    bool anyLiveSquad = false;
    for (TacticalSquad* squad : leader.getSquads()) {
        if (!squad || squad->isEmpty())
            continue;
        anyLiveSquad = true;
        if (!squad->areMembersAtSlots())
            return false;
    }
    return anyLiveSquad;
}

bool IsisMidBossTactic::activeStrikeSquadsAtSlots(Room& room) const {
    if (activeStrikeSquads_.empty())
        return true;
    for (TacticalSquad* squad : activeStrikeSquads_) {
        if (squad && !squad->isEmpty() && !squad->areMembersAtSlots())
            return false;
    }
    return true;
}

bool IsisMidBossTactic::activeStrikeTasksEngaged() const {
    if (activeStrikeTasks_.empty())
        return true;
    for (const StrikeTask& task : activeStrikeTasks_) {
        if (!task.engageIssued)
            return false;
    }
    return true;
}

GrandBaumMidBossTactic::GrandBaumMidBossTactic() = default;

void GrandBaumMidBossTactic::update(float dt, Room& room, PlatoonLeader& leader) {
    TacticalSquad* originalSnakeSquadForRoster = leader.getSquads().size() >= 4
        ? leader.getSquads()[3]
        : nullptr;
    captureOriginalSnakeRoster(room, originalSnakeSquadForRoster);
    leader.removeDeadMembersFromSquads(room);

    const auto& squads = leader.getSquads();
    if (squads.empty())
        return;

    uint32_t targetId = selectNearestPlayerId(room, leader.getPosition());
    Actor* target = targetId != 0 ? room.findActorById(targetId) : nullptr;
    if (target) {
        Vec3 dir = target->getPosition() - leader.getPosition();
        if (dir.length() > 0.1f)
            leader.setFacing(dir.normalized());
    }

    float hpRatio = (leader.getMaxHp() > 0.f)
        ? leader.getHp() / leader.getMaxHp()
        : 1.f;

    int crossedStage = shieldWallTriggerStage_;
    if (previousHpRatio_ > FIRST_SHIELD_WALL_HP_RATIO &&
        hpRatio <= FIRST_SHIELD_WALL_HP_RATIO) {
        crossedStage = std::max(crossedStage, 1);
    }
    if (previousHpRatio_ > SECOND_SHIELD_WALL_HP_RATIO &&
        hpRatio <= SECOND_SHIELD_WALL_HP_RATIO) {
        crossedStage = std::max(crossedStage, 2);
    }
    previousHpRatio_ = hpRatio;

    if (crossedStage > shieldWallTriggerStage_) {
        shieldWallTriggerStage_ = crossedStage;
        pendingShieldWallTrigger_ = true;
    }

    if (phase_ == Phase::Cooldown) {
        tacticCooldown_ -= dt;
        updateBossMelee(dt, room, leader);

        if (shouldPreserveOriginalSnakes()) {
            TacticalSquad* snakeSquad = leader.getSquads().size() >= 4
                ? leader.getSquads()[3] : nullptr;
            updateSnakeEvasion(dt, room, leader, snakeSquad);
        }

        engageRefreshTimer_ -= dt;
        if (engageRefreshTimer_ <= 0.f) {
            engageRefreshTimer_ = ENGAGE_REFRESH_INTERVAL;
            issueEngage(room, leader);
        }

        if (tacticCooldown_ > 0.f)
            return;

        enterPhase(Phase::Engage, "GrandBaum tactic cooldown finished", leader);
        return;
    }

    if (phase_ == Phase::Engage) {
        updateBossMelee(dt, room, leader);

        if (shouldPreserveOriginalSnakes()) {
            TacticalSquad* snakeSquad = leader.getSquads().size() >= 4
                ? leader.getSquads()[3] : nullptr;
            updateSnakeEvasion(dt, room, leader, snakeSquad);
        }

        if (!engageOrderIssued_) {
            issueEngage(room, leader);
            engageRefreshTimer_ = ENGAGE_REFRESH_INTERVAL;
            engageOrderIssued_ = true;
            return;
        }

        engageRefreshTimer_ -= dt;
        if (engageRefreshTimer_ <= 0.f) {
            engageRefreshTimer_ = ENGAGE_REFRESH_INTERVAL;
            issueEngage(room, leader);
        }

        if (pendingShieldWallTrigger_) {
            pendingShieldWallTrigger_ = false;
            TacticalSquad* originalSnakeSquad = squads.size() >= 4 ? squads[3] : nullptr;
            int liveOriginalSnakes = countLiveMembers(room, originalSnakeSquad);
            if (liveOriginalSnakes <= 0) {
                cleanupSnakeWave(room);
                tacticCooldown_ = TACTIC_COOLDOWN_DURATION;
                enterPhase(Phase::Cooldown,
                    "GrandBaum ShieldWall skipped - snake squad already annihilated",
                    leader);
                return;
            }

            int liveSlimes = countLiveSlimeMembers(room, leader);
            if (!canFormShieldWall(liveSlimes)) {
                cleanupSnakeWave(room);
                tacticCooldown_ = TACTIC_COOLDOWN_DURATION;
                enterPhase(Phase::Cooldown,
                    "GrandBaum ShieldWall skipped - not enough slimes",
                    leader);
                return;
            }

            originalSnakeCountAtShieldWall_ = liveOriginalSnakes;
            shieldWallRingRadius_ = calcShieldWallRadius(liveSlimes);
            cleanupSnakeWave(room);
            enterPhase(Phase::ShieldWall, "GrandBaum ShieldWall activated", leader);
            applyShieldWallProtection(room, leader, true);
            issueShieldWall(room, leader);
            return;
        }
    }

    if (phase_ == Phase::ShieldWall) {
        applyShieldWallProtection(room, leader, true);

        orderRefreshTimer_ -= dt;
        if (orderRefreshTimer_ <= 0.f) {
            orderRefreshTimer_ = ORDER_REFRESH_INTERVAL;
            issueShieldWall(room, leader);
        }

        TacticalSquad* originalSnakeSquad = squads.size() >= 4 ? squads[3] : nullptr;
        updateSnakeAmbush(dt, room, leader, originalSnakeSquad);
    }
}

void GrandBaumMidBossTactic::onLeaderDead(Room& room, PlatoonLeader& leader) {
    applyShieldWallProtection(room, leader, false);
    cleanupSnakeWave(room);
    MidBossTacticBase::onLeaderDead(room, leader);
}

void GrandBaumMidBossTactic::enterPhase(Phase next, const char* reason,
                                        PlatoonLeader& leader) {
    Logger::get().log(leader.getName(), reason);
    phase_ = next;

    if (next == Phase::Engage) {
        engageOrderIssued_ = false;
        engageRefreshTimer_ = 0.f;
        shieldWallRingIssued_ = false;
        shieldWallRingRadius_ = MAX_SHIELD_RING_RADIUS;
        snakeRetreatTimer_ = 0.f;
        snakeWaveSpawned_ = false;
        originalSnakeCountAtShieldWall_ = 0;
        snakeAmbushStage_ = SnakeAmbushStage::Evasion;
        snakeWanderCenterSet_ = false;
        snakePersonalTargets_.clear();
        snakePersonalTimers_.clear();
        snakePersonalEvading_.clear();
        return;
    }

    if (next == Phase::ShieldWall) {
        orderRefreshTimer_ = ORDER_REFRESH_INTERVAL;
        snakeRetreatTimer_ = 0.f;
        snakeWaveSpawned_ = false;
        shieldWallRingIssued_ = false;
        resetBossMelee(leader);
        snakePersonalTargets_.clear();
        snakePersonalTimers_.clear();
        snakePersonalEvading_.clear();
        snakeAmbushStage_ = SnakeAmbushStage::RetreatingOriginal;
        return;
    }

    if (next == Phase::Cooldown) {
        engageRefreshTimer_ = ENGAGE_REFRESH_INTERVAL;
        shieldWallRingIssued_ = false;
        shieldWallRingRadius_ = MAX_SHIELD_RING_RADIUS;
        snakeRetreatTimer_ = 0.f;
        snakeWaveSpawned_ = false;
    }
}

void GrandBaumMidBossTactic::resetBossMelee(PlatoonLeader& leader) {
    bossMeleeState_ = BossMeleeState::AcquireTarget;
    bossMeleeTimer_ = 0.f;
    bossMeleeTargetLockTimer_ = 0.f;
    bossMeleeSamePriorityRetargetTimer_ = 0.f;
    bossMeleeTargetId_ = 0;
    bossMeleeTargetPriority_ = BossTargetPriority::None;
    leader.setTacticalTarget(0);
    leader.transitionTacticalState(TacticalNpcState::Idle, "GrandBaum boss melee reset");
}

void GrandBaumMidBossTactic::updateBossMelee(
    float dt, Room& room, PlatoonLeader& leader) {
    if (bossMeleeTargetLockTimer_ > 0.f)
        bossMeleeTargetLockTimer_ = std::max(0.f, bossMeleeTargetLockTimer_ - dt);
    if (bossMeleeSamePriorityRetargetTimer_ > 0.f) {
        bossMeleeSamePriorityRetargetTimer_ =
            std::max(0.f, bossMeleeSamePriorityRetargetTimer_ - dt);
    }

    auto acquireTarget = [&]() {
        BossTargetChoice choice = selectBossMeleeTarget(room, leader);
        bossMeleeTargetId_ = choice.targetId;
        bossMeleeTargetPriority_ = choice.priority;
        leader.setTacticalTarget(bossMeleeTargetId_);
        if (bossMeleeTargetId_ == 0) {
            bossMeleeTargetLockTimer_ = 0.f;
            bossMeleeSamePriorityRetargetTimer_ = 0.f;
            leader.transitionTacticalState(TacticalNpcState::Idle,
                "GrandBaum no melee target");
            return false;
        }
        bossMeleeTargetLockTimer_ = BOSS_TARGET_LOCK_DURATION;
        bossMeleeSamePriorityRetargetTimer_ =
            BOSS_SAME_PRIORITY_RETARGET_INTERVAL;
        bossMeleeState_ = BossMeleeState::ChaseTarget;
        leader.transitionTacticalState(TacticalNpcState::Chase,
            isResourceThreatPriority(bossMeleeTargetPriority_)
                ? "GrandBaum chase resource threat"
                : "GrandBaum chase nearest player");
        return true;
    };

    if (bossMeleeState_ == BossMeleeState::AcquireTarget) {
        acquireTarget();
        return;
    }

    Actor* target = bossMeleeTargetId_ != 0
        ? room.findActorById(bossMeleeTargetId_)
        : nullptr;
    if (!target || !target->isAlive()) {
        bool wasChasing = bossMeleeState_ == BossMeleeState::ChaseTarget;
        bossMeleeState_ = BossMeleeState::AcquireTarget;
        bossMeleeTargetId_ = 0;
        bossMeleeTargetPriority_ = BossTargetPriority::None;
        bossMeleeTargetLockTimer_ = 0.f;
        bossMeleeSamePriorityRetargetTimer_ = 0.f;
        if (!acquireTarget() || !wasChasing)
            return;

        target = bossMeleeTargetId_ != 0
            ? room.findActorById(bossMeleeTargetId_)
            : nullptr;
        if (!target || !target->isAlive())
            return;
    }

    TacticalNpcConfig cfg = leader.getConfig();
    float attackRange = leader.getAttackRange();
    float attackRangeSq = attackRange * attackRange;

    if (bossMeleeState_ == BossMeleeState::ChaseTarget) {
        if (!isCurrentBossMeleeTargetValid(room, leader)) {
            bossMeleeState_ = BossMeleeState::AcquireTarget;
            bossMeleeTargetId_ = 0;
            bossMeleeTargetPriority_ = BossTargetPriority::None;
            bossMeleeTargetLockTimer_ = 0.f;
            bossMeleeSamePriorityRetargetTimer_ = 0.f;
            if (!acquireTarget())
                return;

            target = bossMeleeTargetId_ != 0
                ? room.findActorById(bossMeleeTargetId_)
                : nullptr;
            if (!target || !target->isAlive())
                return;
        }

        if (bossMeleeTargetLockTimer_ <= 0.f) {
            BossTargetChoice priorityTarget = selectBossMeleeTarget(room, leader);
            bool shouldSwitchTarget = false;
            if (priorityTarget.targetId != 0) {
                int newPriority = static_cast<int>(priorityTarget.priority);
                int currentPriority = static_cast<int>(bossMeleeTargetPriority_);
                if (newPriority > currentPriority) {
                    shouldSwitchTarget = true;
                } else if (newPriority == currentPriority &&
                           priorityTarget.targetId != bossMeleeTargetId_ &&
                           bossMeleeSamePriorityRetargetTimer_ <= 0.f) {
                    shouldSwitchTarget = true;
                }
            }

            if (shouldSwitchTarget) {
                bool targetChanged =
                    priorityTarget.targetId != bossMeleeTargetId_;
                bossMeleeTargetId_ = priorityTarget.targetId;
                bossMeleeTargetPriority_ = priorityTarget.priority;
                bossMeleeTargetLockTimer_ = BOSS_TARGET_LOCK_DURATION;
                bossMeleeSamePriorityRetargetTimer_ =
                    BOSS_SAME_PRIORITY_RETARGET_INTERVAL;
                leader.setTacticalTarget(bossMeleeTargetId_);
                if (targetChanged) {
                    target = room.findActorById(bossMeleeTargetId_);
                    if (!target || !target->isAlive()) {
                        bossMeleeState_ = BossMeleeState::AcquireTarget;
                        bossMeleeTargetId_ = 0;
                        bossMeleeTargetPriority_ = BossTargetPriority::None;
                        bossMeleeTargetLockTimer_ = 0.f;
                        bossMeleeSamePriorityRetargetTimer_ = 0.f;
                        return;
                    }
                }
            }
        }

        Vec3 toTarget = target->getPosition() - leader.getPosition();
        if (toTarget.lengthSq() <= attackRangeSq) {
            bossMeleeState_ = BossMeleeState::AttackWindup;
            bossMeleeTimer_ = 0.f;
            leader.transitionTacticalState(TacticalNpcState::AttackWindup,
                "GrandBaum melee windup");
            return;
        }

        moveBossToward(leader, target->getPosition(), BOSS_CHASE_SPEED_MULT, dt);
        return;
    }

    if (bossMeleeState_ == BossMeleeState::AttackWindup) {
        Vec3 toTarget = target->getPosition() - leader.getPosition();
        if (toTarget.lengthSq() > 0.01f)
            leader.setFacing(toTarget.normalized());

        bossMeleeTimer_ += dt;
        if (bossMeleeTimer_ < cfg.attackWindupTime)
            return;

        if (Vec3::distanceSq(leader.getPosition(), target->getPosition()) <=
            attackRangeSq) {
            target->takeDamage(leader.getAttackDamage());
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "GrandBaum melee hit %s for %.0f (hp=%.1f)",
                target->getName().c_str(),
                leader.getAttackDamage(),
                target->getHp());
            Logger::get().log(leader.getName(), buf);
        } else {
            Logger::get().log(leader.getName(), "GrandBaum melee missed");
        }

        bossMeleeState_ = BossMeleeState::AttackRecover;
        bossMeleeTimer_ = 0.f;
        leader.transitionTacticalState(TacticalNpcState::AttackRecover,
            "GrandBaum melee recover");
        return;
    }

    if (bossMeleeState_ == BossMeleeState::AttackRecover) {
        Vec3 toTarget = target->getPosition() - leader.getPosition();
        if (toTarget.lengthSq() > 0.01f)
            leader.setFacing(toTarget.normalized());

        bossMeleeTimer_ += dt;
        if (bossMeleeTimer_ < cfg.attackRecoverTime)
            return;

        bossMeleeState_ = BossMeleeState::AcquireTarget;
        bossMeleeTimer_ = 0.f;
    }
}

GrandBaumMidBossTactic::BossTargetChoice
GrandBaumMidBossTactic::selectBossMeleeTarget(
    Room& room, const PlatoonLeader& leader) const {
    if (shouldPreserveOriginalSnakes()) {
        BossTargetChoice snakeThreat =
            selectOriginalSnakeThreatTarget(room, leader);
        if (snakeThreat.targetId != 0)
            return snakeThreat;

        BossTargetChoice slimeThreat =
            selectSlimeThreatTarget(room, leader);
        if (slimeThreat.targetId != 0)
            return slimeThreat;
    }

    return selectNearestPlayerTarget(room, leader.getPosition());
}

GrandBaumMidBossTactic::BossTargetChoice
GrandBaumMidBossTactic::selectOriginalSnakeThreatTarget(
    Room& room, const PlatoonLeader& leader) const {
    uint32_t bestPlayerId = 0;
    float bestDistSq = -1.f;
    float rangeSq = SNAKE_STOP_EVADE_RANGE * SNAKE_STOP_EVADE_RANGE;

    for (uint32_t snakeId : getOriginalSnakeCandidateIds(leader)) {
        Actor* snake = room.findActorById(snakeId);
        if (!snake || !snake->isAlive())
            continue;

        for (Player* player : room.getLivingPlayers()) {
            if (!player || !player->isAlive())
                continue;

            float distSq =
                Vec3::distanceSq(snake->getPosition(), player->getPosition());
            if (distSq > rangeSq)
                continue;

            if (bestDistSq < 0.f || distSq < bestDistSq) {
                bestDistSq = distSq;
                bestPlayerId = player->getId();
            }
        }
    }

    return { bestPlayerId, bestPlayerId != 0
        ? BossTargetPriority::SnakeThreat
        : BossTargetPriority::None };
}

GrandBaumMidBossTactic::BossTargetChoice
GrandBaumMidBossTactic::selectSlimeThreatTarget(
    Room& room, const PlatoonLeader& leader) const {
    int liveCount = 0;
    Vec3 center = calcLiveSlimeCentroid(room, leader, liveCount);
    if (liveCount <= 0)
        return {};

    return selectNearestPlayerNear(
        room, center, BOSS_SLIME_THREAT_RANGE, BossTargetPriority::SlimeThreat);
}

std::vector<uint32_t> GrandBaumMidBossTactic::getOriginalSnakeCandidateIds(
    const PlatoonLeader& leader) const {
    if (!originalSnakeRoster_.empty())
        return originalSnakeRoster_;

    if (leader.getSquads().size() >= 4 && leader.getSquads()[3])
        return leader.getSquads()[3]->getMembers();

    return {};
}

GrandBaumMidBossTactic::BossTargetChoice
GrandBaumMidBossTactic::selectNearestPlayerTarget(
    const Room& room, const Vec3& center) const {
    uint32_t bestId = 0;
    float bestDistSq = -1.f;

    for (Player* player : room.getLivingPlayers()) {
        if (!player || !player->isAlive())
            continue;

        float distSq = Vec3::distanceSq(center, player->getPosition());
        if (bestDistSq < 0.f || distSq < bestDistSq) {
            bestDistSq = distSq;
            bestId = player->getId();
        }
    }

    return { bestId, bestId != 0
        ? BossTargetPriority::Nearest
        : BossTargetPriority::None };
}

GrandBaumMidBossTactic::BossTargetChoice
GrandBaumMidBossTactic::selectNearestPlayerNear(
    const Room& room, const Vec3& center, float radius,
    BossTargetPriority priority) const {
    uint32_t bestId = 0;
    float bestDistSq = -1.f;
    float radiusSq = radius * radius;

    for (Player* player : room.getLivingPlayers()) {
        if (!player || !player->isAlive())
            continue;

        float distSq = Vec3::distanceSq(center, player->getPosition());
        if (distSq > radiusSq)
            continue;
        if (bestDistSq < 0.f || distSq < bestDistSq) {
            bestDistSq = distSq;
            bestId = player->getId();
        }
    }

    return { bestId, bestId != 0 ? priority : BossTargetPriority::None };
}

bool GrandBaumMidBossTactic::isCurrentBossMeleeTargetValid(
    Room& room, const PlatoonLeader& leader) const {
    if (bossMeleeTargetId_ == 0)
        return false;

    Actor* target = room.findActorById(bossMeleeTargetId_);
    if (!target || !target->isAlive())
        return false;

    if (bossMeleeTargetPriority_ == BossTargetPriority::Nearest)
        return true;

    if (!shouldPreserveOriginalSnakes())
        return false;

    if (bossMeleeTargetPriority_ == BossTargetPriority::SnakeThreat) {
        float rangeSq = SNAKE_STOP_EVADE_RANGE * SNAKE_STOP_EVADE_RANGE;
        for (uint32_t snakeId : getOriginalSnakeCandidateIds(leader)) {
            Actor* snake = room.findActorById(snakeId);
            if (!snake || !snake->isAlive())
                continue;

            if (Vec3::distanceSq(snake->getPosition(), target->getPosition()) <=
                rangeSq) {
                return true;
            }
        }
        return false;
    }

    if (bossMeleeTargetPriority_ == BossTargetPriority::SlimeThreat) {
        int liveCount = 0;
        Vec3 center = calcLiveSlimeCentroid(room, leader, liveCount);
        return liveCount > 0 &&
            Vec3::distanceSq(center, target->getPosition()) <=
                BOSS_SLIME_THREAT_RANGE * BOSS_SLIME_THREAT_RANGE;
    }

    return false;
}

bool GrandBaumMidBossTactic::isResourceThreatPriority(
    BossTargetPriority priority) const {
    return priority == BossTargetPriority::SnakeThreat ||
           priority == BossTargetPriority::SlimeThreat;
}

Vec3 GrandBaumMidBossTactic::calcLiveOriginalSnakeCentroid(
    Room& room, const PlatoonLeader& leader, int& outLiveCount) const {
    Vec3 sum{};
    outLiveCount = 0;

    if (!originalSnakeRoster_.empty()) {
        for (uint32_t memberId : originalSnakeRoster_) {
            Actor* actor = room.findActorById(memberId);
            if (actor && actor->isAlive()) {
                sum += actor->getPosition();
                ++outLiveCount;
            }
        }
    } else if (leader.getSquads().size() >= 4 && leader.getSquads()[3]) {
        for (uint32_t memberId : leader.getSquads()[3]->getMembers()) {
            Actor* actor = room.findActorById(memberId);
            if (actor && actor->isAlive()) {
                sum += actor->getPosition();
                ++outLiveCount;
            }
        }
    }

    if (outLiveCount > 0)
        return sum / static_cast<float>(outLiveCount);
    return leader.getPosition();
}

Vec3 GrandBaumMidBossTactic::calcLiveSlimeCentroid(
    Room& room, const PlatoonLeader& leader, int& outLiveCount) const {
    Vec3 sum{};
    outLiveCount = 0;

    const auto& squads = leader.getSquads();
    const size_t slimeIndices[] = { 0, 1, 2 };
    for (size_t idx : slimeIndices) {
        if (idx >= squads.size() || !squads[idx])
            continue;

        for (uint32_t memberId : squads[idx]->getMembers()) {
            Actor* actor = room.findActorById(memberId);
            if (actor && actor->isAlive()) {
                sum += actor->getPosition();
                ++outLiveCount;
            }
        }
    }

    if (outLiveCount > 0)
        return sum / static_cast<float>(outLiveCount);
    return leader.getPosition();
}

void GrandBaumMidBossTactic::moveBossToward(
    PlatoonLeader& leader, const Vec3& targetPos, float speedMult, float dt) const {
    Vec3 toTarget = targetPos - leader.getPosition();
    if (toTarget.lengthSq() <= 0.01f)
        return;

    Vec3 dir = toTarget.normalized();
    leader.setPosition(leader.getPosition() +
        dir * leader.getLeaderMoveSpeed() * speedMult * dt);
    leader.setFacing(dir);
}

void GrandBaumMidBossTactic::issueEngage(Room& room, PlatoonLeader& leader) {
    uint32_t targetId = selectNearestPlayerId(room, leader.getPosition());

    if (targetId == 0) {
        issueIdleAll(leader);
        return;
    }

    std::vector<TacticalSquad*> liveSquads = collectLiveSquads(leader);
    std::vector<uint32_t> targets(liveSquads.size(), targetId);
    assignSquadsToPlayers(room, leader, liveSquads, targets);

    TacticalSquad* originalSnakeSquad = shouldPreserveOriginalSnakes() &&
        leader.getSquads().size() >= 4
        ? leader.getSquads()[3]
        : nullptr;

    for (size_t i = 0; i < liveSquads.size(); ++i) {
        if (liveSquads[i] == originalSnakeSquad) {
            continue;  // updateSnakeEvasion이 매 틱 처리
        }

        SquadOrder ord;
        ord.type = SquadOrderType::Engage;
        ord.targetId = targets[i];
        liveSquads[i]->receiveOrder(ord);
    }
}

void GrandBaumMidBossTactic::issueShieldWall(Room& room, PlatoonLeader& leader) {
    const auto& squads = leader.getSquads();
    uint32_t targetId = selectNearestPlayerId(room, leader.getPosition());

    Vec3 leaderPos = leader.getPosition();
    Vec3 playerCentroid = calcPlayerCentroid(room, leaderPos);
    Vec3 forward = playerCentroid - leaderPos;
    if (forward.lengthSq() > 0.01f)
        forward = forward.normalized();
    else
        forward = Vec3{ 1.f, 0.f, 0.f };

    if (!shieldWallRingIssued_) {
        shieldWallRingCenter_ = leaderPos;
        shieldWallRingStartAngle_ = std::atan2f(forward.z, forward.x) - 3.14159265f;
        shieldWallRingIssued_ = true;
        room.knockPlayersOutOfShieldWall(shieldWallRingCenter_, shieldWallRingRadius_);

        std::vector<TacticalSquad*> slimeSquads;
        int totalSlimeMembers = 0;
        const size_t slimeIndices[] = { 0, 1, 2 };
        for (size_t idx : slimeIndices) {
            if (idx >= squads.size())
                continue;
            TacticalSquad* squad = squads[idx];
            if (!squad || squad->isEmpty())
                continue;
            int liveMembers = countLiveMembers(room, squad);
            if (liveMembers <= 0)
                continue;
            slimeSquads.push_back(squad);
            totalSlimeMembers += liveMembers;
        }

        constexpr float TWO_PI = 2.f * 3.14159265f;
        float angleAccum = shieldWallRingStartAngle_;
        if (totalSlimeMembers > 0) {
            for (TacticalSquad* squad : slimeSquads) {
                int liveMembers = countLiveMembers(room, squad);
                float fraction = static_cast<float>(liveMembers) /
                                 static_cast<float>(totalSlimeMembers);
                float sectorSpan = TWO_PI * fraction;

                SquadOrder ord;
                ord.type = SquadOrderType::RingGuard;
                ord.targetId = targetId;
                ord.tacticCenter = shieldWallRingCenter_;
                ord.sectorAngle = angleAccum + sectorSpan * 0.5f;
                ord.sectorSpan = sectorSpan;
                ord.approachRadius = shieldWallRingRadius_;
                ord.slotSpacingScale = SHIELD_RING_MIN_ARC_SPACING;
                ord.slotColumnScale = SHIELD_RING_LANE_SPACING;
                squad->receiveOrder(ord);

                angleAccum += sectorSpan;
            }
        }
    }

    if (snakeAmbushStage_ == SnakeAmbushStage::RetreatingOriginal &&
        squads.size() >= 4) {
        issueOriginalSnakeRetreat(room, leader, squads[3]);
    }
}

int GrandBaumMidBossTactic::countLiveMembers(Room& room, TacticalSquad* squad) const {
    if (!squad)
        return 0;

    int count = 0;
    for (uint32_t memberId : squad->getMembers()) {
        Actor* actor = room.findActorById(memberId);
        if (actor && actor->isAlive())
            ++count;
    }
    return count;
}

int GrandBaumMidBossTactic::countLiveSlimeMembers(
    Room& room, const PlatoonLeader& leader) const {
    int count = 0;
    const auto& squads = leader.getSquads();
    const size_t slimeIndices[] = { 0, 1, 2 };
    for (size_t idx : slimeIndices) {
        if (idx >= squads.size())
            continue;
        count += countLiveMembers(room, squads[idx]);
    }
    return count;
}

bool GrandBaumMidBossTactic::canFormShieldWall(int liveSlimeCount) const {
    return liveSlimeCount >= MIN_SHIELD_WALL_SLIME_COUNT;
}

float GrandBaumMidBossTactic::calcShieldWallRadius(int liveSlimeCount) const {
    constexpr float TWO_PI = 2.f * 3.14159265f;
    float radius = static_cast<float>(liveSlimeCount) *
        SLIME_RING_SLOT_SPACING / TWO_PI;
    return std::clamp(radius, MIN_SHIELD_RING_RADIUS, MAX_SHIELD_RING_RADIUS);
}

int GrandBaumMidBossTactic::calcSnakeWaveSpawnCount(
    int liveOriginalSnakeCount) const {
    if (liveOriginalSnakeCount <= 0)
        return 0;

    int spawnCount = std::min(liveOriginalSnakeCount * SNAKE_WAVE_MULTIPLIER,
                              SNAKE_WAVE_MAX_COUNT);
    return (spawnCount / 4) * 4;
}

TacticalNpcConfig GrandBaumMidBossTactic::findSnakeConfig(
    Room& room, TacticalSquad* originalSnakeSquad) const {
    if (originalSnakeSquad) {
        for (uint32_t memberId : originalSnakeSquad->getMembers()) {
            auto* snake = dynamic_cast<TacticalNpc*>(room.findActorById(memberId));
            if (snake)
                return snake->getConfig();
        }
    }

    TacticalNpcConfig cfg;
    cfg.maxHp = 45.f;
    cfg.moveSpeed = 18.f;
    cfg.attackRange = 2.6f;
    cfg.attackDamage = 12.f;
    cfg.attackWindupTime = 0.35f;
    cfg.attackRecoverTime = 0.8f;
    cfg.separationRadius = 3.f;
    cfg.separationWeight = 0.9f;
    return cfg;
}

Vec3 GrandBaumMidBossTactic::pickSnakePersonalWanderTarget(const Vec3& center) const {
    float angle = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)
                  * 2.f * 3.14159265f;
    float dist  = SNAKE_DISPERSE_WANDER_RADIUS * (0.3f + 0.7f *
                  static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX));
    return center + Vec3{
        std::cos(angle) * dist, 0.f, std::sin(angle) * dist };
}

void GrandBaumMidBossTactic::updateSnakeEvasion(
    float dt, Room& room, PlatoonLeader& /*leader*/, TacticalSquad* snakeSquad) {
    if (!snakeSquad || snakeSquad->isEmpty())
        return;

    if (!snakeWanderCenterSet_) {
        snakeWanderCenter_    = snakeSquad->calcCentroid();
        snakeWanderCenterSet_ = true;
    }

    std::vector<uint32_t> liveSnakeIds;
    liveSnakeIds.reserve(snakeSquad->getMembers().size());
    for (uint32_t memberId : snakeSquad->getMembers()) {
        Actor* actor = room.findActorById(memberId);
        if (actor && actor->isAlive())
            liveSnakeIds.push_back(memberId);
    }

    auto isLiveSnake = [&](uint32_t id) {
        return std::find(liveSnakeIds.begin(), liveSnakeIds.end(), id) != liveSnakeIds.end();
    };

    for (auto it = snakePersonalTargets_.begin(); it != snakePersonalTargets_.end(); ) {
        if (!isLiveSnake(it->first)) it = snakePersonalTargets_.erase(it);
        else ++it;
    }
    for (auto it = snakePersonalTimers_.begin(); it != snakePersonalTimers_.end(); ) {
        if (!isLiveSnake(it->first)) it = snakePersonalTimers_.erase(it);
        else ++it;
    }
    for (auto it = snakePersonalEvading_.begin(); it != snakePersonalEvading_.end(); ) {
        if (!isLiveSnake(it->first)) it = snakePersonalEvading_.erase(it);
        else ++it;
    }

    for (uint32_t memberId : liveSnakeIds) {
        Actor* actor = room.findActorById(memberId);
        auto* snake = dynamic_cast<TacticalNpc*>(actor);
        if (!snake)
            continue;

        Vec3 snakePos = snake->getPosition();
        uint32_t targetId = selectNearestPlayerId(room, snakePos);
        Player* nearestPlayer = selectNearestPlayer(room, snakePos);
        Vec3 nearestPos = nearestPlayer ? nearestPlayer->getPosition() : snakeWanderCenter_;

        bool wasEvading = snakePersonalEvading_[memberId];
        bool shouldEvade = false;
        if (nearestPlayer) {
            float nearestDist = Vec3::distance(snakePos, nearestPos);
            shouldEvade = wasEvading
                ? (nearestDist < SNAKE_STOP_EVADE_RANGE)
                : (nearestDist < SNAKE_DETECT_RANGE);
        }

        Vec3 threatCenter{};
        float threatWeightSum = 0.f;
        for (Player* player : room.getLivingPlayers()) {
            if (!player)
                continue;

            Vec3 playerPos = player->getPosition();
            float dist = Vec3::distance(snakePos, playerPos);
            float weight = std::max(0.f, SNAKE_THREAT_WEIGHT_RANGE - dist);
            if (weight <= 0.f)
                continue;

            threatCenter += playerPos * weight;
            threatWeightSum += weight;
        }
        if (threatWeightSum > 0.f)
            threatCenter = threatCenter / threatWeightSum;
        else
            threatCenter = nearestPos;

        bool isTooFarFromCenter =
            Vec3::distance(snakePos, snakeWanderCenter_) > SNAKE_PERSONAL_MAX_LEASH_RADIUS;

        float& timer = snakePersonalTimers_[memberId];
        timer -= dt;
        if (shouldEvade != wasEvading) {
            timer = 0.f;
            snakePersonalEvading_[memberId] = shouldEvade;
        }

        if (timer <= 0.f || isTooFarFromCenter ||
            snakePersonalTargets_.find(memberId) == snakePersonalTargets_.end()) {
            float random01 = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);

            if (shouldEvade) {
                Vec3 fleeDir = snakePos - threatCenter;
                if (fleeDir.lengthSq() < 0.01f) fleeDir = Vec3{ 1.f, 0.f, 0.f };
                else                             fleeDir = fleeDir.normalized();

                Vec3 right{ -fleeDir.z, 0.f, fleeDir.x };
                float scatter = (random01 * 2.f - 1.f) * SNAKE_PERSONAL_SCATTER_RADIUS;
                snakePersonalTargets_[memberId] =
                    snakePos + fleeDir * SNAKE_EVASION_RADIUS + right * scatter;
                timer = SNAKE_EVASION_REFRESH * (0.75f + random01 * 0.5f);
            } else {
                snakePersonalTargets_[memberId] =
                    pickSnakePersonalWanderTarget(snakeWanderCenter_);
                timer = SNAKE_WANDER_INTERVAL * (0.75f + random01 * 0.5f);
            }

            if (isTooFarFromCenter && !shouldEvade) {
                Vec3 fromCenter = snakePos - snakeWanderCenter_;
                if (fromCenter.lengthSq() > 0.01f)
                    snakePersonalTargets_[memberId] =
                        snakeWanderCenter_ +
                        fromCenter.normalized() * SNAKE_DISPERSE_WANDER_RADIUS;
            }
        }

        Vec3 targetFromCenter = snakePersonalTargets_[memberId] - snakeWanderCenter_;
        if (targetFromCenter.length() > SNAKE_PERSONAL_MAX_LEASH_RADIUS) {
            snakePersonalTargets_[memberId] =
                snakeWanderCenter_ +
                targetFromCenter.normalized() * SNAKE_PERSONAL_MAX_LEASH_RADIUS;
        }

        if (targetId == 0) {
            TacticalCommand idle;
            idle.type = TacticalCommandType::Idle;
            snake->receiveCommand(idle);
            continue;
        }

        TacticalCommand cmd;
        cmd.type = TacticalCommandType::HoldSlot;
        cmd.targetId = targetId;
        cmd.slotOffset = snakePersonalTargets_[memberId];
        cmd.speedMult = shouldEvade ? SNAKE_EVASION_SPEED_MULT : SNAKE_WANDER_SPEED_MULT;
        snake->receiveCommand(cmd);
    }
}

void GrandBaumMidBossTactic::issueOriginalSnakeRetreat(
    Room& room, PlatoonLeader& leader, TacticalSquad* originalSnakeSquad) {
    if (!originalSnakeSquad || originalSnakeSquad->isEmpty())
        return;

    uint32_t targetId = selectNearestPlayerId(room, leader.getPosition());
    if (targetId == 0)
        return;

    Vec3 snakeCentroid = originalSnakeSquad->calcCentroid();
    Vec3 retreatDir = snakeCentroid - shieldWallRingCenter_;
    if (retreatDir.lengthSq() <= 0.01f) {
        Vec3 playerCentroid = calcPlayerCentroid(room, leader.getPosition());
        retreatDir = shieldWallRingCenter_ - playerCentroid;
    }
    if (retreatDir.lengthSq() > 0.01f)
        retreatDir = retreatDir.normalized();
    else
        retreatDir = Vec3{ 1.f, 0.f, 0.f };

    Vec3 retreatCenter = shieldWallRingCenter_ + retreatDir * SNAKE_OUTER_RADIUS;

    SquadOrder ord;
    ord.type = SquadOrderType::FormationHold;
    ord.targetId = targetId;
    ord.tacticCenter = retreatCenter;
    ord.formationTargetPos = shieldWallRingCenter_;
    ord.slotSpacingScale = 0.75f;
    ord.slotColumnScale = 2.0f;
    ord.speedMult = SNAKE_RETREAT_SPEED_MULT;
    originalSnakeSquad->receiveOrder(ord);
}

void GrandBaumMidBossTactic::spawnSnakeWave(
    Room& room, PlatoonLeader& leader, TacticalSquad* originalSnakeSquad) {
    int spawnCount = calcSnakeWaveSpawnCount(originalSnakeCountAtShieldWall_);
    if (spawnCount <= 0)
        return;

    cleanupSnakeWave(room);
    snakeWaveSpawned_ = true;
    snakeWaveSquadId_ = SNAKE_WAVE_SQUAD_ID;
    snakeWaveNpcIds_.clear();

    TacticalNpcConfig cfg = findSnakeConfig(room, originalSnakeSquad);
    auto waveSquad = std::make_unique<TacticalSquad>(
        snakeWaveSquadId_, cfg.attackRange, cfg.separationRadius);
    TacticalSquad* waveSquadPtr = waveSquad.get();

    constexpr float TWO_PI = 2.f * 3.14159265f;
    for (int i = 0; i < spawnCount; ++i) {
        float angle = shieldWallRingStartAngle_ +
            TWO_PI * static_cast<float>(i) / static_cast<float>(spawnCount);
        Vec3 pos = shieldWallRingCenter_ + Vec3{
            std::cos(angle) * SNAKE_OUTER_RADIUS,
            0.f,
            std::sin(angle) * SNAKE_OUTER_RADIUS
        };

        std::string name = "WaveSnake" + std::to_string(i + 1);
        auto snake = std::make_shared<TacticalNpc>(name, pos, cfg);
        snake->setSquadId(snakeWaveSquadId_);
        waveSquadPtr->addMember(snake.get());
        snakeWaveNpcIds_.push_back(snake->getId());
        room.addTacticalNpc(snake);
    }

    issueSnakeWaveEngage(room, waveSquadPtr);
    room.addTacticalSquad(std::move(waveSquad));

    Logger::get().log(leader.getName(),
        "GrandBaum snake wave spawned count=" + std::to_string(spawnCount));
}

void GrandBaumMidBossTactic::issueSnakeWaveEngage(
    Room& room, TacticalSquad* waveSquad) {
    if (!waveSquad || waveSquad->isEmpty())
        return;

    std::vector<Player*> players = room.getLivingPlayers();
    players.erase(std::remove(players.begin(), players.end(), nullptr), players.end());
    std::sort(players.begin(), players.end(),
        [](Player* a, Player* b) { return a->getId() < b->getId(); });

    if (players.empty()) {
        SquadOrder idle;
        idle.type = SquadOrderType::Idle;
        waveSquad->receiveOrder(idle);
        return;
    }

    SquadOrder ord;
    ord.type = SquadOrderType::DistributedEngage;
    for (Player* player : players)
        ord.targetIds.push_back(player->getId());
    waveSquad->receiveOrder(ord);
}

bool GrandBaumMidBossTactic::isSnakeWaveAnnihilated(Room& room) const {
    if (!snakeWaveSpawned_)
        return false;

    for (uint32_t npcId : snakeWaveNpcIds_) {
        Actor* actor = room.findActorById(npcId);
        if (actor && actor->isAlive())
            return false;
    }
    return true;
}

void GrandBaumMidBossTactic::updateSnakeAmbush(
    float dt, Room& room, PlatoonLeader& leader, TacticalSquad* originalSnakeSquad) {
    if (snakeAmbushStage_ == SnakeAmbushStage::RetreatingOriginal) {
        snakeRetreatTimer_ += dt;

        if (!snakeWaveSpawned_ &&
            ((!originalSnakeSquad || originalSnakeSquad->areMembersAtSlots()) ||
             snakeRetreatTimer_ >= SNAKE_RETREAT_MAX_TIME)) {
            spawnSnakeWave(room, leader, originalSnakeSquad);
            snakeAmbushStage_ = SnakeAmbushStage::WaveActive;
            Logger::get().log(leader.getName(), "GrandBaum original snake squad retreated");
        }
        return;
    }

    if (snakeAmbushStage_ == SnakeAmbushStage::WaveActive &&
        isSnakeWaveAnnihilated(room)) {
        finishShieldWall(room, leader, "GrandBaum ShieldWall finished - snake wave annihilated");
    }
}

void GrandBaumMidBossTactic::finishShieldWall(
    Room& room, PlatoonLeader& leader, const char* reason) {
    applyShieldWallProtection(room, leader, false);
    cleanupSnakeWave(room);
    reviveOriginalSnakeSquad(room, leader);
    snakeAmbushStage_ = SnakeAmbushStage::ReturningOriginal;
    issueEngage(room, leader);
    tacticCooldown_ = TACTIC_COOLDOWN_DURATION;
    enterPhase(Phase::Cooldown, reason, leader);
}

void GrandBaumMidBossTactic::cleanupSnakeWave(Room& room) {
    if (snakeWaveSquadId_ >= 0)
        room.removeTacticalSquad(snakeWaveSquadId_);

    for (uint32_t npcId : snakeWaveNpcIds_)
        room.removeTacticalNpc(npcId);

    snakeWaveNpcIds_.clear();
    snakeWaveSquadId_ = -1;
    snakeWaveSpawned_ = false;
}

void GrandBaumMidBossTactic::captureOriginalSnakeRoster(
    Room& room, TacticalSquad* originalSnakeSquad) {
    if (!originalSnakeRoster_.empty() || !originalSnakeSquad)
        return;

    for (uint32_t memberId : originalSnakeSquad->getMembers()) {
        Actor* actor = room.findActorById(memberId);
        if (!actor)
            continue;

        originalSnakeRoster_.push_back(memberId);
        originalSnakeSpawnPositions_[memberId] = actor->getPosition();
    }
}

void GrandBaumMidBossTactic::reviveOriginalSnakeSquad(
    Room& room, PlatoonLeader& leader) {
    if (originalSnakeRoster_.empty() || leader.getSquads().size() < 4)
        return;

    TacticalSquad* originalSnakeSquad = leader.getSquads()[3];
    if (!originalSnakeSquad)
        return;

    Vec3 reviveCenter = shieldWallRingIssued_ ? shieldWallRingCenter_ : leader.getPosition();
    constexpr float TWO_PI = 2.f * 3.14159265f;
    int revivedCount = 0;

    for (size_t i = 0; i < originalSnakeRoster_.size(); ++i) {
        uint32_t memberId = originalSnakeRoster_[i];
        Actor* actor = room.findActorById(memberId);
        auto* snake = dynamic_cast<TacticalNpc*>(actor);
        if (!snake)
            continue;

        float angle = shieldWallRingStartAngle_ +
            TWO_PI * static_cast<float>(i) /
            static_cast<float>(std::max<size_t>(1, originalSnakeRoster_.size()));
        Vec3 revivePos = reviveCenter + Vec3{
            std::cos(angle) * SNAKE_OUTER_RADIUS,
            0.f,
            std::sin(angle) * SNAKE_OUTER_RADIUS
        };
        if (!shieldWallRingIssued_) {
            auto spawnIt = originalSnakeSpawnPositions_.find(memberId);
            if (spawnIt != originalSnakeSpawnPositions_.end())
                revivePos = spawnIt->second;
        }

        if (!snake->isAlive()) {
            snake->reviveAt(revivePos);
            ++revivedCount;
        }

        snake->setSquadId(originalSnakeSquad->getSquadId());
        const std::vector<uint32_t>& members = originalSnakeSquad->getMembers();
        if (std::find(members.begin(), members.end(), memberId) == members.end())
            originalSnakeSquad->addMember(snake);
    }

    snakePersonalTargets_.clear();
    snakePersonalTimers_.clear();
    snakePersonalEvading_.clear();
    snakeWanderCenterSet_ = false;

    if (revivedCount > 0) {
        Logger::get().log(leader.getName(),
            "GrandBaum original snake squad revived count=" +
            std::to_string(revivedCount));
    }
}

bool GrandBaumMidBossTactic::shouldPreserveOriginalSnakes() const {
    return shieldWallTriggerStage_ < 2;
}

void GrandBaumMidBossTactic::applyShieldWallProtection(
    Room& room, PlatoonLeader& leader, bool enabled) {
    float multiplier = enabled ? SHIELDWALL_DAMAGE_MULT : 1.f;
    leader.setDamageTakenMultiplier(multiplier);

    if (!enabled)
        room.clearShieldWallBlockers();

    const auto& squads = leader.getSquads();
    std::vector<uint32_t> blockerIds;
    const size_t slimeIndices[] = { 0, 1, 2 };
    for (size_t idx : slimeIndices) {
        if (idx >= squads.size())
            continue;
        TacticalSquad* squad = squads[idx];
        if (!squad)
            continue;

        for (uint32_t memberId : squad->getMembers()) {
            Actor* actor = room.findActorById(memberId);
            if (actor) {
                actor->setDamageTakenMultiplier(multiplier);
                if (enabled && actor->isAlive())
                    blockerIds.push_back(memberId);
            }
        }
    }

    if (enabled)
        room.setShieldWallBlockers(blockerIds);
}

} // namespace sim
