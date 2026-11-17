#pragma once
// ============================================================================
//  RuleEngine：游戏规则的唯一实现
//  ---------------------------------------------------------------------------
//  所有「操作 -> 状态变化」都经过这里。GameState 只存数据，RuleEngine 定规矩。
//  这样拆分的好处：
//    * 规则可以单元测试（不依赖 SDL）
//    * 联机时 Host 用同一份规则校验客户端请求，天然防作弊
//    * AI 可以用同一个接口模拟「如果点这里会怎样」（见 simulateReveal）
//
//  规则明细（对齐你的设计）：
//    1. 双方轮流，每人回合有时间上限；超时自动换人（视为跳过）
//    2. 左键翻开安全格：+1 分，回合结束
//    3. 左键踩雷：扣 1 生命，展示该雷，回合结束
//    4. 右键插旗：任何一方可标任意未翻开格；再点取消；回合结束
//       （旗帜双方可见，且记录归属用于着色）
//    5. 翻开 0 雷格自动扩散（BFS/DFS 洪泛）
//    6. 生命归零 -> 立即结束；全部安全格翻开 -> 结束
//    7. 结束判定：分高者胜，平分比剩余生命，再平则平局
//    8. 干扰：消耗 1 次机会 + 3 秒时间，对对手施加效果（每局限 1 次）
// ============================================================================
#include "GameState.h"
#include "Types.h"

#include <random>
#include <vector>

namespace ms {

class RuleEngine {
public:
    RuleEngine() = default;

    //! 应用一次操作；返回结果告诉调用方「回合是否结束 / 游戏是否结束」
    TurnResult apply(GameState& st, const Action& act);

    //! 回合超时：换人，不产生其他变化
    void onTurnTimeout(GameState& st);

    //! 开始新回合（重置剩余时间，递增回合号）
    void beginTurn(GameState& st);

    //! 纯查询：假设玩家 p 翻开此处，会得到什么（不改状态，给 AI 与预览用）
    struct RevealPreview {
        bool valid       = false;
        bool isMine      = false;
        int  adjacent    = 0;      // 周围雷数（安全格才有意义）
        int  floodCells  = 0;      // 若是 0 雷格，将连带翻开多少个
    };
    [[nodiscard]] RevealPreview preview(const GameState& st, Pos p) const;

    //! 所有当前可翻开的格子（AI 用）
    [[nodiscard]] std::vector<Pos> revealableCells(const GameState& st) const;

    //! 当前玩家是否还能干扰
    [[nodiscard]] bool canInterfere(const GameState& st) const;

    //! 干扰效果的持续时间（秒）
    static constexpr int kInterfereTimeCost = 3;

private:
    //! 三个操作的具体实现
    TurnResult doReveal(GameState& st, const Action& act);
    TurnResult doFlag(GameState& st, const Action& act);
    TurnResult doInterfere(GameState& st, const Action& act);

    //! 洪泛翻开 0 雷格；返回翻开的格子数
    int floodReveal(GameState& st, Pos start);

    //! 检查并结算游戏结束条件
    void checkGameOver(GameState& st);
};

} // namespace ms
