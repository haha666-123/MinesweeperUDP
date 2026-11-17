#pragma once
// ============================================================================
//  棋盘：雷区布局 + 周围雷数计算
//  ---------------------------------------------------------------------------
//  这一层只关心「哪里是雷、某格周围有几个雷」，不关心玩家、回合、胜负。
//  职责单一，方便单元测试。
// ============================================================================
#include "Types.h"

#include <random>
#include <vector>

namespace ms {

class Board {
public:
    Board() = default;

    //! 生成 mapW × mapH 的雷区；mineRatio ∈ (0,1)；seed 相同则布局相同（联机需要）
    void generate(int mapW, int mapH, double mineRatio, std::uint32_t seed);

    [[nodiscard]] int width() const { return m_w; }
    [[nodiscard]] int height() const { return m_h; }
    [[nodiscard]] int cellCount() const { return m_w * m_h; }
    [[nodiscard]] int mineCount() const { return m_mines; }
    [[nodiscard]] int safeCount() const { return cellCount() - m_mines; }

    [[nodiscard]] bool inBounds(Pos p) const
    {
        return p.x >= 0 && p.x < m_w && p.y >= 0 && p.y < m_h;
    }
    [[nodiscard]] int index(Pos p) const { return p.y * m_w + p.x; }
    [[nodiscard]] Pos fromIndex(int idx) const { return Pos{ idx % m_w, idx / m_w }; }

    [[nodiscard]] bool hasMine(Pos p) const;
    //! 该格周围 8 格中的雷数（必须是安全格才有意义）
    [[nodiscard]] int adjacentMines(Pos p) const;
    //! 该格所有相邻格（已裁剪到棋盘内）
    [[nodiscard]] std::vector<Pos> neighbors(Pos p) const;

    //! 所有雷的坐标（AI 与调试用）
    [[nodiscard]] std::vector<Pos> minePositions() const;

private:
    int    m_w     = 0;
    int    m_h     = 0;
    int    m_mines = 0;
    std::vector<bool> m_mine;   // size = cellCount()

    // 使用 mt19937 + Fisher-Yates 洗牌，等价于你原来的实现但可复现
    static void shuffleIndices(std::vector<int>& v, std::uint32_t seed);
};

} // namespace ms
