// ============================================================================
//  ai 层单元测试
//  ---------------------------------------------------------------------------
//  全部用假 HTTP 传输，不联网、不起子进程，跑完只要几十毫秒。
//  覆盖：随机 AI、约束推理、局面描述、JSON 解析、非法坐标降级、HTTP 失败降级。
// ============================================================================
#include "ai/LlmAI.h"
#include "ai/ProbabilityAI.h"
#include "ai/RandomAI.h"
#include "core/RuleEngine.h"

#include <cstdio>
#include <random>
#include <string>

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* what)
{
    if (cond) { ++g_pass; std::printf("  [PASS] %s\n", what); }
    else      { ++g_fail; std::printf("  [FAIL] %s\n", what); }
}

void section(const char* s)
{
    std::printf("\n[%s]\n", s);
    std::fflush(stdout);
}

ms::GameConfig cfg7()
{
    ms::GameConfig c;
    c.mapW = 7; c.mapH = 7;
    c.mineRatio = 0.2;
    c.startLives = 3;
    c.turnSeconds = 30;
    c.seed = 4242;
    return c;
}

// 翻开若干安全格，制造「有数字」的局面
void revealSome(ms::GameState& st, int steps)
{
    ms::RuleEngine engine;
    for (int i = 0; i < steps && !st.isOver(); ++i) {
        ms::Pos t{ -1, -1 };
        for (int y = 0; y < st.board().height() && t.x < 0; ++y)
            for (int x = 0; x < st.board().width() && t.x < 0; ++x) {
                const ms::Pos p{ x, y };
                const ms::Cell& c = st.cellAt(p);
                if (c.isHidden() && !c.hasFlag() && !st.board().hasMine(p))
                    t = p;
            }
        if (t.x < 0) break;

        ms::Action a;
        a.type = ms::ActionType::Reveal;
        a.pos = t;
        a.actor = st.currentPlayer();
        if (engine.apply(st, a) == ms::TurnResult::Invalid) break;
    }
}

} // namespace

