#pragma once
#include "BehaviorTree.hpp"
#include "Vec3.hpp"
#include <cstdint>

// FinalBoss 전용 BT 리프 액션 노드.
// 패턴 피해는 항상 영역 기반으로 적용해 다수 플레이어에 자동 대응한다.

namespace sim {

// ─── BossChaseAction ─────────────────────────────────────────────────────────
// 타겟에게 이동하는 폴백 리프. 타겟이 있는 한 항상 Running.

class BossChaseAction : public BtNode {
public:
    const char* name() const override { return "Chase"; }
    BtStatus tick(float dt, FinalBoss& boss, Room& room) override;
};

// ─── BossIdleAction ──────────────────────────────────────────────────────────
// 살아있는 플레이어가 없을 때의 폴백 리프. 항상 Running.

class BossIdleAction : public BtNode {
public:
    const char* name() const override { return "Idle"; }
    BtStatus tick(float dt, FinalBoss& boss, Room& room) override;
};

// ─── BossMeleeComboAction ────────────────────────────────────────────────────
// 근접 콤보: Windup → Hit → Linger 를 콤보 타수만큼 반복, 마지막 타는 피해 증가.
// Windup 동안 이동하지 않고 타격 지점을 고정해 텔레그래프로 예고한다.
// 타격은 보스 전방 지점 중심 영역 피해.

class BossMeleeComboAction : public BtNode {
public:
    const char* name() const override { return "MeleeCombo"; }
    BtStatus tick(float dt, FinalBoss& boss, Room& room) override;
    void reset() override;

private:
    enum class Phase { Windup, Linger };

    Phase phase_{ Phase::Windup };
    int   hitIndex_{ 0 };       // 0부터 시작, comboHitCount 도달 시 완료
    float timer_{ 0.f };
    bool  windupStarted_{ false };
    Vec3  hitCenter_{};         // Windup 시작 시 고정되는 타격 지점
};

// ─── BossChargeAction ────────────────────────────────────────────────────────
// 돌진: Aim(조준 + 경로 텔레그래프) → Dash(직선 돌진) → Stop(경직).
// Dash 피해는 Room의 WedgeCharge 레지스트리를 재사용해
// 같은 돌진에서 플레이어당 1회만 적용한다.
// Aim 종료 시 목적지가 고정되므로 Dash 중에는 타겟이 죽어도 끝까지 진행한다.

class BossChargeAction : public BtNode {
public:
    const char* name() const override { return "Charge"; }
    BtStatus tick(float dt, FinalBoss& boss, Room& room) override;
    void reset() override;

private:
    enum class Phase { Aim, Dash, Stop };

    Phase    phase_{ Phase::Aim };
    float    timer_{ 0.f };
    Vec3     chargeDest_{};
    uint32_t chargeId_{ 0 };    // 0 = 비활성

    void addPathTelegraph(FinalBoss& boss, Room& room, float progress) const;
};

} // namespace sim
