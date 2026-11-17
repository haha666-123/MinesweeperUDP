#include "GameState.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace ms {

namespace {

// 紧凑的字节读写工具：所有多字节字段都用小端，跨平台一致
inline void putU8(std::vector<std::uint8_t>& v, std::uint8_t b) { v.push_back(b); }

inline void putU16(std::vector<std::uint8_t>& v, std::uint16_t w)
{
    v.push_back(static_cast<std::uint8_t>(w & 0xFF));
    v.push_back(static_cast<std::uint8_t>((w >> 8) & 0xFF));
}

inline void putU32(std::vector<std::uint8_t>& v, std::uint32_t d)
{
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<std::uint8_t>((d >> (8 * i)) & 0xFF));
}

struct Reader {
    const std::uint8_t* p;
    std::size_t         remaining;
    bool                ok = true;

    bool need(std::size_t n)
    {
        if (remaining < n) { ok = false; return false; }
        return true;
    }
    std::uint8_t u8()
    {
        if (!need(1)) return 0;
        const std::uint8_t b = *p++;
        --remaining;
        return b;
    }
    std::uint16_t u16()
    {
        const std::uint16_t lo = u8();
        const std::uint16_t hi = u8();
        return static_cast<std::uint16_t>(lo | (hi << 8));
    }
    std::uint32_t u32()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(u8()) << (8 * i);
        return v;
    }
};

} // namespace

const char* toString(Winner w)
{
    switch (w) {
    case Winner::Player1: return "Player1";
    case Winner::Player2: return "Player2";
    case Winner::Draw:    return "Draw";
    default:              return "None";
    }
}

const char* toString(TurnResult r)
{
    switch (r) {
    case TurnResult::Ok:        return "Ok";
    case TurnResult::TurnEnded: return "TurnEnded";
    case TurnResult::Invalid:   return "Invalid";
    case TurnResult::GameOver:  return "GameOver";
    }
    return "Unknown";
}

const char* toString(ActionType a)
{
    switch (a) {
    case ActionType::Reveal:    return "Reveal";
    case ActionType::Flag:      return "Flag";
    case ActionType::Interfere: return "Interfere";
    }
    return "Unknown";
}

void GameState::reset(const GameConfig& cfg, std::uint32_t seed)
{
    m_cfg = cfg;

    // seed == 0 表示「随机」，用时钟生成一个
    m_seed = seed;
    if (m_seed == 0) {
        m_seed = static_cast<std::uint32_t>(
            std::chrono::system_clock::now().time_since_epoch().count() & 0x7FFFFFFFu);
        if (m_seed == 0) m_seed = 1;
    }

    m_board.generate(m_cfg.mapW, m_cfg.mapH, m_cfg.mineRatio, m_seed);

    m_cells.assign(static_cast<std::size_t>(m_cfg.mapW * m_cfg.mapH), Cell{});

    m_players[0] = PlayerState{};
    m_players[0].id = 1;
    m_players[0].lives = m_cfg.startLives;
    m_players[0].interfereLeft = m_cfg.allowInterfere ? 1 : 0;

    m_players[1] = PlayerState{};
    m_players[1].id = 2;
    m_players[1].lives = m_cfg.startLives;
    m_players[1].interfereLeft = m_cfg.allowInterfere ? 1 : 0;

    m_currentPlayer = 1;
    m_turnNumber    = 1;
    m_remainSeconds = m_cfg.turnSeconds;
    m_revealedSafe  = 0;
    m_over          = false;
    m_winner        = Winner::None;
    m_pendingPenalty[0] = 0;
    m_pendingPenalty[1] = 0;
}

void GameState::evaluateWinner()
{
    const PlayerState& a = m_players[0];
    const PlayerState& b = m_players[1];

    if (a.score != b.score) {
        m_winner = (a.score > b.score) ? Winner::Player1 : Winner::Player2;
        return;
    }
    if (a.lives != b.lives) {
        m_winner = (a.lives > b.lives) ? Winner::Player1 : Winner::Player2;
        return;
    }
    m_winner = Winner::Draw;
}

std::size_t GameState::snapshotSize() const
{
    // 头部 18 字节：
    //   seed4 + w2 + h2 + cur1 + turn2 + remain2 + revealed2 + over1 + winner1 + ratio1
    // 玩家 10 字节：每人 lives1 + score2 + hits1 + interfere1 + penalty1 = 6，共 12
    //   但 interfereLeft 与 penalty 都是 1 字节，lives 1，score 2，minedHits 1 -> 每人 6
    //   -> 两人 12 字节
    // 实际统计（见 serialize 实现）：头部 18 + 玩家 12 + 格子 3N = 30 + 3N
    // 注意：早期版本误写为 29/28，与序列化实现不符，此处以实现为准。
    return 30 + static_cast<std::size_t>(m_cfg.mapW * m_cfg.mapH) * 3;
}

