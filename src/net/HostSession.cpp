#include "HostSession.h"

#include <algorithm>
#include <cstdio>

namespace ms {
namespace net {

// ===========================================================================
//  生命周期
// ===========================================================================
bool HostSession::start(std::uint16_t port, const GameConfig& cfg, std::string* error)
{
    m_lastError.clear();

    if (!m_sock.open(port, error)) {
        m_lastError = m_sock.lastError();
        return false;
    }

    // Host 是玩家 1
    m_game.reset(cfg, 0);            // seed = 0 -> 随机生成
    m_engine = RuleEngine{};

    m_state        = State::WaitingClient;
    m_clientValid  = false;
    m_lastClientMs = 0;
    m_lastSnapMs   = 0;
    m_outSeq       = 0;
    m_lastClientSeq = 0;
    m_stats        = Stats{};

    std::printf("[host] listening on port %u, seed=%u, map=%dx%d\n",
                static_cast<unsigned>(m_sock.localPort()),
                static_cast<unsigned>(m_game.seed()),
                cfg.mapW, cfg.mapH);
    return true;
}

void HostSession::stop()
{
    if (m_clientValid) {
        // 礼貌地道别
        Packet bye;
        bye.header.type  = MsgType::Bye;
        bye.header.seq   = ++m_outSeq;
        const auto bin = bye.encode();
        m_sock.sendTo(m_client, bin.data(), bin.size());
    }
    m_sock.close();
    m_state       = State::Idle;
    m_clientValid = false;
}

std::uint32_t HostSession::elapsedSinceClientMs(std::uint32_t nowMs) const
{
    if (!m_clientValid)
        return 0;
    return nowMs - m_lastClientMs;
}

// ===========================================================================
//  每帧轮询
// ===========================================================================
void HostSession::poll(std::uint32_t nowMs)
{
    if (m_state == State::Idle)
        return;

    // ---- 收包（一次最多处理 64 个，避免单帧被刷爆）
    Endpoint from;
    std::vector<std::uint8_t> buf;

    for (int i = 0; i < 64; ++i) {
        const int n = m_sock.recvFrom(from, buf, 2048);
        if (n <= 0)
            break;                       // 0 = 没有数据；-1 = 出错

        ++m_stats.packetsIn;
        handlePacket(buf.data(), buf.size(), from, nowMs);
    }

    // ---- 客户端超时
    if (m_clientValid && m_state == State::InGame &&
        (nowMs - m_lastClientMs) > kClientTimeoutMs) {
        std::printf("[host] client timeout, dropping\n");
        m_clientValid = false;
        m_state       = State::Finished;
    }

    // ---- 定时广播快照
    if (m_state == State::InGame && m_clientValid &&
        (nowMs - m_lastSnapMs) >= kSnapIntervalMs) {
        broadcastSnapshot(nowMs);
    }
}

// ===========================================================================
//  包处理
// ===========================================================================
void HostSession::handlePacket(const std::uint8_t* data, std::size_t len,
                               const Endpoint& from, std::uint32_t nowMs)
{
    Packet p;
    if (!decode(data, len, p)) {
        ++m_stats.badPackets;
        return;
    }

    switch (p.header.type) {
    case MsgType::Hello:  onHello(p, from, nowMs); break;
    case MsgType::Action: onAction(p, from, nowMs); break;

    case MsgType::Ping: {
        Packet pong;
        pong.header.type = MsgType::Pong;
        pong.header.seq  = ++m_outSeq;
        pong.header.ack  = p.header.seq;
        const auto bin = pong.encode();
        m_sock.sendTo(from, bin.data(), bin.size());
        ++m_stats.packetsOut;
        break;
    }

    case MsgType::Bye:
        std::printf("[host] client said bye\n");
        m_clientValid = false;
        m_state       = State::Finished;
        break;

    case MsgType::Ack:
        // 主机发给客户端的可靠消息目前不需要重发，收到 ACK 仅记录
        break;

    default:
        ++m_stats.badPackets;
        break;
    }
}

void HostSession::onHello(const Packet& p, const Endpoint& from, std::uint32_t nowMs)
{
    (void)p;
    // 只接受一个客户端；重复 Hello 视为重连请求
    m_client      = from;
    m_clientValid = true;
    m_lastClientMs = nowMs;
    m_lastClientSeq = 0;

    std::printf("[host] client joined: %s\n", from.toString().c_str());

    // 关键：先进入 InGame，再发 Welcome。
    // Welcome 里带的是 m_game 当前的 seed / 配置，客户端据此重建雷图；
    // 后续快照也基于同一局。绝对不能在握手时 reset，
    // 否则 seed 会变，客户端与主机的雷图永久不一致。
    if (m_state != State::InGame)
        m_state = State::InGame;

    sendWelcome(from);
    broadcastSnapshot(nowMs, true);
}

void HostSession::onAction(const Packet& p, const Endpoint& from, std::uint32_t nowMs)
{
    // 必须来自已加入的客户端
    if (!m_clientValid || !(from == m_client)) {
        ++m_stats.actionsRejected;
        return;
    }
    m_lastClientMs = nowMs;

    if (m_state != State::InGame) {
        sendRejected(from, TurnResult::Invalid, p.header.seq);
        ++m_stats.actionsRejected;
        return;
    }

    if (p.payload.size() < 6) {
        ++m_stats.badPackets;
        return;
    }

    Reader r(p.payload.data(), p.payload.size());
    ActionMsg am;
    am.actType        = r.u8();
    am.posX           = r.u16();
    am.posY           = r.u16();
    am.interfereKind  = r.u8();
    if (!r.ok()) {
        ++m_stats.badPackets;
        return;
    }

    // 序列号去重：重复包直接 ACK 掉（说明上一次 ACK 丢了）
    if (p.header.seq <= m_lastClientSeq && m_lastClientSeq != 0) {
        sendAck(from, p.header.seq);
        return;
    }
    m_lastClientSeq = p.header.seq;
    m_stats.lastClientSeq = p.header.seq;

    // 构造操作并应用（actor 强制为客户端 = 玩家 2，防篡改）
    Action act;
    act.type           = static_cast<ActionType>(am.actType > 2 ? 0 : am.actType);
    act.pos            = Pos{ static_cast<int>(am.posX), static_cast<int>(am.posY) };
    act.actor          = clientId();
    act.interfereKind  = am.interfereKind;

    const TurnResult res = m_engine.apply(m_game, act);

    sendAck(from, p.header.seq);       // 先 ACK，再广播结果

    if (res == TurnResult::Invalid) {
        sendRejected(from, res, p.header.seq);
        ++m_stats.actionsRejected;
    } else {
        ++m_stats.actionsApplied;
        if (m_game.isOver())
            m_state = State::Finished;
        broadcastSnapshot(nowMs, true);
    }
}

// ===========================================================================
//  发送
// ===========================================================================
void HostSession::sendAck(const Endpoint& to, std::uint16_t seq)
{
    Packet a;
    a.header.type  = MsgType::Ack;
    a.header.seq   = ++m_outSeq;
    a.header.ack   = seq;
    a.header.flags = FlagIsAck;
    const auto bin = a.encode();
    m_sock.sendTo(to, bin.data(), bin.size());
    ++m_stats.packetsOut;
}

void HostSession::sendRejected(const Endpoint& to, TurnResult reason, std::uint16_t ackSeq)
{
    Packet r;
    r.header.type  = MsgType::Rejected;
    r.header.seq   = ++m_outSeq;
    r.header.ack   = ackSeq;
    r.header.flags = FlagAckRequired | FlagIsAck;
    r.payload.push_back(static_cast<std::uint8_t>(reason));

    const auto bin = r.encode();
    m_sock.sendTo(to, bin.data(), bin.size());
    ++m_stats.packetsOut;
}

void HostSession::sendWelcome(const Endpoint& to)
{
    Writer w;
    w.u32(m_game.seed());
    w.u16(static_cast<std::uint16_t>(m_game.config().mapW));
    w.u16(static_cast<std::uint16_t>(m_game.config().mapH));
    w.u8(static_cast<std::uint8_t>(std::min(99, static_cast<int>(m_game.config().mineRatio * 100))));
    w.u8(static_cast<std::uint8_t>(m_game.config().startLives));
    w.u16(static_cast<std::uint16_t>(m_game.config().turnSeconds));
    w.u8(m_game.config().allowInterfere ? 1 : 0);
    w.u8(static_cast<std::uint8_t>(clientId()));

    Packet p;
    p.header.type  = MsgType::Welcome;
    p.header.seq   = ++m_outSeq;
    p.header.flags = FlagAckRequired;
    p.payload      = w.buf;

    const auto bin = p.encode();
    m_sock.sendTo(to, bin.data(), bin.size());
    ++m_stats.packetsOut;
}

// ===========================================================================
//  广播快照（可能分片）
// ===========================================================================
void HostSession::broadcastSnapshot(std::uint32_t nowMs, bool force)
{
    if (!m_clientValid)
        return;

    const auto snap = m_game.serialize();
    m_lastSnapMs = nowMs;

    // 无需分片：一个包能装下
    if (static_cast<int>(snap.size()) <= kMaxPacket - kHeaderSize) {
        Packet p;
        p.header.type  = MsgType::Snapshot;
        p.header.seq   = ++m_outSeq;
        p.header.flags = 0;              // 快照不可靠，不需要 ACK
        p.payload      = snap;

        const auto bin = p.encode();
        m_sock.sendTo(m_client, bin.data(), bin.size());
        ++m_stats.packetsOut;
        ++m_stats.snapsSent;
        (void)force;
        return;
    }

    // 需要分片
    const std::size_t chunk = static_cast<std::size_t>(kMaxPacket - kHeaderSize);
    const std::uint8_t count = static_cast<std::uint8_t>(
        (snap.size() + chunk - 1) / chunk);
    const std::uint32_t fid = ++m_fragId;

    for (std::uint8_t i = 0; i < count; ++i) {
        const std::size_t off = i * chunk;
        const std::size_t n   = std::min(chunk, snap.size() - off);

        Packet p;
        p.header.type      = MsgType::Snapshot;
        p.header.seq       = ++m_outSeq;
        p.header.flags     = FlagFragment;
        p.header.fragIndex = i;
        p.header.fragCount = count;
        p.header.fragId    = fid;
        p.payload.assign(snap.begin() + off, snap.begin() + off + n);

        const auto bin = p.encode();
        m_sock.sendTo(m_client, bin.data(), bin.size());
        ++m_stats.packetsOut;
    }
    ++m_stats.snapsSent;
}

// ===========================================================================
//  主机本地操作
// ===========================================================================
TurnResult HostSession::submitLocalAction(const Action& act)
{
    if (m_state != State::InGame)
        return TurnResult::Invalid;

    Action a = act;
    a.actor = 1;                        // 主机固定是玩家 1

    const TurnResult res = m_engine.apply(m_game, a);
    if (res != TurnResult::Invalid) {
        ++m_stats.actionsApplied;
        if (m_game.isOver())
            m_state = State::Finished;
        broadcastSnapshot(0, true);
    } else {
        ++m_stats.actionsRejected;
    }
    return res;
}

} // namespace net
} // namespace ms
