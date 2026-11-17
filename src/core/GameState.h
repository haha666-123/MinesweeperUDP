#pragma once
// ============================================================================
//  GameState：一局游戏的完整状态
//  ---------------------------------------------------------------------------
//  设计要点：
//  * 这是「值对象」——可以被整体序列化、拷贝、比对，联机时直接作为快照传。
//  * 不持有任何指针/资源，纯 POD + vector，clone 成本低。
//  * 规则逻辑放在 RuleEngine，这里只存数据 + 提供查询。
//
//  玩法（对齐你的规则）：
//    - 双方轮流行动，各自有回合时间上限（可设置）
//    - 左键翻开：踩雷扣 1 点生命；安全格 +1 分
//    - 右键插旗：任何一方可标记任意格，双方可见，记录归属
//    - 生命归零 或 雷区清空 -> 结束，分高者胜
//    - 回合内可干扰对手（消耗时间/次数，见 RuleEngine）
// ============================================================================
#include "Board.h"
#include "Types.h"

#include <cstdint>
#include <vector>

namespace ms {

struct PlayerState {
    int  id        = 1;        // 1 或 2
    int  lives     = 3;        // 生命值
    int  score     = 0;        // 安全格数
    int  minedHits = 0;        // 踩雷次数
    int  interfereLeft = 1;    // 本局剩余干扰次数
};

struct GameConfig {
    int      mapW        = 12;
    int      mapH        = 12;
    double   mineRatio   = 0.18;
    int      startLives  = 3;
    int      turnSeconds = 20;      // 每回合思考时间
    int      maxTurns    = 0;       // 0 = 不限
    bool     allowInterfere = true;
    std::uint32_t seed   = 0;       // 0 = 随机生成
};

class GameState {
public:
    GameState() = default;

    //! 开始新一局；seed 相同则雷区相同（联机用）
    void reset(const GameConfig& cfg, std::uint32_t seed);

    // ---------------------------------------------------------- 只读查询
    [[nodiscard]] const GameConfig& config() const { return m_cfg; }
    [[nodiscard]] const Board& board() const { return m_board; }
    [[nodiscard]] std::uint32_t seed() const { return m_seed; }

    //! 越界或未初始化时返回一个静态空 Cell，避免 UB（防御式编程）
    [[nodiscard]] const Cell& cellAt(Pos p) const
    {
        const auto idx = static_cast<std::size_t>(p.y * m_cfg.mapW + p.x);
        if (p.x < 0 || p.y < 0 || p.x >= m_cfg.mapW || p.y >= m_cfg.mapH ||
            idx >= m_cells.size()) {
            static const Cell kEmpty{};
            return kEmpty;
        }
        return m_cells[idx];
    }
    [[nodiscard]] const std::vector<Cell>& cells() const { return m_cells; }
    //! 棋盘是否已完成初始化（客户端尚未收到 Welcome 时为 false）
    [[nodiscard]] bool ready() const
    {
        return !m_cells.empty() &&
               m_cells.size() == static_cast<std::size_t>(m_cfg.mapW * m_cfg.mapH) &&
               m_board.width() == m_cfg.mapW && m_board.height() == m_cfg.mapH;
    }

    [[nodiscard]] const PlayerState& player(int id) const { return m_players[id == 1 ? 0 : 1]; }
    [[nodiscard]] PlayerState& player(int id) { return m_players[id == 1 ? 0 : 1]; }

    [[nodiscard]] int currentPlayer() const { return m_currentPlayer; }
    [[nodiscard]] bool isOver() const { return m_over; }
    [[nodiscard]] Winner winner() const { return m_winner; }
    [[nodiscard]] int revealedSafeCount() const { return m_revealedSafe; }
    [[nodiscard]] int turnNumber() const { return m_turnNumber; }
    [[nodiscard]] int remainingSeconds() const { return m_remainSeconds; }

    //! 双方分数相同时按「剩余生命多」判胜，再相同则平局
    void evaluateWinner();

    // ---------------------------------------------------------- 供 RuleEngine 修改
    void setCell(Pos p, const Cell& c) { m_cells[toIndex(p)] = c; }
    void setCurrentPlayer(int id) { m_currentPlayer = (id == 1 ? 1 : 2); }
    void setOver(bool v) { m_over = v; }
    void setWinner(Winner w) { m_winner = w; }
    void addRevealedSafe(int n) { m_revealedSafe += n; }
    void setRemainingSeconds(int s) { m_remainSeconds = s; }
    void decRemainingSeconds() { if (m_remainSeconds > 0) --m_remainSeconds; }
    void incTurnNumber() { ++m_turnNumber; }

    //! 给某玩家登记「下回合时间惩罚」（干扰机制）
    void setPendingPenalty(int playerId, int seconds)
    {
        m_pendingPenalty[playerId == 1 ? 0 : 1] = seconds;
    }
    //! 取出并清零该玩家的惩罚（新回合开始时调用）
    int consumePenalty(int playerId)
    {
        int& p = m_pendingPenalty[playerId == 1 ? 0 : 1];
        const int v = p;
        p = 0;
        return v;
    }

    //! 序列化为紧凑字节串（用于 UDP 快照）；不包含 Board 的雷位
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    //! 从快照恢复；返回 false 表示数据损坏
    bool deserialize(const std::uint8_t* data, std::size_t len);

    [[nodiscard]] std::size_t snapshotSize() const;

private:
    [[nodiscard]] std::size_t toIndex(Pos p) const
    {
        return static_cast<std::size_t>(p.y * m_cfg.mapW + p.x);
    }

    GameConfig          m_cfg;
    Board               m_board;
    std::vector<Cell>   m_cells;
    PlayerState         m_players[2];
    std::uint32_t       m_seed          = 0;
    int                 m_currentPlayer = 1;
    int                 m_turnNumber    = 1;
    int                 m_remainSeconds = 20;
    int                 m_revealedSafe  = 0;
    bool                m_over          = false;
    Winner              m_winner        = Winner::None;
    int                 m_pendingPenalty[2] = { 0, 0 };   // 干扰留下的时间惩罚
};

} // namespace ms
