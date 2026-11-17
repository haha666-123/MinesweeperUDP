#pragma once
// ============================================================================
//  HostSession：主机（权威端）
//  ---------------------------------------------------------------------------
//  职责：
//    1. 维护唯一权威 GameState
//    2. 接收客户端 Hello -> 回 Welcome（带 seed + 配置，客户端据此生成同一张雷图）
//    3. 接收客户端 Action -> 用 RuleEngine 校验并应用 -> 广播 Snapshot
//    4. 客户端超时（长时间无消息）自动判离
//
//  可靠性策略：
//    * 客户端操作需要 ACK，丢了重发（客户端侧负责重发）
//    * 快照不可靠，但每 100ms 至少发一次；客户端以「最大 seq」为准丢弃乱序
//
//  为什么雷区不传输：双方用同一个 seed 各自生成，省掉 N*3 字节，
//  而且能保证完全一致（见 Board::generate 的可复现设计）。
// ============================================================================
#include "../core/GameState.h"
#include "../core/RuleEngine.h"
#include "Protocol.h"
#include "UdpSocket.h"

#include <cstdint>
#include <string>

namespace ms {
namespace net {

class HostSession {
public:
    enum class State { Idle, WaitingClient, InGame, Finished };

    HostSession() = default;

    //! 开始监听；返回实际端口
    bool start(std::uint16_t port, const GameConfig& cfg, std::string* error);
    void stop();

    //! 每帧调用：处理所有到达的包、推进计时
    void poll(std::uint32_t nowMs);

    //! 主机自己也参与游戏，本地操作直接走这个入口（不经网络）
    TurnResult submitLocalAction(const Action& act);

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] GameState& game() { return m_game; }
    [[nodiscard]] const GameState& game() const { return m_game; }
    [[nodiscard]] std::uint16_t localPort() const { return m_sock.localPort(); }
    [[nodiscard]] bool hasClient() const { return m_clientValid; }
    [[nodiscard]] int clientId() const { return 2; }   // 客户端固定是玩家 2

    //! 统计：便于界面显示与排错
    struct Stats {
        std::uint64_t packetsIn      = 0;
        std::uint64_t packetsOut     = 0;
        std::uint64_t badPackets     = 0;
        std::uint64_t actionsApplied = 0;
        std::uint64_t actionsRejected = 0;
        std::uint64_t snapsSent      = 0;
        std::uint32_t lastClientSeq  = 0;
    };
    [[nodiscard]] const Stats& stats() const { return m_stats; }

    [[nodiscard]] const std::string& lastError() const { return m_lastError; }
    [[nodiscard]] std::uint32_t elapsedSinceClientMs(std::uint32_t nowMs) const;

private:
    void handlePacket(const std::uint8_t* data, std::size_t len,
                      const Endpoint& from, std::uint32_t nowMs);
    void onHello(const Packet& p, const Endpoint& from, std::uint32_t nowMs);
    void onAction(const Packet& p, const Endpoint& from, std::uint32_t nowMs);
    void sendWelcome(const Endpoint& to);
    void sendRejected(const Endpoint& to, TurnResult reason, std::uint16_t ackSeq);
    void broadcastSnapshot(std::uint32_t nowMs, bool force = false);
    void sendAck(const Endpoint& to, std::uint16_t seq);

    UdpSocket    m_sock;
    RuleEngine   m_engine;
    GameState    m_game;

    State        m_state = State::Idle;
    Endpoint     m_client;
    bool         m_clientValid = false;
    std::uint32_t m_lastClientMs = 0;
    std::uint32_t m_lastSnapMs   = 0;

    std::uint16_t m_outSeq = 0;        // 我方发送序列号
    std::uint32_t m_fragId = 0;        // 分片组 ID
    std::uint16_t m_lastClientSeq = 0; // 已处理的最大客户端序列号

    Stats       m_stats;
    std::string m_lastError;

    static constexpr std::uint32_t kSnapIntervalMs = 100;   // 每秒 10 帧快照
    static constexpr std::uint32_t kClientTimeoutMs = 5000;
};

} // namespace net
} // namespace ms
