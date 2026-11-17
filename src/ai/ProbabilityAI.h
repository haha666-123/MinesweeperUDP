#pragma once
// ============================================================================
//  概率推理 AI（核心算法，面试重点）
//  ---------------------------------------------------------------------------
//  思路：经典扫雷求解器的「约束推理」。
//
//  一、单约束推理（确定性命中）
//     对每个已翻开的数字格 n，它的邻居中：
//        unknown = 未翻开且无旗的格子数
//        flagged = 已插旗的格子数
//        n       = 数字（周围雷总数）
//     于是剩下的未知格中，必有 n - flagged 个雷。
//       * 若 n - flagged == unknown  -> 这些未知格全是雷（可以安全插旗）
//       * 若 n - flagged == 0        -> 这些未知格全安全（可以放心点）
//
//  二、全局概率
//     对没有约束覆盖的格子，用「剩余雷数 / 剩余未知格数」估计风险。
//     剩余雷数 = 总雷数 - 已标旗数（旗不一定全对，但作为估计足够）。
//
//  三、多约束交叉（子集推理）
//     若约束 A 的未知集合是约束 B 的子集，且两者所需雷数相同，
//     则 B \ A 中的所有格子必然安全。这一步能解出很多「单约束」解不出的局面。
//
//  复杂度：单约束 O(N * 8)，子集推理 O(C^2 * |U|)，在 30×30 以内完全够用。
//  不做精确的组合枚举（那是指数级），因为在工程上收益远小于复杂度。
//
//  面试可讲点：
//    * 为什么不用 DFS 爆破：状态空间指数增长，且要赌「哪次猜测是对的」
//    * 为什么先插旗再翻格：把确定性信息尽快固化，减少后续搜索空间
//    * 概率用「约束内加权」而不是「全局均匀」：前者更准
// ============================================================================
#include "IAI.h"

#include <vector>

namespace ms {

class ProbabilityAI : public IAI {
public:
    AIDecision decide(const GameState& st, std::mt19937& rng) override;

    [[nodiscard]] const char* name() const override { return "Probability"; }

    // ---- 供测试与界面显示用的统计
    struct Analysis {
        int certainMines   = 0;    // 推理出「必是雷」的格子数
        int certainSafe    = 0;    // 推理出「必安全」的格子数
        int guessed        = 0;    // 只能靠概率猜的格子数
        double bestProb    = 0.0;  // 所选格子的雷概率
    };

    //! 静态分析当前局面（不修改状态，给测试与调试用）
    [[nodiscard]] static Analysis analyze(const GameState& st);

private:
    // 一组约束：某些未知格中恰有 mines 个雷
    struct Constraint {
        std::vector<Pos> cells;
        int              mines = 0;
    };

    static std::vector<Constraint> buildConstraints(const GameState& st);

    //! 子集推理：把能确定的雷/安全格补充进结果
    static void reduceBySubset(const std::vector<Constraint>& cons,
                               std::vector<Pos>& outMines,
                               std::vector<Pos>& outSafe);
};

} // namespace ms
