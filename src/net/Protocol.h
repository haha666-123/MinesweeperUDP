#pragma once
// ============================================================================
//  UDP 协议设计
//  ---------------------------------------------------------------------------
//  设计目标：双人对战，主机权威（Host Authoritative）。
//    * Host 持有唯一权威状态，客户端只能「请求」，不能「断言结果」
//    * 客户端发操作，Host 校验后广播状态快照
//    * UDP 不可靠：每条消息带序列号，Host 记录客户端已确认的序号
//    * 快照可能超过一个 UDP 包：按分片发送，客户端重组
//
//  为什么用 UDP 而不是 TCP（面试必问）：
//    这类回合制小游戏，一条 UDP 丢包的代价是「重发一次」；
//    而 TCP 的队头阻塞 + 重传延迟，在手感上更差，且 TCP 长连接还要处理
//    NAT 超时、断线重连。UDP + 自己做「序列号 + 关键消息重发」更可控。
//    真正的实时游戏（FPS）则完全不敢用 TCP。
//
//  消息分两类：
//    * 可靠类（Reliable）：客户端操作请求、握手 —— 需要 ACK，丢了重发
//    * 不可靠类（Unreliable）：状态快照 —— 后一帧会覆盖前一帧，丢了不要紧
// ============================================================================
#include "../core/Types.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ms {
namespace net {

// ---------------------------------------------------------------- 协议常量
constexpr std::uint32_t kMagic      = 0x4D534B50;   // 'MSKP'，用于快速丢弃非本协议包
constexpr std::uint8_t  kVersion    = 1;
constexpr int           kMaxPacket  = 1200;          // 留出安全余量，避免 IP 分片
// 头部字段字节数（与 writeHeader / readHeader 严格一致）：
//   magic   u32 = 4
//   version u8  = 1
//   type    u8  = 1
//   seq     u16 = 2
//   ack     u16 = 2
//   flags   u16 = 2
//   fragIdx u8  = 1
//   fragCnt u8  = 1
//   fragId  u32 = 4
//   合计       = 18
constexpr int           kHeaderSize = 18;

// 编译期自检：任何人改动 Header 结构都必须同步这里
static_assert(kHeaderSize == 4 + 1 + 1 + 2 + 2 + 2 + 1 + 1 + 4,
              "kHeaderSize 必须与 writeHeader 的字段总和一致");

// ---------------------------------------------------------------- 消息类型
enum class MsgType : std::uint8_t {
    Hello       = 1,    // 客户端 -> Host：请求加入（可靠）
    Welcome     = 2,    // Host -> 客户端：同意加入，带上 seed 与配置（可靠）
    Action      = 3,    // 客户端 -> Host：提交一次操作（可靠）
    Rejected    = 4,    // Host -> 客户端：操作被拒绝 + 原因（可靠）
    Snapshot    = 5,    // Host -> 客户端：状态快照（不可靠，可分片）
    Ping        = 6,    // 双向：保活 / 测延迟
    Pong        = 7,    // 对 Ping 的响应
    Bye         = 8,    // 任一方主动断开
    Ack         = 9,    // 确认某个序列号（可靠消息专用）
};

// ---------------------------------------------------------------- 标志位
enum : std::uint16_t {
    FlagAckRequired = 1u << 0,   // 这条消息需要对方回 ACK
    FlagIsAck       = 1u << 1,   // 这条是纯 ACK 包（无负载）
    FlagFragment    = 1u << 2,   // 该消息是分片
};

// ---------------------------------------------------------------- 头部
// 注意：所有字段用「手工序列化」而不是直接 memcpy 结构体，
// 避免结构体对齐填充导致不同编译器/平台字节布局不一致。
struct Header {
    std::uint32_t magic      = kMagic;
    std::uint8_t  version    = kVersion;
    MsgType       type       = MsgType::Ping;
    std::uint16_t seq        = 0;      // 发送方递增序列号
    std::uint16_t ack        = 0;      // 确认收到的对方序列号
    std::uint16_t flags      = 0;
    // 分片信息（仅 FlagFragment 时有效）
    std::uint8_t  fragIndex  = 0;
    std::uint8_t  fragCount  = 1;
    std::uint32_t fragId     = 0;      // 同一消息的所有分片共享
};

// ---------------------------------------------------------------- 操作消息体
struct ActionMsg {
    std::uint8_t actType = static_cast<std::uint8_t>(ActionType::Reveal);
    std::uint16_t posX   = 0;
    std::uint16_t posY   = 0;
    std::uint8_t interfereKind = 0;
};

// ---------------------------------------------------------------- 编解码工具
class Writer {
public:
    std::vector<std::uint8_t> buf;

