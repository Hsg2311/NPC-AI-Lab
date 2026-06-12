#include "FinalBoss.hpp"
#include "BossBtActions.hpp"
#include "Player.hpp"
#include "Room.hpp"
#include "Logger.hpp"
#include <cstring>
#include <limits>

namespace sim {

FinalBoss::FinalBoss(const std::string& name, const Vec3& pos,
                     const FinalBossConfig& cfg)
    : Actor(name, pos, cfg.maxHp)
    , cfg_(cfg)
    , spawnPos_(pos)
    , moveSpeed_(cfg.moveSpeed)
{
    buildTree();
}

// ─── update ──────────────────────────────────────────────────────────────────

void FinalBoss::update(float dt, Room& room) {
    if (!alive_)
        return;

    evaluateTarget(dt, room);

    currentLeaf_ = nullptr;
    root_->tick(dt, *this, room);

    const char* leaf = currentLeaf_ ? currentLeaf_ : "None";
    if (std::strcmp(leaf, activeLeaf_) != 0) {
        Logger::get().log("BOSS:" + name_,
            std::string("BT leaf: ") + activeLeaf_ + " -> " + leaf);
        activeLeaf_     = leaf;
        actionProgress_ = 0.f;
    }
}

// ─── buildTree ───────────────────────────────────────────────────────────────
// Root (Selector)
// ├─ Sequence: [Cond: 타겟 있음]
// │   └─ Selector (전투)
// │       ├─ Cooldown(8s): Sequence [Cond: 타겟 멀리] → ChargeAction
// │       ├─ Cooldown(3s): Sequence [Cond: 타겟 근접] → MeleeComboAction
// │       └─ ChaseAction
// └─ IdleAction
// 페이즈/광폭화는 추후 Root 최상단에 우선순위 가지로 추가한다.

void FinalBoss::buildTree() {
    auto combat = std::make_unique<BtSelector>("Combat");

    {
        auto seq = std::make_unique<BtSequence>("ChargeSeq");
        seq->addChild(std::make_unique<BtCondition>("TargetFar",
            [](FinalBoss& boss, Room& room) {
                return boss.distanceToTarget(room) >= boss.getConfig().chargeMinRange;
            }));
        seq->addChild(std::make_unique<BossChargeAction>());
        combat->addChild(std::make_unique<BtCooldown>(
            "ChargeCooldown", cfg_.chargeCooldown, std::move(seq)));
    }
    {
        auto seq = std::make_unique<BtSequence>("MeleeComboSeq");
        seq->addChild(std::make_unique<BtCondition>("TargetNear",
            [](FinalBoss& boss, Room& room) {
                return boss.distanceToTarget(room) <= boss.getConfig().attackRange;
            }));
        seq->addChild(std::make_unique<BossMeleeComboAction>());
        combat->addChild(std::make_unique<BtCooldown>(
            "MeleeCooldown", cfg_.comboCooldown, std::move(seq)));
    }
    combat->addChild(std::make_unique<BossChaseAction>());

    auto engage = std::make_unique<BtSequence>("EngageSeq");
    engage->addChild(std::make_unique<BtCondition>("HasTarget",
        [](FinalBoss& boss, Room& room) {
            return boss.resolveTarget(room) != nullptr;
        }));
    engage->addChild(std::move(combat));

    auto rootSel = std::make_unique<BtSelector>("Root");
    rootSel->addChild(std::move(engage));
    rootSel->addChild(std::make_unique<BossIdleAction>());
    root_ = std::move(rootSel);
}

// ─── evaluateTarget ──────────────────────────────────────────────────────────
// 감지 범위 없이 살아있는 플레이어 전원을 후보로 점수 평가한다.
// 현재 타겟이 죽거나 사라지면 즉시 재평가한다.

void FinalBoss::evaluateTarget(float dt, Room& room) {
    targetEvalTimer_ -= dt;

    bool targetValid = (resolveTarget(room) != nullptr);
    if (targetValid && targetEvalTimer_ > 0.f)
        return;

    targetEvalTimer_ = cfg_.targetEvalInterval;

    Player* best      = nullptr;
    float   bestScore = -std::numeric_limits<float>::max();
    for (Player* p : room.getLivingPlayers()) {
        float score = scoreTarget(*p);
        if (score > bestScore) {
            bestScore = score;
            best      = p;
        }
    }

    uint32_t newId = best ? best->getId() : 0;
    if (newId != targetId_) {
        targetId_ = newId;
        if (best)
            Logger::get().log("BOSS:" + name_, "타겟 선택: " + best->getName());
    }
}

float FinalBoss::scoreTarget(const Player& player) const {
    float dist    = Vec3::distance(position_, player.getPosition());
    float hpRatio = (player.getMaxHp() > 0.f)
                        ? player.getHp() / player.getMaxHp() : 1.f;

    float score = 100.f / (dist + 1.f);    // 가까울수록 높음
    score += (1.f - hpRatio) * 20.f;       // 저HP 가중치
    return score;
}

// ─── 리프 노드용 헬퍼 ─────────────────────────────────────────────────────────

Player* FinalBoss::resolveTarget(Room& room) const {
    if (targetId_ == 0)
        return nullptr;
    for (Player* p : room.getLivingPlayers())
        if (p->getId() == targetId_)
            return p;
    return nullptr;
}

float FinalBoss::distanceToTarget(Room& room) const {
    Player* target = resolveTarget(room);
    if (!target)
        return std::numeric_limits<float>::max();
    return Vec3::distance(position_, target->getPosition());
}

void FinalBoss::moveToward(const Vec3& dest, float speedMult, float dt) {
    Vec3 dir = dest - position_;
    dir.y = 0.f;
    float dist = dir.length();
    if (dist < 1e-4f)
        return;

    dir = dir / dist;
    facing_ = dir;

    float step = moveSpeed_ * speedMult * dt;
    if (step > dist)
        step = dist;    // 목적지 오버슈트 방지
    position_ += dir * step;
}

void FinalBoss::faceToward(const Vec3& pos) {
    Vec3 dir = pos - position_;
    dir.y = 0.f;
    if (dir.lengthSq() > 1e-6f)
        facing_ = dir.normalized();
}

} // namespace sim