std::vector<std::uint8_t> GameState::serialize() const
{
    std::vector<std::uint8_t> v;
    v.reserve(snapshotSize() + 32);

    // ---- 头部
    putU32(v, m_seed);
    putU16(v, static_cast<std::uint16_t>(m_cfg.mapW));
    putU16(v, static_cast<std::uint16_t>(m_cfg.mapH));
    putU8(v, static_cast<std::uint8_t>(m_currentPlayer));
    putU16(v, static_cast<std::uint16_t>(m_turnNumber));
    putU16(v, static_cast<std::uint16_t>(m_remainSeconds));
    putU16(v, static_cast<std::uint16_t>(m_revealedSafe));
    putU8(v, m_over ? 1 : 0);
    putU8(v, static_cast<std::uint8_t>(m_winner));
    putU8(v, static_cast<std::uint8_t>(
                static_cast<int>(m_cfg.mineRatio * 100 + 0.5) & 0xFF));

    // ---- 玩家
    for (const PlayerState& p : m_players) {
        putU8(v, static_cast<std::uint8_t>(p.lives));
        putU16(v, static_cast<std::uint16_t>(p.score));
        putU8(v, static_cast<std::uint8_t>(p.minedHits));
        putU8(v, static_cast<std::uint8_t>(p.interfereLeft));
        putU8(v, static_cast<std::uint8_t>(m_pendingPenalty[&p - m_players]));
    }

    // ---- 格子：状态 + 旗归属（雷位不传，双方用 seed 各自生成）
    for (const Cell& c : m_cells) {
        putU8(v, static_cast<std::uint8_t>(c.state));
        putU8(v, c.flagOwner);
        putU8(v, static_cast<std::uint8_t>(0));   // 保留
    }

    return v;
}

bool GameState::deserialize(const std::uint8_t* data, std::size_t len)
{
    if (!data || len < 24)
        return false;

    Reader r{ data, len };

    const std::uint32_t seed = r.u32();
    const std::uint16_t w    = r.u16();
    const std::uint16_t h    = r.u16();
    const std::uint8_t  cur  = r.u8();
    const std::uint16_t tn   = r.u16();
    const std::uint16_t rs   = r.u16();
    const std::uint16_t revealed = r.u16();
    const std::uint8_t  over = r.u8();
    const std::uint8_t  win  = r.u8();
    const std::uint8_t  ratioPct = r.u8();

    if (!r.ok || w == 0 || h == 0 || w > 64 || h > 64)
        return false;

    // 用 seed 重建雷区（必须与主机一致）
    // 注意：mineRatio 由调用方在 deserialize 之前通过 config() 设定，
    // 这里若 m_cfg.mineRatio 未设置会退化为 0.20，导致雷区与主机不一致。
    double ratio = (ratioPct > 0) ? (ratioPct / 100.0) : m_cfg.mineRatio;
    if (ratio <= 0.0 || ratio >= 1.0)
        ratio = 0.20;
    m_cfg.mineRatio = ratio;

    Board fresh;
    fresh.generate(w, h, ratio, seed);
    m_board = fresh;
    m_seed  = seed;

    m_cfg.mapW = w;
    m_cfg.mapH = h;

    m_cells.assign(static_cast<std::size_t>(w) * h, Cell{});
    m_players[0] = PlayerState{};
    m_players[1] = PlayerState{};
    m_players[0].id = 1;
    m_players[1].id = 2;

    for (PlayerState& p : m_players) {
        p.lives         = r.u8();
        p.score         = static_cast<int>(r.u16());
        p.minedHits     = r.u8();
        p.interfereLeft = r.u8();
        m_pendingPenalty[&p - m_players] = r.u8();
    }

    if (!r.ok)
        return false;

    // 防御：剩余数据必须够读 N 个格子（每格 3 字节）
    const std::size_t need = static_cast<std::size_t>(w) * h * 3;
    if (r.remaining < need)
        return false;

    for (Cell& c : m_cells) {
        const std::uint8_t st  = r.u8();
        const std::uint8_t own = r.u8();
        (void)r.u8();     // 保留字节
        if (!r.ok)
            return false;
        c.state     = static_cast<CellState>(st > 2 ? 0 : st);
        c.flagOwner = (own > 2 ? 0 : own);
    }

    m_currentPlayer = (cur == 2 ? 2 : 1);
    m_turnNumber    = static_cast<int>(tn);
    m_remainSeconds = static_cast<int>(rs);
    m_revealedSafe  = static_cast<int>(revealed);
    m_over          = (over != 0);
    m_winner        = static_cast<Winner>(win > 3 ? 0 : win);
    return true;
}

} // namespace ms
