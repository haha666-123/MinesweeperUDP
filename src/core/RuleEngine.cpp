#include "RuleEngine.h"

#include <algorithm>
#include <deque>

namespace ms {

constexpr int RuleEngine::kInterfereTimeCost;

// ===========================================================================
//  公开接口
// ===========================================================================

TurnResult RuleEngine::apply(GameState& st, const Action& act)
{
    if (st.isOver())
        return TurnResult::GameOver;

    // 操作方必须是当前玩家（联机时这是防作弊的第一道闸）
    if (act.actor != st.currentPlayer())
        return TurnResult::Invalid;

    if (!st.board().inBounds(act.pos) && act.type != ActionType::Interfere)
        return TurnResult::Invalid;

    switch (act.type) {
    case ActionType::Reveal:    return doReveal(st, act);
    case ActionType::Flag:      return doFlag(st, act);
    case ActionType::Interfere: return doInterfere(st, act);
    }

    return TurnResult::Invalid;
}

// ---------------------------------------------------------------------------
//  左键：翻开
// ---------------------------------------------------------------------------
TurnResult RuleEngine::doReveal(GameState& st, const Action& act)
{
    const Cell& c = st.cellAt(act.pos);
    if (!c.isHidden())
        return TurnResult::Invalid;      // 已翻开
    if (c.hasFlag())
        return TurnResult::Invalid;      // 先取消旗

    // ---- 踩雷
    if (st.board().hasMine(act.pos)) {
        Cell nc = c;
        nc.state = CellState::Exploded;
        st.setCell(act.pos, nc);
        st.player(act.actor).lives     -= 1;
        st.player(act.actor).minedHits += 1;

        checkGameOver(st);
        if (st.isOver())
            return TurnResult::GameOver;
        beginTurn(st);
        return TurnResult::TurnEnded;
    }

    // ---- 安全格
    const int adjacent = st.board().adjacentMines(act.pos);
    Cell nc = c;
    nc.state = CellState::Revealed;
    st.setCell(act.pos, nc);
    st.player(act.actor).score += 1;
    st.addRevealedSafe(1);

    // 周围无雷 -> 洪泛展开
    if (adjacent == 0) {
        const int extra = floodReveal(st, act.pos);
        st.player(act.actor).score += extra;
        st.addRevealedSafe(extra);
    }

    checkGameOver(st);
    if (st.isOver())
        return TurnResult::GameOver;
    beginTurn(st);
    return TurnResult::TurnEnded;
}

// ---------------------------------------------------------------------------
//  右键：插旗 / 取消旗（可覆盖对手的旗——这就是「干扰」的一种）
//  注意：插旗**不结束回合**。一次回合内可以连续标记多个格子，
//        只有「翻开格子」才把回合交给对手。这样玩家可以利用回合时间做完整推理。
// ---------------------------------------------------------------------------
TurnResult RuleEngine::doFlag(GameState& st, const Action& act)
{
    Cell c = st.cellAt(act.pos);
    if (!c.isHidden())
        return TurnResult::Invalid;

    const auto mine = static_cast<std::uint8_t>(act.actor);
    if (c.flagOwner == mine)
        c.flagOwner = 0;                 // 再点一次取消
    else
        c.flagOwner = mine;              // 新插或覆盖对手

    st.setCell(act.pos, c);

    // 回合不变、时间不重置：玩家可以继续在同一回合里操作
    return TurnResult::Ok;
}

// ---------------------------------------------------------------------------
//  干扰：消耗 1 次机会 + 3 秒，对对手施加效果
// ---------------------------------------------------------------------------
TurnResult RuleEngine::doInterfere(GameState& st, const Action& act)
{
    if (!st.config().allowInterfere)
        return TurnResult::Invalid;
    if (st.player(act.actor).interfereLeft <= 0)
        return TurnResult::Invalid;

    // 先检查时间是否足够——不够就不能扣次数（旧版的 bug）
    if (st.remainingSeconds() <= kInterfereTimeCost)
        return TurnResult::Invalid;

    st.player(act.actor).interfereLeft -= 1;
    st.setRemainingSeconds(st.remainingSeconds() - kInterfereTimeCost);

    // 施加给对手的效果：让对手下一回合少 3 秒
    const int foe = (act.actor == 1 ? 2 : 1);
    st.setPendingPenalty(foe, kInterfereTimeCost);

    beginTurn(st);
    return TurnResult::TurnEnded;
}

// ---------------------------------------------------------------------------
void RuleEngine::onTurnTimeout(GameState& st)
{
    if (st.isOver())
        return;
    beginTurn(st);
}

void RuleEngine::beginTurn(GameState& st)
{
    if (st.isOver())
        return;

    // 换人
    st.setCurrentPlayer(st.currentPlayer() == 1 ? 2 : 1);
    st.incTurnNumber();

    // 新回合的时间 = 基准时间 - 对手施加的惩罚
    const int penalty = st.consumePenalty(st.currentPlayer());
    int secs = st.config().turnSeconds - penalty;
    if (secs < 3)
        secs = 3;                        // 最少留 3 秒，避免被罚到无法操作
    st.setRemainingSeconds(secs);

    // 回合上限
    if (st.config().maxTurns > 0 && st.turnNumber() > st.config().maxTurns) {
        st.evaluateWinner();
        st.setOver(true);
    }
}

bool RuleEngine::canInterfere(const GameState& st) const
{
    return st.config().allowInterfere &&
           !st.isOver() &&
           st.player(st.currentPlayer()).interfereLeft > 0 &&
           st.remainingSeconds() > kInterfereTimeCost;
}

// ===========================================================================
//  洪泛翻开：从 start 出发，把所有相邻的「0 雷格」及其边界一起翻开
// ===========================================================================
int RuleEngine::floodReveal(GameState& st, Pos start)
{
    int opened = 0;
    std::deque<Pos> q;
    q.push_back(start);

    while (!q.empty()) {
        const Pos cur = q.front();
        q.pop_front();

        for (const Pos& n : st.board().neighbors(cur)) {
            Cell c = st.cellAt(n);
            if (!c.isHidden() || c.hasFlag())
                continue;                    // 已翻开或有旗，跳过
            if (st.board().hasMine(n))
                continue;                    // 不自动翻开雷

            c.state = CellState::Revealed;
            st.setCell(n, c);
            ++opened;

            if (st.board().adjacentMines(n) == 0)
                q.push_back(n);              // 继续向外扩散
        }
    }
    return opened;
}

void RuleEngine::checkGameOver(GameState& st)
{
    // 生命归零
    const int l1 = st.player(1).lives;
    const int l2 = st.player(2).lives;
    if (l1 <= 0 || l2 <= 0) {
        if (l1 <= 0 && l2 > 0)      st.setWinner(Winner::Player2);
        else if (l2 <= 0 && l1 > 0) st.setWinner(Winner::Player1);
        else                        st.evaluateWinner();
        st.setOver(true);
        return;
    }

    // 安全格全部翻开
    if (st.revealedSafeCount() >= st.board().safeCount()) {
        st.evaluateWinner();
        st.setOver(true);
    }
}

// ===========================================================================
//  查询接口
// ===========================================================================
RuleEngine::RevealPreview RuleEngine::preview(const GameState& st, Pos p) const
{
    RevealPreview out;
    if (!st.board().inBounds(p))
        return out;

    const Cell& c = st.cellAt(p);
    if (!c.isHidden() || c.hasFlag())
        return out;

    out.valid  = true;
    out.isMine = st.board().hasMine(p);
    if (out.isMine)
        return out;

    out.adjacent = st.board().adjacentMines(p);
    if (out.adjacent == 0) {
        for (const Pos& n : st.board().neighbors(p)) {
            const Cell& nc = st.cellAt(n);
            if (nc.isHidden() && !nc.hasFlag() && !st.board().hasMine(n))
                ++out.floodCells;
        }
    }
    return out;
}

std::vector<Pos> RuleEngine::revealableCells(const GameState& st) const
{
    std::vector<Pos> out;
    const int w = st.board().width();
    const int h = st.board().height();
    out.reserve(static_cast<std::size_t>(w * h));

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Pos p{ x, y };
            const Cell& c = st.cellAt(p);
            if (c.isHidden() && !c.hasFlag())
                out.push_back(p);
        }
    }
    return out;
}

} // namespace ms
