#include "Board.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ms {

void Board::shuffleIndices(std::vector<int>& v, std::uint32_t seed)
{
    // 用 mt19937 而不是 rand()：可复现、分布质量好。
    // 联机时双方用同一个 seed 生成同一张雷图，避免同步整张雷区。
    std::mt19937 rng(seed);
    for (int i = static_cast<int>(v.size()) - 1; i > 0; --i) {
        std::uniform_int_distribution<int> dist(0, i);
        const int j = dist(rng);
        std::swap(v[i], v[j]);
    }
}

void Board::generate(int mapW, int mapH, double mineRatio, std::uint32_t seed)
{
    m_w = std::max(1, mapW);
    m_h = std::max(1, mapH);

    const int total = m_w * m_h;

    // 雷数至少 1，最多留 1 个安全格
    double ratio = mineRatio;
    if (ratio <= 0.0 || ratio >= 1.0)
        ratio = 0.20;
    m_mines = static_cast<int>(std::ceil(total * ratio));
    m_mines = std::max(1, std::min(total - 1, m_mines));

    m_mine.assign(static_cast<std::size_t>(total), false);

    std::vector<int> idx(static_cast<std::size_t>(total));
    std::iota(idx.begin(), idx.end(), 0);
    shuffleIndices(idx, seed);

    for (int i = 0; i < m_mines; ++i)
        m_mine[static_cast<std::size_t>(idx[static_cast<std::size_t>(i)])] = true;
}

bool Board::hasMine(Pos p) const
{
    if (!inBounds(p))
        return false;
    return m_mine[static_cast<std::size_t>(index(p))];
}

std::vector<Pos> Board::neighbors(Pos p) const
{
    std::vector<Pos> out;
    out.reserve(8);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0)
                continue;
            const Pos q{ p.x + dx, p.y + dy };
            if (inBounds(q))
                out.push_back(q);
        }
    }
    return out;
}

int Board::adjacentMines(Pos p) const
{
    if (!inBounds(p))
        return 0;
    int n = 0;
    for (const Pos& q : neighbors(p))
        if (hasMine(q))
            ++n;
    return n;
}

std::vector<Pos> Board::minePositions() const
{
    std::vector<Pos> out;
    out.reserve(static_cast<std::size_t>(m_mines));
    for (int i = 0; i < cellCount(); ++i)
        if (m_mine[static_cast<std::size_t>(i)])
            out.push_back(fromIndex(i));
    return out;
}

} // namespace ms