    void u8(std::uint8_t v) { buf.push_back(v); }
    void u16(std::uint16_t v)
    {
        buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
        buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    }
    void u32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            buf.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void bytes(const std::uint8_t* p, std::size_t n) { buf.insert(buf.end(), p, p + n); }
};

class Reader {
public:
    Reader(const std::uint8_t* d, std::size_t n) : m_p(d), m_n(n) {}

    bool ok() const { return m_ok; }
    std::size_t remaining() const { return m_n; }

    std::uint8_t u8()
    {
        if (m_n < 1) { m_ok = false; return 0; }
        --m_n;
        return *m_p++;
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
    const std::uint8_t* raw(std::size_t n)
    {
        if (m_n < n) { m_ok = false; return nullptr; }
        const std::uint8_t* p = m_p;
        m_p += n;
        m_n -= n;
        return p;
    }

private:
    const std::uint8_t* m_p;
    std::size_t         m_n;
    bool                m_ok = true;
};

// ---------------------------------------------------------------- 头编解码
inline void writeHeader(Writer& w, const Header& h)
{
    w.u32(h.magic);
    w.u8(h.version);
    w.u8(static_cast<std::uint8_t>(h.type));
    w.u16(h.seq);
    w.u16(h.ack);
    w.u16(h.flags);
    w.u8(h.fragIndex);
    w.u8(h.fragCount);
    w.u32(h.fragId);
}

inline bool readHeader(Reader& r, Header& h)
{
    h.magic   = r.u32();
    h.version = r.u8();
    h.type    = static_cast<MsgType>(r.u8());
    h.seq     = r.u16();
    h.ack     = r.u16();
    h.flags   = r.u16();
    h.fragIndex = r.u8();
    h.fragCount = r.u8();
    h.fragId    = r.u32();

    if (!r.ok() || h.magic != kMagic || h.version != kVersion)
        return false;
    if (h.fragCount == 0 || h.fragCount > 64 || h.fragIndex >= h.fragCount)
        return false;
    return true;
}

// ---------------------------------------------------------------- 打包
struct Packet {
    Header                    header;
    std::vector<std::uint8_t> payload;

    [[nodiscard]] std::vector<std::uint8_t> encode() const
    {
        Writer w;
        w.buf.reserve(kHeaderSize + payload.size());
        writeHeader(w, header);
        w.bytes(payload.data(), payload.size());
        return w.buf;
    }
};

inline bool decode(const std::uint8_t* data, std::size_t len, Packet& out)
{
    if (!data || len < static_cast<std::size_t>(kHeaderSize))
        return false;

    Reader r(data, len);
    if (!readHeader(r, out.header))
        return false;

    const std::size_t body = len - static_cast<std::size_t>(kHeaderSize);
    out.payload.assign(data + kHeaderSize, data + kHeaderSize + body);
    return true;
}

// ---------------------------------------------------------------- 分片重组
// 快照可能超过 kMaxPacket，必须切片。接收端按 fragId 分组重组。
class FragmentAssembler {
public:
    //! 喂入一个分片；返回 true 表示一组分片已完整组装完毕
    bool feed(const Packet& p, std::vector<std::uint8_t>& assembled)
    {
        if ((p.header.flags & FlagFragment) == 0) {
            assembled = p.payload;          // 未分片，直接用
            return true;
        }

        const std::uint8_t count = p.header.fragCount;
        const std::uint8_t idx   = p.header.fragIndex;
        if (idx >= count || count > 64)
            return false;

        auto& slot = m_slots[p.header.fragId];
        if (slot.parts.size() != count) {
            slot.parts.resize(count);
            slot.got.assign(count, false);
            slot.received = 0;
        }

        if (!slot.got[idx]) {
            slot.parts[idx] = p.payload;
            slot.got[idx]   = true;
            ++slot.received;
        }

        if (slot.received != count)
            return false;

        // 全部到齐，拼接
        std::size_t total = 0;
        for (const auto& part : slot.parts)
            total += part.size();

        assembled.clear();
        assembled.reserve(total);
        for (const auto& part : slot.parts)
            assembled.insert(assembled.end(), part.begin(), part.end());

        m_slots.erase(p.header.fragId);
        return true;
    }

    //! 清理太久没凑齐的分片，防止内存泄漏
    void cleanup(std::uint32_t keepFragIdsBelow)
    {
        for (auto it = m_slots.begin(); it != m_slots.end();) {
            if (it->first < keepFragIdsBelow)
                it = m_slots.erase(it);
            else
                ++it;
        }
    }

    [[nodiscard]] std::size_t pendingGroups() const { return m_slots.size(); }

private:
    struct Slot {
        std::vector<std::vector<std::uint8_t>> parts;
        std::vector<bool>                      got;
        std::uint8_t                           received = 0;
    };
    std::unordered_map<std::uint32_t, Slot> m_slots;
};

} // namespace net
} // namespace ms
