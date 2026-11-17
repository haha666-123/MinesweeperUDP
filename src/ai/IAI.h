#pragma once
// ============================================================================
//  AI 接口
//  ---------------------------------------------------------------------------
//  三档难度用同一个接口（策略模式）：
//    RandomAI      —— 随机选一个可翻开的格子（最朴素，可作对照组）
//    ProbabilityAI —— 用「周围数字」做约束推理，算出必雷/必安全格（真算法）
//    LlmAI         —— 把局面描述给大模型选格；失败自动降级到 ProbabilityAI
//
//  用接口而不是 switch 的好处：
//    * 界面层只持有 IAI*，换难度不改调用代码
//    * 测试可以注入假 AI，验证「AI 不会点已翻开的格子」
// ============================================================================
#include "../core/GameState.h"

#include <cstdint>
#include <random>
#include <string>

namespace ms {

// ---------------------------------------------------------------- 决策
struct AIDecision {
    bool        valid  = false;
    ActionType  type   = ActionType::Reveal;
    Pos         pos{};
    std::string reason;         // 给界面显示「AI 在想什么」，也便于调试
};

class IAI {
public:
    virtual ~IAI() = default;

    //! 给出一个决策；无合法选择时返回 valid = false
    virtual AIDecision decide(const GameState& st, std::mt19937& rng) = 0;

    [[nodiscard]] virtual const char* name() const = 0;
};

} // namespace ms
