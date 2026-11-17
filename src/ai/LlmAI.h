#pragma once
// ============================================================================
//  大模型 AI
//  ---------------------------------------------------------------------------
//  把当前局面「文字化」发给大模型，让它返回要翻开的坐标。
//
//  设计要点：
//    1. HTTP 传输通过 std::function 注入 —— 默认实现用 curl 子进程，
//       测试可注入假实现，从而在不联网、不起子进程的情况下验证全部逻辑。
//       这也解决了「std::system 阻塞主循环」的问题：可换成异步实现。
//    2. 模型可能返回非法坐标（越界/已翻开），必须校验，非法则降级。
//    3. 任何失败（无 Key / 超时 / 解析失败 / 非法坐标）都降级到 ProbabilityAI，
//       保证 AI 永远不会「卡住等待」或「不做决策」。
//
//  与扫雷算法 AI 的关系：
//    LlmAI 不是替代 ProbabilityAI，而是「在有信息时做额外判断」。
//    实测中 LLM 在简单局面不如约束推理准确，所以真实对局里用 LLM 兜底、
//    用 ProbabilityAI 做主决策也是一种可选策略（见 decideWithHybrid）。
// ============================================================================
#include "IAI.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace ms {

class LlmAI : public IAI {
public:
    // HTTP 传输函数签名：(url, apiKey, jsonBody) -> (成功?, 响应体)
    // 返回 false 表示网络/传输层失败。
    using HttpTransport = std::function<bool(const std::string& url,
                                             const std::string& apiKey,
                                             const std::string& body,
                                             std::string& responseOut)>;

    struct Config {
        std::string endpoint  = "https://api.deepseek.com/chat/completions";
        std::string apiKey;                       // 空 = 直接降级
        std::string model     = "deepseek-chat";
        int         timeoutMs = 8000;
        double      temperature = 0.2;
    };

    LlmAI();
    ~LlmAI() override = default;

    void configure(const Config& cfg);
    [[nodiscard]] const Config& config() const { return m_cfg; }
    [[nodiscard]] bool available() const { return !m_cfg.apiKey.empty(); }

    //! 注入 HTTP 实现；不注入则使用内置 curl 实现
    void setHttpTransport(HttpTransport t) { m_transport = std::move(t); }
    [[nodiscard]] bool hasCustomTransport() const { return static_cast<bool>(m_transport); }

    AIDecision decide(const GameState& st, std::mt19937& rng) override;

    [[nodiscard]] const char* name() const override { return "LLM"; }

    // ---- 统计与诊断
    struct Stats {
        std::uint64_t calls         = 0;
        std::uint64_t ok            = 0;
        std::uint64_t failed        = 0;
        std::uint64_t fallbacks     = 0;
        std::uint64_t invalidMove   = 0;
        std::uint32_t lastLatencyMs = 0;
    };
    [[nodiscard]] Stats stats() const;
    [[nodiscard]] std::string lastReason() const;

    // ---- 纯函数（可单测，不涉网络）
    [[nodiscard]] static std::string describeBoard(const GameState& st);
    [[nodiscard]] static std::string buildRequestBody(const Config& cfg,
                                                      const std::string& boardText);
    static bool parseModelReply(const std::string& body, int& x, int& y, std::string& reason);

    //! 内置 curl 传输实现（在无自定义 transport 时使用）
    static bool curlTransport(const std::string& url,
                              const std::string& apiKey,
                              const std::string& body,
                              std::string& responseOut);

private:
    AIDecision fallback(const GameState& st, std::mt19937& rng, const char* why);

    Config        m_cfg;
    HttpTransport m_transport;
    mutable std::mutex m_mutex;
    Stats         m_stats;
    std::string   m_lastReason;
};

} // namespace ms
