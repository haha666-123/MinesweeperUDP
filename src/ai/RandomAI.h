#pragma once
// ============================================================================
//  随机 AI
//  ---------------------------------------------------------------------------
//  从所有可翻开的格子里均匀随机选一个。
//  这不是「傻」——在没有信息的情况下，随机确实是理论最优（所有格风险相同）。
//  但在有信息时它不会利用，所以作为对照组存在。
// ============================================================================
#include "IAI.h"

namespace ms {

class RandomAI : public IAI {
public:
    AIDecision decide(const GameState& st, std::mt19937& rng) override;

    [[nodiscard]] const char* name() const override { return "Random"; }
};

} // namespace ms
