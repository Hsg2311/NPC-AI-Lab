#include "PlatoonLeader.hpp"
#include "Room.hpp"
#include "Player.hpp"
#include "Logger.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace sim {

PlatoonLeader::PlatoonLeader(const std::string& name, const Vec3& pos,
                             const TacticalNpcConfig& cfg)
    : TacticalNpc(name, pos, cfg)
{}

void PlatoonLeader::addSquad(TacticalSquad* squad) {
    squads_.push_back(squad);
}

// ─── update ───────────────────────────────────────────────────────────────────

void PlatoonLeader::update(float dt, Room& room) {
    // 초기 부대 규모 기록 (사망 전 최초 1회)
    if (!initialSizesSet_) {
        initialSizesSet_ = true;
        for (auto* sq : squads_)
            initialSquadSizes_.push_back(static_cast<int>(sq->getMembers().size()));
    }

    // 사망 처리: 소속 Squad에 Confused 명령 (1회만)
    if (!alive_) {
        if (!deathReported_) {
            deathReported_ = true;
            for (auto* sq : squads_)
                sq->pushConfusedToMembers(room);
            Logger::get().log(name_, "사망 — Squad 전체 Confused 명령 발행");
        }
        updateDead();
        return;
    }

    // 경계 타이머 누적
    if (tacticalPhase_ == TacticalPhase::Vigilance)
        vigilanceElapsed_ += dt;

    // 쿨타임 관리
    if (tacticsOnCooldown_) {
        tacticCooldown_ -= dt;
        if (tacticCooldown_ <= 0.f) {
            tacticsOnCooldown_ = false;
            boxAdvanceActive_  = true;   // 쿨타임 종료 후 박스 대형 재개
            boxAdvanceOrderIssued_ = false;
            Logger::get().log(name_, "전술 쿨타임 종료 — 박스 대형 재개");
        }
    } else if (tacticsUnlocked_ && encircleSlotsAssigned_ && allMembersArrived(room)) {
        // 포위 완성 → 쿨타임 진입
        tacticsOnCooldown_     = true;
        tacticCooldown_        = TACTIC_COOLDOWN_DURATION;
        encircleSlotsAssigned_ = false;
        Logger::get().log(name_, "포위 완성 — 쿨타임 진입");
    }

    // ── 박스 대형 완성 감지 (tacticsUnlocked_ 여부 무관) ────────────────────
    Player* primary = selectPrimaryTarget(room);

    if (!tacticsOnCooldown_ && primary && boxAdvanceActive_ && allMembersArrived(room)) {
        boxAdvanceActive_      = false;
        boxAdvanceOrderIssued_ = false;
        if (!tacticsUnlocked_) {
            Logger::get().log(name_, "박스 대형 완성 — Engage 전환");
            for (auto* sq : squads_) {
                if (sq->isEmpty()) continue;
                SquadOrder ord;
                ord.type     = SquadOrderType::Engage;
                ord.targetId = primaryTargetId_;
                sq->receiveOrder(ord);
            }
        } else {
            Logger::get().log(name_, "박스 대형 완성 — 포위 준비");
        }
    }

    // 전술 평가 (주기적)
    tacticTimer_ -= dt;
    if (tacticTimer_ <= 0.f) {
        tacticTimer_ = TACTIC_INTERVAL;
        evaluateTactics(room);
    }

    // ── 보스 이동 ─────────────────────────────────────────────────────────────
    pendingCmd_.type = TacticalCommandType::None;

    if (boxAdvanceActive_) {
        // BoxAdvance 중: 보스 이동 없음, 플레이어 응시
        if (primary)
            facing_ = (primary->getPosition() - position_).normalized();
        return;
    }

    // Engage / Tactics 중: 플레이어와 거리 유지
    if (primary) {
        float dist    = Vec3::distance(position_, primary->getPosition());
        Vec3 toPlayer = (primary->getPosition() - position_).normalized();
        if (dist > BOSS_KEEP_DIST + BOSS_KEEP_TOL) {
            position_ += toPlayer * moveSpeed_ * dt;
            facing_    = toPlayer;
        } else if (dist < BOSS_KEEP_DIST - BOSS_KEEP_TOL) {
            position_ -= toPlayer * moveSpeed_ * dt;
            facing_    = toPlayer;
        } else {
            facing_ = toPlayer;
        }
    }
    // TacticalNpc::update() 호출하지 않음 — 보스는 직접 공격 안 함
}

