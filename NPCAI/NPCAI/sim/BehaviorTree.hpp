#pragma once
#include <vector>
#include <memory>
#include <functional>

// 최종보스 전용 소형 Behavior Tree 프레임워크.
// 일반 NPC/미드보스는 기존 FSM을 유지하고, FinalBoss만 이 트리를 사용한다.
// 컴포지트는 Running 자식을 기억해 다음 틱에 이어서 실행하며,
// Selector는 기억한 자식보다 앞선(높은 우선순위) 가지를 매 틱 재평가하여
// 인터럽트를 허용한다.

namespace sim {

class FinalBoss;
class Room;

enum class BtStatus { Success, Failure, Running };

// ─── BtNode ─────────────────────────────────────────────────────────────────

class BtNode {
public:
    virtual ~BtNode() = default;

    virtual const char* name() const = 0;
    virtual BtStatus tick(float dt, FinalBoss& boss, Room& room) = 0;

    // Running 도중 다른 가지에 선점당했을 때 내부 상태 초기화
    virtual void reset() {}
};

// ─── BtSelector ─────────────────────────────────────────────────────────────
// 자식을 순서대로 평가해 Success/Running이면 즉시 반환한다.
// 매 틱 첫 자식부터 재평가하므로 상위 가지가 살아나면
// 기억해 둔 Running 자식은 reset() 후 선점된다.

class BtSelector : public BtNode {
public:
    explicit BtSelector(const char* name) : name_(name) {}

    void addChild(std::unique_ptr<BtNode> child);

    const char* name() const override { return name_; }
    BtStatus tick(float dt, FinalBoss& boss, Room& room) override;
    void reset() override;

private:
    const char* name_;
    std::vector<std::unique_ptr<BtNode>> children_;
    int runningIndex_{ -1 };
};

// ─── BtSequence ─────────────────────────────────────────────────────────────
// 자식을 순서대로 평가해 Failure/Running이면 즉시 반환한다.
// Running 자식을 기억하고 다음 틱에 그 자식부터 재개한다 (패턴 도중 중단 방지).

class BtSequence : public BtNode {
public:
    explicit BtSequence(const char* name) : name_(name) {}

    void addChild(std::unique_ptr<BtNode> child);

    const char* name() const override { return name_; }
    BtStatus tick(float dt, FinalBoss& boss, Room& room) override;
    void reset() override;

private:
    const char* name_;
    std::vector<std::unique_ptr<BtNode>> children_;
    int runningIndex_{ -1 };
};

// ─── BtCondition ────────────────────────────────────────────────────────────
// bool 함수 래퍼. true=Success, false=Failure.

class BtCondition : public BtNode {
public:
    using Predicate = std::function<bool(FinalBoss&, Room&)>;

    BtCondition(const char* name, Predicate pred)
        : name_(name), pred_(std::move(pred)) {}

    const char* name() const override { return name_; }
    BtStatus tick(float dt, FinalBoss& boss, Room& room) override;

private:
    const char* name_;
    Predicate pred_;
};

// ─── BtCooldown ─────────────────────────────────────────────────────────────
// 데코레이터: 자식이 Success를 반환하면 cooldownSec 동안 Failure를 반환한다.
// 자식이 Failure면 쿨다운을 시작하지 않는다.

class BtCooldown : public BtNode {
public:
    BtCooldown(const char* name, float cooldownSec, std::unique_ptr<BtNode> child)
        : name_(name), cooldownSec_(cooldownSec), child_(std::move(child)) {}

    const char* name() const override { return name_; }
    BtStatus tick(float dt, FinalBoss& boss, Room& room) override;
    void reset() override;

private:
    const char* name_;
    float cooldownSec_;
    float timer_{ 0.f };   // 남은 쿨다운 (0 이하 = 사용 가능)
    std::unique_ptr<BtNode> child_;
};

} // namespace sim
