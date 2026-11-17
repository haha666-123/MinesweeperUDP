#pragma once
// ============================================================================
//  基础类型定义
//  ---------------------------------------------------------------------------
//  本项目 core 层不依赖任何图形库（不含 SDL），因此可以在控制台里被单元测试。
//  所有类型都用 enum class，避免隐式转换。
// ============================================================================
#include <cstdint>
#include <ostream>
#include <string>

namespace ms {

// ---------------------------------------------------------------- 格子状态
enum class CellState : std::uint8_t {
    Hidden = 0,     // 未翻开
    Revealed,       // 已翻开（安全格）
    Exploded,       // 已翻开且是雷（踩中）
};

// ---------------------------------------------------------------- 单元格
// 注意：旗帜与「标记归属」是两回事——任何一方都可以标记任意格子，
// 标记对所有玩家可见，但记录是谁标的（用于界面着色与干扰机制）。
struct Cell {
    bool      hasMine   = false;
    CellState state     = CellState::Hidden;
    // 哪一方插了旗：0 = 无人，1 = 玩家1，2 = 玩家2
    std::uint8_t flagOwner = 0;

    [[nodiscard]] bool isHidden() const { return state == CellState::Hidden; }
    [[nodiscard]] bool isRevealed() const { return state != CellState::Hidden; }
    [[nodiscard]] bool hasFlag() const { return flagOwner != 0; }
};

// ---------------------------------------------------------------- 坐标
struct Pos {
    int x = 0;
    int y = 0;

    constexpr Pos() = default;
    constexpr Pos(int xx, int yy) : x(xx), y(yy) {}

    friend constexpr bool operator==(const Pos& a, const Pos& b)
    {
        return a.x == b.x && a.y == b.y;
    }
    friend constexpr bool operator!=(const Pos& a, const Pos& b) { return !(a == b); }
};

// ---------------------------------------------------------------- 操作类型
enum class ActionType : std::uint8_t {
    Reveal = 0,     // 左键：翻开
    Flag,           // 右键：插旗 / 取消插旗
    Interfere,      // 干扰对手（消耗自身回合时间）
};

// ---------------------------------------------------------------- 一次操作
struct Action {
    ActionType type = ActionType::Reveal;
    Pos        pos{};
    int        actor = 1;       // 操作发起方：1 或 2

    // 干扰类型：见 RuleEngine::applyInterfere
    // 0 = 无，1 = 迷雾（对手一格短暂隐身），2 = 冻结（对手本回合少 3 秒）
    int interfereKind = 0;
};

// ---------------------------------------------------------------- 回合结果
enum class TurnResult : std::uint8_t {
    Ok = 0,             // 操作生效，回合继续
    TurnEnded,          // 操作生效，回合结束（换人）
    Invalid,            // 非法操作（格子已翻开、不属于你等）
    GameOver,           // 操作导致游戏结束
};

// ---------------------------------------------------------------- 胜负
enum class Winner : std::uint8_t {
    None = 0,
    Player1,
    Player2,
    Draw,
};

const char* toString(Winner w);
const char* toString(TurnResult r);
const char* toString(ActionType a);

} // namespace ms