// ─── evaluateTactics ─────────────────────────────────────────────────────────

void PlatoonLeader::evaluateTactics(Room& room) {
    for (auto* sq : squads_)
        sq->removeDeadMembers(room);

    std::vector<TacticalSquad*> liveSquads;
    for (auto* sq : squads_)
        if (!sq->isEmpty()) liveSquads.push_back(sq);

    Player* primary = selectPrimaryTarget(room);

    if (!primary || liveSquads.empty()) {
        for (auto* sq : liveSquads) {
            SquadOrder ord; ord.type = SquadOrderType::Idle;
            sq->receiveOrder(ord);
        }
        if (state_ != TacticalNpcState::Idle && state_ != TacticalNpcState::Return) {
            targetId_ = 0;
            transitionTo(TacticalNpcState::Idle, "플레이어 없음");
        }
        return;
    }

    // 리더 자신은 항상 primary 추격
    if (targetId_ != primary->getId()) {
        targetId_ = primary->getId();
        if (state_ == TacticalNpcState::Idle || state_ == TacticalNpcState::Return) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "전술 평가: target=%s",
                primary->getName().c_str());
            transitionTo(TacticalNpcState::Chase, buf);
        }
    }

    // 전술 발동 조건 체크 (한번 활성화되면 유지)
    if (!tacticsUnlocked_ && checkTacticsConditions()) {
        tacticsUnlocked_ = true;
        Logger::get().log(name_, "전술 활성화 — 조건 충족");
    }

    int numSquads = static_cast<int>(liveSquads.size());

    if (!tacticsUnlocked_ || tacticsOnCooldown_ || boxAdvanceActive_) {
        primaryTargetId_ = primary->getId();
        if (boxAdvanceActive_) {
            if (boxAdvanceOrderIssued_) return;

            boxAdvanceTargetPos_ = primary->getPosition();

            // ── 박스 대형 진격 ────────────────────────────────────────────────
            // right 방향 투영값 기준으로 부대 정렬 → 반대편 이동 방지
            Vec3 toTgt2  = boxAdvanceTargetPos_ - position_;
            float tLen2  = toTgt2.length();
            Vec3  fwd2   = (tLen2 > 0.01f) ? (toTgt2 / tLen2) : Vec3{ 1.f, 0.f, 0.f };
            Vec3  rgt2{ -fwd2.z, 0.f, fwd2.x };

            std::vector<std::pair<float, TacticalSquad*>> sqByLat;
            sqByLat.reserve(static_cast<size_t>(numSquads));
            for (auto* sq : liveSquads) {
                Vec3 sum{}; int cnt = 0;
                for (uint32_t mid : sq->getMembers()) {
                    Actor* ma = room.findActorById(mid);
                    if (ma && ma->isAlive()) { sum += ma->getPosition(); ++cnt; }
                }
                Vec3 cen = (cnt > 0) ? (sum / static_cast<float>(cnt)) : position_;
                sqByLat.push_back({ cen.dot(rgt2), sq });
            }
            std::sort(sqByLat.begin(), sqByLat.end(),
                [](const std::pair<float, TacticalSquad*>& a,
                   const std::pair<float, TacticalSquad*>& b) {
                    return a.first < b.first;
                });

            auto offsets = calcSquadBoxOffsets(numSquads);
            for (int i = 0; i < numSquads; ++i) {
                SquadOrder ord;
                ord.type           = SquadOrderType::BoxAdvance;
                ord.targetId       = primaryTargetId_;
                ord.sectorPos      = offsets[static_cast<size_t>(i)];
                ord.leaderPos      = position_;
                ord.formationTargetPos = boxAdvanceTargetPos_;
                ord.approachRadius = BOX_APPROACH_DIST;
                sqByLat[static_cast<size_t>(i)].second->receiveOrder(ord);
            }
            boxAdvanceOrderIssued_ = true;
        } else {
            // ── Engage 유지 (박스 완성 후 전투 중) ────────────────────────────
            for (auto* sq : liveSquads) {
                SquadOrder ord;
                ord.type     = SquadOrderType::Engage;
                ord.targetId = primaryTargetId_;
                sq->receiveOrder(ord);
            }
        }
        return;
    }

    bool scattered = (clusterPlayers(room) >= 2);

    if (!scattered) {
        // ── (가) 포위 ─────────────────────────────────────────────────────────
        Vec3 centroid   = calcPlayerCentroid(room);
        bool isNewPhase = (tacticalPhase_ != TacticalPhase::Encircle);

        if (isNewPhase) {
            Logger::get().log(name_, "전술 전환: 포위");
            tacticalPhase_    = TacticalPhase::Encircle;
            vigilanceElapsed_ = 0.f;
        }

        // 현 사이클에서 아직 슬롯을 발행하지 않은 경우에만 재발행
        // (플레이어 이동으로 인한 재할당 방지 — 쿨타임 후 encircleSlotsAssigned_ 가 리셋)
        if (isNewPhase || !encircleSlotsAssigned_) {
            lastEncircleCentroid_  = centroid;
            encircleSlotsAssigned_ = true;

            int totalMembers = 0;
            for (auto* sq : liveSquads)
                totalMembers += static_cast<int>(sq->getMembers().size());
            if (totalMembers < 1) totalMembers = 1;

            constexpr float TWO_PI = 2.f * 3.14159265f;

            // ── 명령 발행 ────────────────────────────────────────────────────
            float angleAccum = 0.f;
            for (int i = 0; i < numSquads; ++i) {
                int   memberCount = static_cast<int>(liveSquads[static_cast<size_t>(i)]->getMembers().size());
                float fraction    = static_cast<float>(memberCount) / static_cast<float>(totalMembers);
                float sectorSpan  = TWO_PI * fraction;
                float sectorAngle = angleAccum + sectorSpan * 0.5f;

                SquadOrder ord;
                ord.type           = SquadOrderType::Encircle;
                ord.targetId       = primary->getId();
                ord.sectorAngle    = sectorAngle;
                ord.sectorSpan     = sectorSpan;
                ord.approachRadius = ENCIRCLE_RADIUS;
                liveSquads[static_cast<size_t>(i)]->receiveOrder(ord);

                angleAccum += sectorSpan;
            }
        }
        // 슬롯 이미 발행됨 → 재발행 없음 (NPC 현재 상태 유지)

    } else {
        // ── 분산 상태 ─────────────────────────────────────────────────────────
        switch (tacticalPhase_) {

            case TacticalPhase::Encircle:
                Logger::get().log(name_, "전술 전환: 경계");
                tacticalPhase_    = TacticalPhase::Vigilance;
                vigilanceElapsed_ = 0.f;
                for (auto* sq : liveSquads) {
                    SquadOrder ord;
                    ord.type     = SquadOrderType::DenseHold;
                    ord.targetId = primary->getId();
                    sq->receiveOrder(ord);
                }
                break;

            case TacticalPhase::Vigilance:
                // 경계 유지 — 재발행 없음
                if (vigilanceElapsed_ >= VIGILANCE_DURATION) {
                    Logger::get().log(name_, "전술 전환: 각개격파");
                    tacticalPhase_ = TacticalPhase::DivideAndConquer;
                    if (numSquads >= 1) {
                        SquadOrder atk;
                        atk.type      = SquadOrderType::WedgeCharge;
                        atk.targetId  = primary->getId();
                        atk.leaderPos = position_;
                        liveSquads[0]->receiveOrder(atk);
                    }
                    for (int i = 1; i < numSquads; ++i) {
                        SquadOrder hold;
                        hold.type     = SquadOrderType::DenseHold;
                        hold.targetId = primary->getId();
                        liveSquads[static_cast<size_t>(i)]->receiveOrder(hold);
                    }
                }
                break;

            case TacticalPhase::DivideAndConquer:
                // 쐐기 방향만 매 evaluate마다 갱신 (매 틱은 Squad::update()에서 처리)
                if (numSquads >= 1) {
                    SquadOrder atk;
                    atk.type      = SquadOrderType::WedgeCharge;
                    atk.targetId  = primary->getId();
                    atk.leaderPos = position_;
                    liveSquads[0]->receiveOrder(atk);
                }
                break;
        }
    }
}

