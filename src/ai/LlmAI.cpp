#include "LlmAI.h"

#include "ProbabilityAI.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ms {

namespace {

// ---------------------------------------------------------------------------
//  极简 JSON 取值（只覆盖本项目需要的形态，不引入第三方库）
// ---------------------------------------------------------------------------
std::string findString(const std::string& s, const char* key)
{
    const std::string pat = std::string("\"") + key + "\"";
    std::size_t p = s.find(pat);
    if (p == std::string::npos)
        return {};
    p = s.find(':', p);
    if (p == std::string::npos)
        return {};
    ++p;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n'))
        ++p;
    if (p >= s.size() || s[p] != '"')
        return {};
    ++p;
    std::string out;
    while (p < s.size() && s[p] != '"') {
        if (s[p] == '\\' && p + 1 < s.size()) {
            ++p;
            switch (s[p]) {
            case 'n':  out += '\n'; break;
            case 't':  out += '\t'; break;
            case 'r':  out += '\r'; break;
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            default:   out += s[p];
            }
        } else {
            out += s[p];
        }
        ++p;
    }
    return out;
}

int findInt(const std::string& s, const char* key)
{
    const std::string pat = std::string("\"") + key + "\"";
    std::size_t p = s.find(pat);
    if (p == std::string::npos)
        return -1;
    p = s.find(':', p);
    if (p == std::string::npos)
        return -1;
    ++p;
    while (p < s.size() && !(s[p] == '-' || (s[p] >= '0' && s[p] <= '9')))
        ++p;
    if (p >= s.size())
        return -1;
    return std::atoi(s.c_str() + p);
}

std::string escapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 32);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
                out += ' ';
            else
                out += c;
        }
    }
    return out;
}

} // namespace

LlmAI::LlmAI() = default;

void LlmAI::configure(const Config& cfg)
{
    std::scoped_lock lock(m_mutex);
    m_cfg = cfg;
}

LlmAI::Stats LlmAI::stats() const
{
    std::scoped_lock lock(m_mutex);
    return m_stats;
}

std::string LlmAI::lastReason() const
{
    std::scoped_lock lock(m_mutex);
    return m_lastReason;
}

// ===========================================================================
//  局面描述（纯函数）
// ===========================================================================
std::string LlmAI::describeBoard(const GameState& st)
{
    const Board& b = st.board();
    std::string out;
    char buf[160];

    std::snprintf(buf, sizeof(buf), "棋盘 %d x %d，共 %d 个雷。\n",
                  b.width(), b.height(), b.mineCount());
    out += buf;

    out += "已翻开的格子（坐标:周围雷数）：\n";
    int revealed = 0;
    for (int y = 0; y < b.height(); ++y) {
        for (int x = 0; x < b.width(); ++x) {
            const Pos p{ x, y };
            const Cell& c = st.cellAt(p);
            if (c.state == CellState::Revealed) {
                std::snprintf(buf, sizeof(buf), "  (%d,%d):%d\n", x, y, b.adjacentMines(p));
                out += buf;
                ++revealed;
            }
        }
    }
    if (revealed == 0)
        out += "  (无，这是第一步)\n";

    out += "可点击的未知格子（坐标）：\n  ";
    int unknown = 0;
    for (int y = 0; y < b.height(); ++y) {
        for (int x = 0; x < b.width(); ++x) {
            const Pos p{ x, y };
            const Cell& c = st.cellAt(p);
            if (c.isHidden() && !c.hasFlag()) {
                std::snprintf(buf, sizeof(buf), "(%d,%d) ", x, y);
                out += buf;
                ++unknown;
            }
        }
    }
    out += "\n";

    std::snprintf(buf, sizeof(buf), "未知格子总数: %d\n", unknown);
    out += buf;
    return out;
}

// ===========================================================================
//  构造请求体（纯函数）
// ===========================================================================
std::string LlmAI::buildRequestBody(const Config& cfg, const std::string& boardText)
{
    std::string prompt;
    prompt += "你是扫雷游戏 AI。根据当前局面，选择最安全的一个格子翻开。\n\n";
    prompt += boardText;
    prompt += "\n规则：\n";
    prompt += "1. 已翻开的格子后面跟的数字，表示它周围 8 格中有几个雷\n";
    prompt += "2. 必须从「未知格子」里选\n";
    prompt += "3. 能通过周围数字推理出必然安全的格子，优先选它\n";
    prompt += "4. 只输出 JSON，不要任何其他文字，格式："
              "{\"x\": 数字, \"y\": 数字, \"reason\": \"不超过20字的理由\"}\n";

    char temp[32];
    std::snprintf(temp, sizeof(temp), "%.2f", cfg.temperature);

    std::string req;
    req += "{\"model\":\"" + escapeJson(cfg.model) + "\",";
    req += "\"temperature\":" + std::string(temp) + ",";
    req += "\"messages\":[{\"role\":\"user\",\"content\":\"" + escapeJson(prompt) + "\"}]}";
    return req;
}

