#pragma once
// ============================================================================
//  ClientSession：客户端（非权威端）
//  ---------------------------------------------------------------------------
//  职责：
//    1. 发 Hello，等 Welcome（拿到 seed 与配置，重建同一张雷图）
//    2. 用户操作 -> 发 Action（带序列号，未收到 ACK 则重发）
//    3. 收 Snapshot -> 覆盖本地 GameState（客户端不自己算结果）
//
//  关键设计：客户端**永远不用 RuleEngine 修改状态**。
//  所有「点了一格之后会怎样」都以主机快照为准。
//  这样即使客户端被篡改，也无法影响对局（防作弊）。
//
//  重发策略：发出 Action 后记录 (seq, 包体, 发送时刻)；
//  每 200ms 检查一次，超过 300ms 未 ACK 就重发，最多重发 5 次。
// ============================================================================
#include "../core/GameState.h"
#include "Protocol.h"
#include "UdpSocket.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ms {
namespace net {

class ClientSession {
public:
    enum class State { Idle, Connecting, InGame, Finished };

    ClientSession() = default;

    //! 连接主机（host 可为 IP 或域名）
    bool connect(const std::string& host, std::uint16_t port, std::string* error);
    void disconnect();

    //! 每帧调用；nowMs 为单调递增毫秒
    void poll(std::uint32_t nowMs);

    //! 提交一次操作（会被排队重发直到 ACK）
    bool submitAction(const Action& act);

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] const GameState& game() const { return m_game; }
    [[nodiscard]] std::uint32_t seed() const { return m_seed; }
    [[nodiscard]] bool connected() const { return m_state == State::InGame; }
    [[nodiscard]] int myPlayerId() const { return m_myId; }

    //! 往返延迟（毫秒），未测出返回 0
    [[nodiscard]] std::uint32_t rttMs() const { return m_rttMs; }

    struct Stats {
        std::uint64_t packetsIn     = 0;
        std::uint64_t packetsOut    = 0;
        std::uint64_t badPackets    = 0;
        std::uint64_t snapsReceived = 0;
        std::uint64_t resends       = 0;
        std::uint64_t rejects       = 0;
        std::uint32_t lastSnapSeq   = 0;
    };
    [[nodiscard]] const Stats& stats() const { return m_stats; }
    [[nodiscard]] const std::string& lastError() const { return m_lastError; }

private:
    struct Pending {
        std::uint16_t             seq = 0;
        std::vector<std::uint8_t> payload;
        std::uint32_t             sentMs = 0;
        int                       tries  = 0;
    };

    void handlePacket(const std::uint8_t* data, std::size_t len, std::uint32_t nowMs);
    void onWelcome(const Packet& p);
    void onSnapshot(const Packet& p, std::uint32_t nowMs);
    void sendHello(std::uint32_t nowMs);
    void sendPing(std::uint32_t nowMs);
    void resendPending(std::uint32_t nowMs);

    UdpSocket    m_sock;
    GameState    m_game;
    Endpoint     m_server;

    State        m_state = State::Idle;
    std::uint32_t m_seed = 0;
    int          m_myId  = 2;

    std::uint16_t m_outSeq = 0;
    std::uint32_t m_lastSnapSeq = 0;

    std::uint32_t m_lastHelloMs = 0;
    std::uint32_t m_lastPingMs  = 0;
    std::uint32_t m_pingSentMs  = 0;
    std::uint32_t m_rttMs       = 0;

    std::vector<Pending> m_pending;
    FragmentAssembler    m_asm;

    Stats       m_stats;
    std::string m_lastError;

    static constexpr std::uint32_t kHelloIntervalMs = 500;
    static constexpr std::uint32_t kPingIntervalMs  = 1000;
    static constexpr std::uint32_t kResendAfterMs   = 300;
    static constexpr int           kMaxResend       = 5;
};

} // namespace net
} // namespace ms