int main()
{
    std::printf("=== ai layer tests ===\n");
    using namespace ms;

    std::mt19937 rng(12345);

    // ---------------------------------------------------------------- 1
    section("1. RandomAI 只选合法格");
    {
        GameState st;
        st.reset(cfg7(), 4242);

        RandomAI ai;
        bool allLegal = true;
        for (int i = 0; i < 50; ++i) {
            const auto d = ai.decide(st, rng);
            if (!d.valid) { allLegal = false; break; }
            const Cell& c = st.cellAt(d.pos);
            if (!st.board().inBounds(d.pos) || !c.isHidden() || c.hasFlag()) {
                allLegal = false;
                break;
            }
        }
        check(allLegal, "50 次随机决策全部合法");
        check(std::string(ai.name()) == "Random", "name() = Random");
    }

    // ---------------------------------------------------------------- 2
    section("2. ProbabilityAI 基本行为");
    {
        GameState st;
        st.reset(cfg7(), 4242);
        revealSome(st, 6);

        const auto an = ProbabilityAI::analyze(st);
        check(an.certainMines >= 0 && an.certainSafe >= 0, "analyze 统计非负");
        std::printf("         必雷=%d 必安全=%d 待猜=%d\n",
                    an.certainMines, an.certainSafe, an.guessed);

        ProbabilityAI ai;
        const auto d = ai.decide(st, rng);
        check(d.valid, "给出决策");
        check(!d.reason.empty(), "带理由");
        std::printf("         理由: %s\n", d.reason.c_str());
    }

    // ---------------------------------------------------------------- 3
    section("3. 约束推理的正确性（关键）");
    {
        // 用较少 seed 与较少采样，避免测试耗时过长；
        // 关键在于「凡判定为必安全/必是雷，都必须正确」
        int checked = 0;
        int wrongSafe = 0;
        int wrongMine = 0;

        for (std::uint32_t seed = 1; seed <= 5; ++seed) {
            GameState st;
            st.reset(cfg7(), seed);
            revealSome(st, 5);

            const auto an = ProbabilityAI::analyze(st);
            if (an.certainSafe == 0 && an.certainMines == 0)
                continue;
            ++checked;

            ProbabilityAI ai;
            for (int i = 0; i < 5; ++i) {
                const auto d = ai.decide(st, rng);
                if (!d.valid) break;
                if (d.reason.find("必安全") != std::string::npos) {
                    if (st.board().hasMine(d.pos)) ++wrongSafe;
                    break;
                }
                if (d.reason.find("必是雷") != std::string::npos) {
                    if (!st.board().hasMine(d.pos)) ++wrongMine;
                    break;
                }
            }
        }

        std::printf("         检验了 %d 个局面\n", checked);
        check(wrongSafe == 0, "「必安全」判定全部正确");
        check(wrongMine == 0, "「必是雷」判定全部正确");
    }

    // ---------------------------------------------------------------- 4
    section("4. LlmAI::describeBoard");
    {
        GameState st;
        st.reset(cfg7(), 4242);
        const std::string desc = LlmAI::describeBoard(st);

        check(!desc.empty(), "非空描述");
        check(desc.find("棋盘 7 x 7") != std::string::npos, "含棋盘尺寸");
        check(desc.find("可点击的未知格子") != std::string::npos, "含未知格清单");
        check(desc.find("(0,0)") != std::string::npos, "含 (x,y) 坐标格式");
        check(desc.find("(无，这是第一步)") != std::string::npos, "开局标注无已翻开格");

        revealSome(st, 4);
        const std::string desc2 = LlmAI::describeBoard(st);
        check(desc2.find(":") != std::string::npos, "翻开后含数字信息");
    }

    // ---------------------------------------------------------------- 5
    section("5. LlmAI::buildRequestBody");
    {
        LlmAI::Config c;
        c.model = "deepseek-chat";
        c.temperature = 0.2;
        const std::string req = LlmAI::buildRequestBody(c, "测试局面\n");

        check(req.find("\"model\":\"deepseek-chat\"") != std::string::npos, "含 model 字段");
        check(req.find("\"temperature\":0.20") != std::string::npos, "含 temperature");
        check(req.find("\"messages\"") != std::string::npos, "含 messages 数组");
        check(req.find("测试局面") != std::string::npos, "含局面文本");
    }

    // ---------------------------------------------------------------- 6
    section("6. LlmAI::parseModelReply");
    {
        int x = -1, y = -1;
        std::string reason;

        check(LlmAI::parseModelReply("{\"x\": 3, \"y\": 5, \"reason\": \"安全\"}",
                                     x, y, reason), "纯 JSON");
        check(x == 3 && y == 5, "坐标 3,5");
        check(reason == "安全", "理由正确");

        const std::string wrapped =
            "{\"choices\":[{\"message\":{\"content\":\"{\\\"x\\\": 1, \\\"y\\\": 2, "
            "\\\"reason\\\": \\\"测试\\\"}\"}}]}";
        x = y = -1; reason.clear();
        const bool ok2 = LlmAI::parseModelReply(wrapped, x, y, reason);
        check(ok2, "嵌套 content 解析");
        if (ok2) {
            check(x == 1 && y == 2, "嵌套坐标正确");
            check(reason == "测试", "嵌套理由正确");
        }

        const std::string fenced = "```json\n{\"x\": 4, \"y\": 6, \"reason\": \"ok\"}\n```";
        x = y = -1;
        check(LlmAI::parseModelReply(fenced, x, y, reason), "markdown 代码块");
        check(x == 4 && y == 6, "代码块坐标正确");

        check(!LlmAI::parseModelReply("not json", x, y, reason), "非 JSON 返回 false");
        check(!LlmAI::parseModelReply("", x, y, reason), "空串返回 false");
        check(!LlmAI::parseModelReply("{\"x\": -1, \"y\": 3}", x, y, reason),
              "负坐标返回 false");
    }

    // ---------------------------------------------------------------- 7
    section("7. 未配置 Key 直接降级");
    {
        GameState st;
        st.reset(cfg7(), 4242);

        LlmAI ai;
        LlmAI::Config c;
        c.apiKey.clear();
        ai.configure(c);

        check(!ai.available(), "available() = false");
        const auto d = ai.decide(st, rng);
        check(d.valid, "仍给出决策");
        check(d.reason.find("降级") != std::string::npos, "标注降级");
        check(ai.stats().fallbacks == 1, "降级计数 = 1");
        check(ai.stats().calls == 0, "未发起 HTTP 调用");
    }

    // ---------------------------------------------------------------- 8
    section("8. 假传输：正常返回");
    {
        GameState st;
        st.reset(cfg7(), 4242);

        LlmAI ai;
        LlmAI::Config c;
        c.apiKey = "test-key";
        ai.configure(c);

        // 找一个合法格作为模型「返回」的坐标
        Pos target{ -1, -1 };
        for (int y = 0; y < 7 && target.x < 0; ++y)
            for (int x = 0; x < 7 && target.x < 0; ++x)
                if (st.cellAt({ x, y }).isHidden())
                    target = { x, y };

        ai.setHttpTransport([&](const std::string&, const std::string&,
                                const std::string&, std::string& out) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "{\"choices\":[{\"message\":{\"content\":\"{\\\"x\\\": %d, "
                          "\\\"y\\\": %d, \\\"reason\\\": \\\"模型选择\\\"}\"}}]}",
                          target.x, target.y);
            out = buf;
            return true;
        });

        const auto d = ai.decide(st, rng);
        check(d.valid, "决策有效");
        check(d.pos == target, "采用模型给的坐标");
        check(d.reason.find("LLM") != std::string::npos, "理由标注来自 LLM");
        check(ai.stats().ok == 1, "成功计数 = 1");
        check(ai.stats().fallbacks == 0, "未降级");
    }

    // ---------------------------------------------------------------- 9
    section("9. 假传输：返回非法坐标 -> 降级");
    {
        GameState st;
        st.reset(cfg7(), 4242);

        LlmAI ai;
        LlmAI::Config c;
        c.apiKey = "test-key";
        ai.configure(c);

        // 故意返回越界坐标
        ai.setHttpTransport([](const std::string&, const std::string&,
                               const std::string&, std::string& out) {
            out = "{\"choices\":[{\"message\":{\"content\":"
                  "\"{\\\"x\\\": 999, \\\"y\\\": 999, \\\"reason\\\": \\\"越界\\\"}\"}}]}";
            return true;
        });

        const auto d = ai.decide(st, rng);
        check(d.valid, "仍给出决策（降级）");
        check(st.board().inBounds(d.pos), "降级后坐标在棋盘内");
        check(d.reason.find("降级") != std::string::npos, "标注降级");
        check(ai.stats().invalidMove == 1, "记录非法坐标计数");
    }

    // ---------------------------------------------------------------- 10
    section("10. 假传输：HTTP 失败 -> 降级");
    {
        GameState st;
        st.reset(cfg7(), 4242);

        LlmAI ai;
        LlmAI::Config c;
        c.apiKey = "test-key";
        ai.configure(c);

        ai.setHttpTransport([](const std::string&, const std::string&,
                               const std::string&, std::string&) {
            return false;                   // 传输失败
        });
        check(ai.hasCustomTransport(), "已注入假传输（避免真实网络）");

        const auto d = ai.decide(st, rng);
        check(d.valid, "降级后有决策");
        check(ai.stats().failed == 1, "失败计数 = 1");
        check(ai.stats().fallbacks == 1, "降级计数 = 1");
    }

    // ---------------------------------------------------------------- 11
    section("11. 假传输：回复无法解析 -> 降级");
    {
        GameState st;
        st.reset(cfg7(), 4242);

        LlmAI ai;
        LlmAI::Config c;
        c.apiKey = "test-key";
        ai.configure(c);

        ai.setHttpTransport([](const std::string&, const std::string&,
                               const std::string&, std::string& out) {
            out = "抱歉，我无法完成这个任务。";
            return true;
        });

        const auto d = ai.decide(st, rng);
        check(d.valid, "降级后有决策");
        check(d.reason.find("降级") != std::string::npos, "标注降级");
    }

    std::printf("\n=== %s  通过 %d / 失败 %d ===\n",
                g_fail == 0 ? "ALL PASSED" : "SOME FAILED", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
