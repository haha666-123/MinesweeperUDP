#include "ClientSession.h"

#include <cstdio>

namespace ms {
namespace net {

// ===========================================================================
//  连接
// ===========================================================================
bool ClientSession::connect(const std::string& host, std::uint16_t port, std::string* error)
{
    m_lastError.clear();

    if (!UdpSocket::resolve(host, port, m_server)) {
        m_lastError = "无法解析主机: " + host;
        if (error) *error = m_lastError;
        return false;
    }

    if (!m_sock.open(0, error)) {      // 0 = 系统分配本地端口
        m_lastError = m_sock.lastError();
        return false;
    }

    m_state    = State::Connecting;
    m_outSeq   = 0;
    m_pending.clear();
    m_stats    = Stats{};

    std::printf("[client] connecting to %s (local port %u)\n",
                m_server.toString().c_str(),
                static_cast<unsigned>(m_sock.localPort()));
    return true;
}

void ClientSession::disconnect()
{
    if (m_state == State::InGame || m_state == State::Connecting) {
        Packet bye;
        bye.header.type = MsgType::Bye;
        bye.header.seq  = ++m_outSeq;
        const auto bin  = bye.encode();
        m_sock.sendTo(m_server, bin.data(), bin.size());
    }
    m_sock.close();
    m_state = State::Idle;
    m_pending.clear();
}

// ===========================================================================
//  轮询
// ===========================================================================
void ClientSession::poll(std::uint32_t nowMs)
{
    if (m_state == State::Idle)
        return;

    // ---- 收包
    Endpoint from;
    std::vector<std::uint8_t> buf;

    for (int i = 0; i < 64; ++i) {
        const int n = m_sock.recvFrom(from, buf, 2048);
        if (n <= 0)
            break;
        ++m_stats.packetsIn;
        handlePacket(buf.data(), buf.size(), nowMs);
    }

    // ---- 连接阶段：定时重发 Hello
    if (m_state == State::Connecting && (nowMs - m_lastHelloMs) >= kHelloIntervalMs) {
        sendHello(nowMs);
    }

    // ---- 游戏阶段：心跳 + 未确认消息重发
    if (m_state == State::InGame) {
        if ((nowMs - m_lastPingMs) >= kPingIntervalMs) {
            sendPing(nowMs);
        }
        resendPending(nowMs);
    }
}

// ===========================================================================
//  包处理
// ===========================================================================
void ClientSession::handlePacket(const std::uint8_t* data, std::size_t len, std::uint32_t nowMs)
{
    Packet p;
    if (!decode(data, len, p)) {
        ++m_stats.badPackets;
        return;
    }

    // 过滤非主机来源（简单防伪造）
    // 注意：这里不严格比对，因为 NAT 环境下主机看到的源地址可能不同；
    // 真实产品应加入 token 校验。本项目在 Host 侧已经做了严格校验。

    switch (p.header.type) {
    case MsgType::Welcome: onWelcome(p); break;
    case MsgType::Snapshot: onSnapshot(p, nowMs); break;

    case MsgType::Ack: {
        // 从待确认队列移除
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->seq == p.header.ack)
                it = m_pending.erase(it);
            else
                ++it;
        }
        break;
    }

    case MsgType::Rejected:
        ++m_stats.rejects;
        // 被拒绝的请求也要从队列移除，否则会一直重发
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->seq == p.header.ack)
                it = m_pending.erase(it);
            else
                ++it;
        }
        break;

    case MsgType::Pong:
        if (m_pingSentMs != 0)
            m_rttMs = nowMs - m_pingSentMs;
        break;

    case MsgType::Bye:
        std::printf("[client] host closed the session\n");
        m_state = State::Finished;
        break;

    default:
        break;
    }
}

void ClientSession::onWelcome(const Packet& p)
{
    Reader r(p.payload.data(), p.payload.size());
    const std::uint32_t seed   = r.u32();
    const std::uint16_t w      = r.u16();
    const std::uint16_t h      = r.u16();
    const std::uint8_t  ratio  = r.u8();
    const std::uint8_t  lives  = r.u8();
    const std::uint16_t turnS  = r.u16();
    const std::uint8_t  canInt = r.u8();
    const std::uint8_t  myId   = r.u8();

    if (!r.ok() || w == 0 || h == 0) {
        ++m_stats.badPackets;
        return;
    }

    GameConfig cfg;
    cfg.mapW           = w;
    cfg.mapH           = h;
    cfg.mineRatio      = (ratio > 0 ? ratio : 20) / 100.0;
    cfg.startLives     = (lives > 0 ? lives : 3);
    cfg.turnSeconds    = turnS;
    cfg.allowInterfere = (canInt != 0);
    cfg.seed           = seed;

    // 用主机给的 seed 重建同一张雷图
    m_game.reset(cfg, seed);
    m_seed = seed;
    m_myId = (myId == 1 ? 1 : 2);

    m_state = State::InGame;

    std::printf("[client] welcome ok: board=%dx%d cells=%d\n",
                m_game.board().width(), m_game.board().height(),
                static_cast<int>(m_game.cells().size()));
    std::printf("[client] welcome: seed=%u map=%dx%d lives=%u myId=%d\n",
                static_cast<unsigned>(seed), w, h,
                static_cast<unsigned>(lives), m_myId);
}

