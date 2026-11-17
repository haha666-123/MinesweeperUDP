#include "ProbabilityAI.h"

#include <algorithm>
#include <unordered_set>

namespace ms {

namespace {

// 用于去重的 Pos 哈希
struct PosHash {
    std::size_t operator()(const Pos& p) const noexcept
    {
        return static_cast<std::size_t>(p.y) * 73856093u ^
               static_cast<std::size_t>(p.x) * 19349663u;
    }
};

bool contains(const std::vector<Pos>& v, const Pos& p)
{
    return std::find(v.begin(), v.end(), p) != v.end();
}

void addUnique(std::vector<Pos>& v, const Pos& p)
{
    if (!contains(v, p))
        v.push_back(p);
}

} // namespace

// ===========================================================================
//  约束构建
// ===========================================================================
std::vector<ProbabilityAI::Constraint>
ProbabilityAI::buildConstraints(const GameState& st)
{
    std::vector<Constraint> out;

    for (int y = 0; y < st.board().height(); ++y) {
        for (int x = 0; x < st.board().width(); ++x) {
            const Pos p{ x, y };
            const Cell& c = st.cellAt(p);

            // 只对「已翻开且非雷」的格子建约束
            if (c.state != CellState::Revealed)
                continue;

            const int num = st.board().adjacentMines(p);
            if (num <= 0)
                continue;               // 0 雷格周围没有约束信息

            Constraint cons;
            cons.mines = num;

            int flagged = 0;
            for (const Pos& n : st.board().neighbors(p)) {
                const Cell& nc = st.cellAt(n);
                if (nc.hasFlag()) {
                    ++flagged;
                } else if (nc.isHidden()) {
                    cons.cells.push_back(n);
                }
            }

            // 全部邻居都确定了，这条约束没有新信息
            if (cons.cells.empty())
                continue;

            cons.mines -= flagged;      // 剩余未知格里应有这么多雷
            if (cons.mines < 0)
                cons.mines = 0;         // 旗插多了（玩家标错），容错处理

            out.push_back(std::move(cons));
        }
    }
    return out;
}

// ===========================================================================
//  子集推理
// ===========================================================================
void ProbabilityAI::reduceBySubset(const std::vector<Constraint>& cons,
                                   std::vector<Pos>& outMines,
                                   std::vector<Pos>& outSafe)
{
    for (std::size_t i = 0; i < cons.size(); ++i) {
        for (std::size_t j = 0; j < cons.size(); ++j) {
            if (i == j)
                continue;

            const auto& a = cons[i];    // 小集合
            const auto& b = cons[j];    // 大集合
            if (a.cells.size() >= b.cells.size())
                continue;

            // a 是否完全包含于 b
            bool subset = true;
            for (const Pos& p : a.cells) {
                if (!contains(b.cells, p)) { subset = false; break; }
            }
            if (!subset)
                continue;

            // b \ a 的雷数 = b.mines - a.mines
            const int extraMines = b.mines - a.mines;
            const int extraCells = static_cast<int>(b.cells.size() - a.cells.size());

            if (extraMines == extraCells) {
                // b 中不属于 a 的格子全是雷
                for (const Pos& p : b.cells)
                    if (!contains(a.cells, p))
                        addUnique(outMines, p);
            } else if (extraMines == 0) {
                // b 中不属于 a 的格子全安全
                for (const Pos& p : b.cells)
                    if (!contains(a.cells, p))
                        addUnique(outSafe, p);
            }
        }
    }
}

// ===========================================================================
//  统计算法（对外暴露，便于测试）
// ===========================================================================
ProbabilityAI::Analysis ProbabilityAI::analyze(const GameState& st)
{
    Analysis an;

    const auto cons = buildConstraints(st);

    std::vector<Pos> mines, safe;

    // ---- 第一轮：单约束
    for (const auto& c : cons) {
        const int unknown = static_cast<int>(c.cells.size());
        if (c.mines <= 0) {
            for (const Pos& p : c.cells)
                addUnique(safe, p);
        } else if (c.mines >= unknown) {
            for (const Pos& p : c.cells)
                addUnique(mines, p);
        }
    }

    // ---- 第二轮：子集推理
    reduceBySubset(cons, mines, safe);

    // 同一个格子不可能既必雷又必安全（说明玩家标错旗了），以「安全」为准
    for (auto it = mines.begin(); it != mines.end();) {
        if (contains(safe, *it))
            it = mines.erase(it);
        else
            ++it;
    }

    an.certainMines = static_cast<int>(mines.size());
    an.certainSafe  = static_cast<int>(safe.size());

    // 剩余可翻开的格子中，有多少未被约束覆盖
    int unknownTotal = 0;
    for (int y = 0; y < st.board().height(); ++y)
        for (int x = 0; x < st.board().width(); ++x) {
            const Pos p{ x, y };
            const Cell& c = st.cellAt(p);
            if (c.isHidden() && !c.hasFlag())
                ++unknownTotal;
        }
    an.guessed = std::max(0, unknownTotal - an.certainMines - an.certainSafe);

    return an;
}

// ===========================================================================
//  决策
// ===========================================================================
AIDecision ProbabilityAI::decide(const GameState& st, std::mt19937& rng)
{
    AIDecision d;

    const auto cons = buildConstraints(st);

    std::vector<Pos> safe, mines;

    // ---- 第一轮：单约束
    for (const auto& c : cons) {
        const int unknown = static_cast<int>(c.cells.size());
        if (c.mines <= 0) {
            for (const Pos& p : c.cells)
                addUnique(safe, p);
        } else if (c.mines >= unknown) {
            for (const Pos& p : c.cells)
                addUnique(mines, p);
        }
    }

    // ---- 第二轮：子集推理
    reduceBySubset(cons, mines, safe);

    for (auto it = mines.begin(); it != mines.end();) {
        if (contains(safe, *it))
            it = mines.erase(it);
        else
            ++it;
    }

    // 优先级 1：确定安全的格子，直接翻
    if (!safe.empty()) {
        std::uniform_int_distribution<std::size_t> dist(0, safe.size() - 1);
        d.valid  = true;
        d.type   = ActionType::Reveal;
        d.pos    = safe[dist(rng)];
        d.reason = "约束推理：该格必安全";
        return d;
    }

    // 优先级 2：确定是雷的格子，插旗（比盲目翻格更有价值）
    if (!mines.empty()) {
        std::uniform_int_distribution<std::size_t> dist(0, mines.size() - 1);
        d.valid  = true;
        d.type   = ActionType::Flag;
        d.pos    = mines[dist(rng)];
        d.reason = "约束推理：该格必是雷";
        return d;
    }

    // 优先级 3：没有确定信息，用「全局均匀概率」挑风险最低的
    //   剩余雷数 = 总雷数 - 已标旗数
    int flagged = 0;
    std::vector<Pos> unknown;
    for (int y = 0; y < st.board().height(); ++y) {
        for (int x = 0; x < st.board().width(); ++x) {
            const Pos p{ x, y };
            const Cell& c = st.cellAt(p);
            if (c.hasFlag())
                ++flagged;
            else if (c.isHidden())
                unknown.push_back(p);
        }
    }

    if (unknown.empty())
        return d;                       // 无格可翻

    // 开局第一手：角落的期望信息量略高（周围 3 格，触发洪泛的概率大）
    // 但这里不做复杂启发，直接按均匀概率随机（保持可预测、易测试）
    std::uniform_int_distribution<std::size_t> dist(0, unknown.size() - 1);
    d.valid  = true;
    d.type   = ActionType::Reveal;
    d.pos    = unknown[dist(rng)];

    const int remainMines = std::max(0, st.board().mineCount() - flagged);
    const double p = static_cast<double>(remainMines) /
                     static_cast<double>(unknown.size());
    char buf[128];
    std::snprintf(buf, sizeof(buf), "无确定信息，均匀猜测（雷概率 %.1f%%）", p * 100.0);
    d.reason = buf;
    return d;
}

} // namespace ms