// ===========================================================================
//  解析回复（纯函数）
// ===========================================================================
bool LlmAI::parseModelReply(const std::string& body, int& x, int& y, std::string& reason)
{
    x = -1;
    y = -1;
    reason.clear();

    // OpenAI 风格响应把内容放在 content 里；直接给 JSON 时没有这个字段
    std::string content = findString(body, "content");
    if (content.empty())
        content = body;

    // 去掉 markdown 代码块
    const std::size_t fence = content.find("```");
    if (fence != std::string::npos) {
        std::size_t start = content.find('\n', fence);
        if (start != std::string::npos) {
            const std::size_t end = content.find("```", start + 1);
            if (end != std::string::npos)
                content = content.substr(start + 1, end - start - 1);
        }
    }

    x = findInt(content, "x");
    y = findInt(content, "y");
    reason = findString(content, "reason");

    if (x < 0 || y < 0)
        return false;
    if (reason.empty())
        reason = "模型未给出理由";
    return true;
}

// ===========================================================================
//  curl 传输（默认实现）
// ===========================================================================
bool LlmAI::curlTransport(const std::string& url,
                          const std::string& apiKey,
                          const std::string& body,
                          std::string& responseOut)
{
    responseOut.clear();

    const std::string reqFile = "llm_req.json";
    const std::string resFile = "llm_res.json";

    if (FILE* f = std::fopen(reqFile.c_str(), "wb")) {
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
    } else {
        return false;
    }

    const std::string cmd =
        "curl -s -S -m 8 -X POST \"" + url + "\" "
        "-H \"Content-Type: application/json\" "
        "-H \"Authorization: Bearer " + apiKey + "\" "
        "--data-binary @" + reqFile + " -o " + resFile + " 2>nul";

    const int rc = std::system(cmd.c_str());
    std::remove(reqFile.c_str());

    if (rc != 0) {
        std::remove(resFile.c_str());
        return false;
    }

    if (FILE* f = std::fopen(resFile.c_str(), "rb")) {
        char buf[4096];
        std::size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
            responseOut.append(buf, n);
        std::fclose(f);
    }
    std::remove(resFile.c_str());

    return !responseOut.empty();
}

// ===========================================================================
//  降级
// ===========================================================================
AIDecision LlmAI::fallback(const GameState& st, std::mt19937& rng, const char* why)
{
    ProbabilityAI prob;
    AIDecision d = prob.decide(st, rng);

    d.reason = std::string("[降级:") + why + "] " + d.reason;

    std::scoped_lock lock(m_mutex);
    ++m_stats.fallbacks;
    m_lastReason = d.reason;
    return d;
}

// ===========================================================================
//  决策
// ===========================================================================
AIDecision LlmAI::decide(const GameState& st, std::mt19937& rng)
{
    if (!available())
        return fallback(st, rng, "未配置 API Key");

    const Config cfg = m_cfg;
    const std::string boardText = describeBoard(st);
    const std::string reqBody   = buildRequestBody(cfg, boardText);

    {
        std::scoped_lock lock(m_mutex);
        ++m_stats.calls;
    }

    const auto t0 = std::chrono::steady_clock::now();

    std::string resp;
    const bool transportOk = m_transport
        ? m_transport(cfg.endpoint, cfg.apiKey, reqBody, resp)
        : curlTransport(cfg.endpoint, cfg.apiKey, reqBody, resp);

    const auto ms = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());

    if (!transportOk) {
        {
            // 注意：锁必须限制在最小作用域内。
            // fallback() 内部也会拿 m_mutex，若在持锁状态下调用会导致同线程死锁。
            std::scoped_lock lock(m_mutex);
            ++m_stats.failed;
            m_stats.lastLatencyMs = ms;
        }
        return fallback(st, rng, "HTTP 失败");
    }

    int x = -1, y = -1;
    std::string reason;
    if (!parseModelReply(resp, x, y, reason)) {
        {
            std::scoped_lock lock(m_mutex);
            ++m_stats.failed;
            m_stats.lastLatencyMs = ms;
        }
        return fallback(st, rng, "解析失败");
    }

    // 坐标校验：越界 / 已翻开 / 有旗 都算非法
    const Pos p{ x, y };
    const bool legal = st.board().inBounds(p) &&
                       st.cellAt(p).isHidden() &&
                       !st.cellAt(p).hasFlag();

    {
        std::scoped_lock lock(m_mutex);
        m_stats.lastLatencyMs = ms;
        if (legal) {
            ++m_stats.ok;
        } else {
            ++m_stats.invalidMove;
            ++m_stats.failed;
        }
    }

    if (!legal)
        return fallback(st, rng, "非法坐标");

    {
        std::scoped_lock lock(m_mutex);
        m_lastReason = reason;
    }

    AIDecision d;
    d.valid  = true;
    d.type   = ActionType::Reveal;
    d.pos    = p;
    d.reason = "LLM: " + reason;
    return d;
}

} // namespace ms