void ClientSession::onSnapshot(const Packet& p, std::uint32_t nowMs)
{
    (void)nowMs;

    // 分片重组
    std::vector<std::uint8_t> full;
    if (!m_asm.feed(p, full))
        return;                        // 尚未收齐

    // 用快照覆盖本地状态
    if (!m_game.deserialize(full.data(), full.size())) {
        ++m_stats.badPackets;
        std::printf("[client] deserialize FAILED len=%zu ratio=%.3f\n",
                    full.size(), m_game.config().mineRatio);
        return;
    }

    // 丢弃乱序的旧快照
    if (p.header.seq > m_lastSnapSeq) {
        m_lastSnapSeq = p.header.seq;
        m_stats.lastSnapSeq = m_lastSnapSeq;
        ++m_stats.snapsReceived;
    }

    if (m_game.isOver())
        m_state = State::Finished;

    // 收到快照说明链路正常
    if (m_state == State::Connecting)
        m_state = State::InGame;
}

// ===========================================================================
//  发送
// ===========================================================================
void ClientSession::sendHello(std::uint32_t nowMs)
{
    Packet p;
    p.header.type  = MsgType::Hello;
    p.header.seq   = ++m_outSeq;
    p.header.flags = FlagAckRequired;

    Writer w;
    w.u8(static_cast<std::uint8_t>(kVersion));
    w.u16(0);                          // 预留：昵称长度
    p.payload = w.buf;

    const auto bin = p.encode();
    m_sock.sendTo(m_server, bin.data(), bin.size());
    ++m_stats.packetsOut;
    m_lastHelloMs = nowMs;
}

void ClientSession::sendPing(std::uint32_t nowMs)
{
    Packet p;
    p.header.type = MsgType::Ping;
    p.header.seq  = ++m_outSeq;
    const auto bin = p.encode();
    m_sock.sendTo(m_server, bin.data(), bin.size());
    ++m_stats.packetsOut;

    m_lastPingMs = nowMs;
    m_pingSentMs = nowMs;
}

bool ClientSession::submitAction(const Action& act)
{
    if (m_state != State::InGame)
        return false;

    // 本地不做规则校验——但做一次「显然不可能」的过滤，省网络往返
    if (act.type != ActionType::Interfere) {
        if (!m_game.board().inBounds(act.pos))
            return false;
    }

    Writer w;
    w.u8(static_cast<std::uint8_t>(act.type));
    w.u16(static_cast<std::uint16_t>(act.pos.x));
    w.u16(static_cast<std::uint16_t>(act.pos.y));
    w.u8(static_cast<std::uint8_t>(act.interfereKind));

    Packet p;
    p.header.type  = MsgType::Action;
    p.header.seq   = ++m_outSeq;
    p.header.flags = FlagAckRequired;
    p.payload      = w.buf;

    const auto bin = p.encode();
    if (m_sock.sendTo(m_server, bin.data(), bin.size()) <= 0)
        return false;

    ++m_stats.packetsOut;

    Pending pd;
    pd.seq     = p.header.seq;
    pd.payload = bin;
    pd.sentMs  = 0;                    // 由第一次 resendPending 填
    pd.tries   = 1;
    m_pending.push_back(pd);

    return true;
}

void ClientSession::resendPending(std::uint32_t nowMs)
{
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->sentMs == 0) {
            it->sentMs = nowMs;        // 记录首次发送时间
            ++it;
            continue;
        }

        if ((nowMs - it->sentMs) < kResendAfterMs) {
            ++it;
            continue;
        }

        if (it->tries >= kMaxResend) {
            // 重发太多次，放弃
            it = m_pending.erase(it);
            continue;
        }

        m_sock.sendTo(m_server, it->payload.data(), it->payload.size());
        ++m_stats.packetsOut;
        ++m_stats.resends;
        ++it->tries;
        it->sentMs = nowMs;
        ++it;
    }
}

} // namespace net
} // namespace ms