// ─── checkTacticsConditions ───────────────────────────────────────────────────

bool PlatoonLeader::checkTacticsConditions() const {
    // 조건 A: 리더 HP가 임계값 이하
    if (maxHp_ > 0.f && hp_ / maxHp_ < TACTIC_HP_THRESHOLD) return true;

    // 조건 B: 어느 부대든 초기 인원 대비 생존 비율이 임계값 미만
    for (size_t i = 0; i < squads_.size(); ++i) {
        int initial = (i < initialSquadSizes_.size()) ? initialSquadSizes_[i] : 0;
        int current = static_cast<int>(squads_[i]->getMembers().size());
        if (initial > 0 &&
            static_cast<float>(current) / static_cast<float>(initial) < TACTIC_SQUAD_RATIO)
            return true;
    }
    return false;
}

// ─── clusterPlayers ───────────────────────────────────────────────────────────
// union-find 없이 연결 컴포넌트 카운트 (플레이어 수 ≤ 8이므로 O(N²) 허용)

int PlatoonLeader::clusterPlayers(const Room& room) const {
    const auto& players = room.getLivingPlayers();
    int n = static_cast<int>(players.size());
    if (n <= 1) return n;

    std::vector<int> label(static_cast<size_t>(n), -1);
    int numClusters = 0;

    for (int i = 0; i < n; ++i) {
        if (label[i] != -1) continue;
        label[i] = numClusters++;
        // BFS
        for (int j = i + 1; j < n; ++j) {
            if (label[j] == -1) {
                float d = Vec3::distance(players[static_cast<size_t>(i)]->getPosition(),
                                         players[static_cast<size_t>(j)]->getPosition());
                if (d <= CLUSTER_RADIUS) label[j] = label[i];
            }
        }
    }
    return numClusters;
}

