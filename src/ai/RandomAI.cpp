#include "RandomAI.h"

namespace ms {

AIDecision RandomAI::decide(const GameState& st, std::mt19937& rng)
{
    AIDecision d;

    std::vector<Pos> cells;
    cells.reserve(static_cast<std::size_t>(st.board().cellCount()));

    for (int y = 0; y < st.board().height(); ++y) {
        for (int x = 0; x < st.board().width(); ++x) {
            const Pos p{ x, y };
            const Cell& c = st.cellAt(p);
            if (c.isHidden() && !c.hasFlag())
                cells.push_back(p);
        }
    }

    if (cells.empty())
        return d;

    std::uniform_int_distribution<std::size_t> dist(0, cells.size() - 1);
    d.valid  = true;
    d.type   = ActionType::Reveal;
    d.pos    = cells[dist(rng)];
    d.reason = "随机选择";
    return d;
}

} // namespace ms
