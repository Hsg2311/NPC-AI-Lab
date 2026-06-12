#pragma once
#include "Actor.hpp"
#include "BehaviorTree.hpp"
#include <memory>

namespace sim {

class Player;

// 최종보스 설정. 보스전은 전용 맵에서 진행되므로 감지 범위(detectionRange)가 없다.
// 살아있는 플레이어가 존재하면 무조건 점수 기반으로 타겟을 선택한다.
struct FinalBossConfig {
    float maxHp              = 1000.f;
    float moveSpeed          = 8.5f;
    float attackRange        = 3.5f;   // 근접 패턴 발동 거리
    float targetEvalInterval = 0.5f;   // 타겟 재평가 주기 (초)

    // ── 근접 콤보 패턴 ───────────────────────────────────────────────────────
    int   comboHitCount      = 3;      // 콤보 타수 (마지막 타는 피해 증가)
    float comboWindupTime    = 0.45f;
    float comboLingerTime    = 0.25f;
    float comboHitRadius     = 3.0f;   // 타격 지점 중심 영역 반경
    float comboDamage        = 12.f;
    float comboFinisherMult  = 1.5f;   // 마지막 타 피해 배율
    float comboCooldown      = 3.f;

    // ── 돌진 패턴 ────────────────────────────────────────────────────────────
    float chargeMinRange     = 9.f;    // 이 거리 이상일 때만 돌진 발동
    float chargeAimTime      = 0.9f;   // 조준 + 경로 텔레그래프 시간
    float chargeSpeedMult    = 4.f;    // 돌진 속도 = moveSpeed x 배율
    float chargeOvershoot    = 4.f;    // 타겟 위치를 지나치는 추가 거리
    float chargeHitRadius    = 2.2f;
    float chargeDamage       = 25.f;
    float chargeStopTime     = 0.5f;   // 돌진 후 경직 시간
    float chargeCooldown     = 8.f;
};

// 독립 보스 클래스. 스쿼드/Platoon 인프라를 쓰지 않고 자체 BT로 행동한다.
// 블랙보드는 별도 클래스 없이 FinalBoss 멤버가 겸하며,
// 리프 노드가 FinalBoss& 를 통해 읽고 쓴다.
class FinalBoss : public Actor {
public:
    FinalBoss(const std::string& name, const Vec3& pos, const FinalBossConfig& cfg);

    void update(float dt, Room& room) override;
    const char* typeName() const override { return "FinalBoss"; }

    // ── 리프 노드용 헬퍼 ─────────────────────────────────────────────────────
    const FinalBossConfig& getConfig() const { return cfg_; }
    Player* resolveTarget(Room& room) const;        // 생존 검증 포함, 없으면 nullptr
    float   distanceToTarget(Room& room) const;     // 타겟 없으면 매우 큰 값
    void    moveToward(const Vec3& dest, float speedMult, float dt);
    void    faceToward(const Vec3& pos);

    // 리프 노드가 실행될 때 호출 — update()가 직전 틱과 비교해 전환 로그를 남긴다
    void reportActiveLeaf(const char* leafName) { currentLeaf_ = leafName; }
    void setActionProgress(float p) { actionProgress_ = p; }

    // ── 스냅샷 접근자 ────────────────────────────────────────────────────────
    const char* getActiveLeafName()  const { return activeLeaf_; }
    uint32_t    getTargetId()        const { return targetId_; }
    float       getActionProgress()  const { return actionProgress_; }
    float       getAttackRange()     const { return cfg_.attackRange; }

private:
    // ── 내부 단계 ────────────────────────────────────────────────────────────
    void buildTree();
    void evaluateTarget(float dt, Room& room);

    // 점수 함수: 거리 역수 + 저HP 가중치.
    // 위협(threat) 가중치는 다수 플레이어 확장 시 이 함수에 추가한다.
    float scoreTarget(const Player& player) const;

    // ── 데이터 ───────────────────────────────────────────────────────────────
    FinalBossConfig cfg_;
    Vec3            spawnPos_;
    float           moveSpeed_;

    uint32_t targetId_{ 0 };          // 0 = 타겟 없음
    float    targetEvalTimer_{ 0.f };

    std::unique_ptr<BtNode> root_;
    const char* activeLeaf_{ "None" };    // 직전 틱 확정 리프 이름
    const char* currentLeaf_{ nullptr };  // 이번 틱 보고값
    float       actionProgress_{ 0.f };   // 0~1, 활성 액션 진행률 (시각화용)
};

} // namespace sim
