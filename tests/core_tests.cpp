// ============================================================================
//  core 层单元测试（不依赖 SDL）
//  ---------------------------------------------------------------------------
//  这个可执行文件证明：游戏规则可以被独立验证。
//  面试时说「我的规则引擎有 30 项断言测试，不依赖图形库」，比说「能跑起来」有说服力。
// ============================================================================
#include "core/Board.h"
#include "core/GameState.h"
#include "core/RuleEngine.h"

#include <cstdio>
#include <cstring>

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* what)
{
    if (cond) {
        ++g_pass;
        std::printf("  [PASS] %s\n", what);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", what);
    }
}

void section(const char* name)
{
    std::printf("\n[%s]\n", name);
}

ms::GameConfig makeCfg()
{
    ms::GameConfig cfg;
    cfg.mapW        = 8;
    cfg.mapH        = 8;
    cfg.mineRatio   = 0.15;     // 64 * 0.15 = 10 雷
    cfg.startLives  = 3;
    cfg.turnSeconds = 20;
    cfg.seed        = 12345;
    return cfg;
}

} // namespace

int main()
{
    std::printf("=== UdpMinesweeper core tests ===\n");

    // ---------------------------------------------------------------- 1 棋盘
    section("1. Board 雷区生成");
    {
        ms::Board b;
        b.generate(8, 8, 0.15, 12345);
        check(b.width() == 8 && b.height() == 8, "尺寸正确");
        check(b.mineCount() == 10, "雷数 = ceil(64 * 0.15) = 10");
        check(b.safeCount() == 54, "安全格 = 64 - 10 = 54");

        // 同 seed 必须生成同一张图（联机依赖这一点）
        ms::Board b2;
        b2.generate(8, 8, 0.15, 12345);
        bool same = true;
        for (int y = 0; y < 8 && same; ++y)
            for (int x = 0; x < 8 && same; ++x)
                if (b.hasMine({x, y}) != b2.hasMine({x, y}))
                    same = false;
        check(same, "相同 seed 生成相同雷图（可复现）");

        ms::Board b3;
        b3.generate(8, 8, 0.15, 99999);
        bool diff = false;
        for (int y = 0; y < 8 && !diff; ++y)
            for (int x = 0; x < 8 && !diff; ++x)
                if (b.hasMine({x, y}) != b3.hasMine({x, y}))
                    diff = true;
        check(diff, "不同 seed 生成不同雷图");

        // 边界检查
        check(!b.hasMine({-1, 0}) && !b.hasMine({8, 0}), "越界坐标安全返回 false");

        // 邻居数量：角落 3、边 5、内部 8
        check(b.neighbors({0, 0}).size() == 3, "左上角有 3 个邻居");
        check(b.neighbors({7, 0}).size() == 3, "右上角有 3 个邻居");
        check(b.neighbors({0, 3}).size() == 5, "左边缘有 5 个邻居");
        check(b.neighbors({3, 3}).size() == 8, "内部格有 8 个邻居");
    }

    // ---------------------------------------------------------------- 2 状态
    section("2. GameState 初始化与序列化");
    {
        ms::GameState st;
        st.reset(makeCfg(), 12345);
        check(st.player(1).lives == 3 && st.player(2).lives == 3, "双方初始 3 条命");
        check(st.currentPlayer() == 1, "玩家 1 先手");
        check(st.remainingSeconds() == 20, "回合时间 20 秒");
        check(!st.isOver(), "初始未结束");
        check(st.seed() == 12345, "seed 已保存");

        // 序列化往返
        auto dump = st.serialize();
        check(!dump.empty(), "序列化产生数据");
        check(dump.size() == st.snapshotSize(), "序列化长度与 snapshotSize 一致");

        ms::GameState st2;
        st2.reset(makeCfg(), 12345);
        const bool ok = st2.deserialize(dump.data(), dump.size());
        check(ok, "反序列化成功");
        check(st2.currentPlayer() == st.currentPlayer(), "恢复后当前玩家一致");
        check(st2.player(1).lives == st.player(1).lives, "恢复后生命值一致");
        check(st2.board().mineCount() == st.board().mineCount(), "恢复后雷数一致");

        // 损坏数据必须被拒绝
        check(!st2.deserialize(nullptr, 0), "空数据被拒绝");
        check(!st2.deserialize(dump.data(), 5), "截断数据被拒绝");
    }

    // ---------------------------------------------------------------- 3 翻格
    section("3. 翻开规则");
    {
        ms::RuleEngine engine;
        ms::GameState st;
        st.reset(makeCfg(), 12345);

        // 找一个安全格
        ms::Pos safe{-1, -1};
        for (int y = 0; y < 8 && safe.x < 0; ++y)
            for (int x = 0; x < 8 && safe.x < 0; ++x)
                if (!st.board().hasMine({x, y}))
                    safe = {x, y};
        check(safe.x >= 0, "找到安全格");

        const int before = st.player(1).score;
        ms::Action a;
        a.type  = ms::ActionType::Reveal;
        a.pos   = safe;
        a.actor = 1;
        const auto r = engine.apply(st, a);

        check(r == ms::TurnResult::TurnEnded, "翻开安全格后回合结束");
        check(st.player(1).score > before, "玩家 1 得分增加");
        check(st.currentPlayer() == 2, "回合切换到玩家 2");
        check(st.cellAt(safe).isRevealed(), "该格已标记为翻开");
    }

    // ---------------------------------------------------------------- 4 踩雷
    section("4. 踩雷扣血");
    {
        ms::RuleEngine engine;
        ms::GameState st;
        st.reset(makeCfg(), 12345);

        ms::Pos mine{-1, -1};
        for (int y = 0; y < 8 && mine.x < 0; ++y)
            for (int x = 0; x < 8 && mine.x < 0; ++x)
                if (st.board().hasMine({x, y}))
                    mine = {x, y};
        check(mine.x >= 0, "找到雷格");

        ms::Action a;
        a.type  = ms::ActionType::Reveal;
        a.pos   = mine;
        a.actor = 1;
        engine.apply(st, a);

        check(st.player(1).lives == 2, "玩家 1 生命从 3 减到 2");
        check(st.cellAt(mine).state == ms::CellState::Exploded, "雷格状态为 Exploded");
        check(st.player(1).minedHits == 1, "踩雷计数 = 1");
    }

    // ---------------------------------------------------------------- 5 插旗
    section("5. 插旗不结束回合");
    {
        ms::RuleEngine engine;
        ms::GameState st;
        st.reset(makeCfg(), 12345);

        const int firstPlayer = st.currentPlayer();
        const int beforeTime  = st.remainingSeconds();

        ms::Action flag;
        flag.type  = ms::ActionType::Flag;
        flag.pos   = {0, 0};
        flag.actor = firstPlayer;

        const auto r1 = engine.apply(st, flag);
        check(r1 == ms::TurnResult::Ok, "插旗返回 Ok（不是 TurnEnded）");
        check(st.cellAt({0, 0}).flagOwner == firstPlayer, "插旗成功");
        check(st.currentPlayer() == firstPlayer, "插旗后回合不变（仍是同一个人）");
        check(st.remainingSeconds() == beforeTime, "插旗后剩余时间不变");

        // 同一回合内可以连续标记多个格子
        flag.pos = {1, 0};
        engine.apply(st, flag);
        flag.pos = {2, 0};
        engine.apply(st, flag);
        check(st.cellAt({1, 0}).flagOwner == firstPlayer &&
              st.cellAt({2, 0}).flagOwner == firstPlayer,
              "同一回合内可连续插多个旗");
        check(st.currentPlayer() == firstPlayer, "连续插旗后回合仍不变");

        // 再点一次取消
        flag.pos = {0, 0};
        engine.apply(st, flag);
        check(st.cellAt({0, 0}).flagOwner == 0, "同一个人再点取消自己的旗");

        // 对手无法在非自己回合覆盖（回合仍属于第一个人）
        ms::Action foeFlag = flag;
        foeFlag.actor = (firstPlayer == 1 ? 2 : 1);
        foeFlag.pos = {3, 0};
        check(engine.apply(st, foeFlag) == ms::TurnResult::Invalid,
              "非当前玩家不能插旗");

        // 翻开一个安全格 -> 此时才换人
        ms::Pos safe{ -1, -1 };
        for (int y = 0; y < 8 && safe.x < 0; ++y)
            for (int x = 0; x < 8 && safe.x < 0; ++x) {
                const ms::Pos p{ x, y };
                if (!st.board().hasMine(p) && st.cellAt(p).isHidden() &&
                    !st.cellAt(p).hasFlag())
                    safe = p;
            }
        ms::Action rev;
        rev.type = ms::ActionType::Reveal;
        rev.pos = safe;
        rev.actor = firstPlayer;
        check(engine.apply(st, rev) == ms::TurnResult::TurnEnded,
              "翻开格子才结束回合");
        check(st.currentPlayer() != firstPlayer, "翻开后换人");
    }

    // ---------------------------------------------------------------- 6 非法操作
    section("6. 非法操作被拒绝");
    {
        ms::RuleEngine engine;
        ms::GameState st;
        st.reset(makeCfg(), 12345);

        ms::Action a;
        a.type  = ms::ActionType::Reveal;
        a.pos   = {0, 0};
        a.actor = 2;                 // 应该是玩家 1 的回合
        check(engine.apply(st, a) == ms::TurnResult::Invalid, "非当前玩家操作被拒绝");

        a.actor = 1;
        a.pos   = {-5, -5};
        check(engine.apply(st, a) == ms::TurnResult::Invalid, "越界坐标被拒绝");

        // 已翻开的格子
        ms::Action ok;
        ok.type  = ms::ActionType::Reveal;
        ok.actor = 1;
        for (int y = 0; y < 8; ++y) {
            bool found = false;
            for (int x = 0; x < 8 && !found; ++x) {
                if (!st.board().hasMine({x, y})) {
                    ok.pos = {x, y};
                    found = true;
                }
            }
            if (found) break;
        }
        engine.apply(st, ok);
        // 现在轮到玩家 2，切回来后重试同一格
        ok.actor = 2;
        engine.apply(st, ok);        // 让回合走完
        ok.actor = 1;
        check(engine.apply(st, ok) == ms::TurnResult::Invalid, "重复翻开同一格被拒绝");
    }

    // ---------------------------------------------------------------- 7 插旗阻挡
    section("7. 有旗的格子不能翻开");
    {
        ms::RuleEngine engine;
        ms::GameState st;
        st.reset(makeCfg(), 12345);

        ms::Action f;
        f.type  = ms::ActionType::Flag;
        f.pos   = {0, 0};
        f.actor = 1;
        engine.apply(st, f);

        // 插旗不换回合，所以仍由同一个人尝试翻开自己插旗的格子
        ms::Action r;
        r.type  = ms::ActionType::Reveal;
        r.pos   = {0, 0};
        r.actor = st.currentPlayer();
        check(engine.apply(st, r) == ms::TurnResult::Invalid, "插旗格不能被翻开");
    }

    // ---------------------------------------------------------------- 8 生命归零
    section("8. 生命归零立即结束");
    {
        ms::RuleEngine engine;
        ms::GameConfig cfg = makeCfg();
        cfg.startLives = 1;
        ms::GameState st;
        st.reset(cfg, 12345);

        ms::Pos mine{-1, -1};
        for (int y = 0; y < 8 && mine.x < 0; ++y)
            for (int x = 0; x < 8 && mine.x < 0; ++x)
                if (st.board().hasMine({x, y}))
                    mine = {x, y};

        ms::Action a;
        a.type  = ms::ActionType::Reveal;
        a.pos   = mine;
        a.actor = 1;
        const auto r = engine.apply(st, a);

        check(r == ms::TurnResult::GameOver, "踩雷后返回 GameOver");
        check(st.isOver(), "状态标记为结束");
        check(st.winner() == ms::Winner::Player2, "玩家 1 生命归零，判玩家 2 胜");
    }

    // ---------------------------------------------------------------- 9 干扰
    section("9. 干扰机制");
    {
        ms::RuleEngine engine;
        ms::GameState st;
        st.reset(makeCfg(), 12345);

        check(engine.canInterfere(st), "初始可以干扰");
        check(st.player(1).interfereLeft == 1, "干扰次数 = 1");

        ms::Action a;
        a.type  = ms::ActionType::Interfere;
        a.actor = 1;
        const auto r = engine.apply(st, a);
        check(r == ms::TurnResult::TurnEnded, "干扰后回合结束");
        check(st.player(1).interfereLeft == 0, "干扰次数用完");
        check(st.currentPlayer() == 2, "轮到对手");
        // 对手回合时间应该被扣 3 秒
        check(st.remainingSeconds() == 17, "对手回合时间被扣 3 秒（20-3=17）");
    }

    // ---------------------------------------------------------------- 10 预览
    section("10. AI 用的预览接口");
    {
        ms::RuleEngine engine;
        ms::GameState st;
        st.reset(makeCfg(), 12345);

        ms::Pos mine{-1, -1}, safe{-1, -1};
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                if (st.board().hasMine({x, y}) && mine.x < 0) mine = {x, y};
                if (!st.board().hasMine({x, y}) && safe.x < 0) safe = {x, y};
            }
        }

        const auto pMine = engine.preview(st, mine);
        check(pMine.valid && pMine.isMine, "预览雷格：valid 且 isMine");

        const auto pSafe = engine.preview(st, safe);
        check(pSafe.valid && !pSafe.isMine, "预览安全格：valid 且非雷");

        auto cells = engine.revealableCells(st);
        check(cells.size() == 64, "初始全部 64 格可翻开");
    }

    // ---------------------------------------------------------------- 汇总
    std::printf("\n=== %s  通过 %d / 失败 %d ===\n",
                g_fail == 0 ? "ALL PASSED" : "SOME FAILED", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