// ─── calcPlayerCentroid ───────────────────────────────────────────────────────

Vec3 PlatoonLeader::calcPlayerCentroid(const Room& room) const {
    const auto& players = room.getLivingPlayers();
    if (players.empty()) return position_;
    Vec3 sum{};
    for (Player* p : players) sum += p->getPosition();
    return sum / static_cast<float>(players.size());
}

// ─── selectPrimaryTarget ─────────────────────────────────────────────────────

Player* PlatoonLeader::selectPrimaryTarget(Room& room) const {
    Player* best      = nullptr;
    float   bestScore = -1.f;
    for (Player* p : room.getLivingPlayers()) {
        float s = evaluatePlayerScore(p);
        if (s > bestScore) { bestScore = s; best = p; }
    }
    return best;
}

float PlatoonLeader::evaluatePlayerScore(const Player* p) const {
    float dist      = Vec3::distance(position_, p->getPosition());
    float distScore = 1.f / (1.f + dist);
    float hpScore   = 1.f - (p->getHp() / p->getMaxHp());
    return distScore * 0.5f + hpScore * 0.5f;
}

// ─── allMembersArrived ────────────────────────────────────────────────────────
// 모든 생존 멤버가 슬롯에 도착했는지 확인.

bool PlatoonLeader::allMembersArrived(const Room& room) const {
    for (auto* sq : squads_) {
        for (uint32_t id : sq->getMembers()) {
            Actor* a = room.findActorById(id);
            if (!a || !a->isAlive()) continue;
            auto* tnpc = dynamic_cast<TacticalNpc*>(a);
            if (!tnpc) continue;
            if (!tnpc->isAtSlot()) return false;
        }
    }
    return true;
}

// ─── calcSquadBoxOffsets ──────────────────────────────────────────────────────
// numSquads개 부대의 상대 오프셋 반환.
// x=우방향, z=깊이방향 — TacticalSquad가 매 틱 절대 좌표로 변환.

std::vector<Vec3> PlatoonLeader::calcSquadBoxOffsets(int numSquads) const {
    int rows = static_cast<int>(std::max(1.f, std::floorf(std::sqrtf(static_cast<float>(numSquads)))));
    int cols = (numSquads + rows - 1) / rows;

    std::vector<Vec3> offsets;
    offsets.reserve(static_cast<size_t>(numSquads));
    for (int i = 0; i < numSquads; ++i) {
        int   col    = i % cols;
        int   row    = i / cols;
        float colOff     = (static_cast<float>(col) - static_cast<float>(cols - 1) * 0.5f) * BOX_SQUAD_SPACING;
        float rowOff     = (static_cast<float>(row) - static_cast<float>(rows - 1) * 0.5f) * BOX_SQUAD_SPACING;
        float halfCols   = static_cast<float>(cols - 1) * 0.5f;
        float latFrac    = (cols > 1)
            ? std::abs(static_cast<float>(col) - halfCols) / halfCols
            : 0.f;
        float arcZ       = rowOff - BOX_ARC_DEPTH * latFrac;  // 측면 부대를 플레이어 방향으로 당김
        offsets.push_back(Vec3{ colOff, 0.f, arcZ });
    }
    return offsets;
}

} // namespace sim
