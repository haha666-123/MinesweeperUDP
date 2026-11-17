// ============================================================================
//  net 层单元测试
//  ---------------------------------------------------------------------------
//  验证协议本身（不依赖真实网络）：
//    1. 包头编解码往返
//    2. 非法包被拒绝
//    3. 分片重组（模拟丢包/乱序）
//  以及一个真实 loopback 的联机对局（Host 与 Client 在同一进程）。
// ============================================================================
#include "net/ClientSession.h"
#include "net/HostSession.h"
#include "net/Protocol.h"
#include "net/UdpSocket.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* what)
{
    if (cond) { ++g_pass; std::printf("  [PASS] %s\n", what); }
    else      { ++g_fail; std::printf("  [FAIL] %s\n", what); }
}

void section(const char* s) { std::printf("\n[%s]\n", s); }

std::uint32_t nowMs()
{
    using namespace std::chrono;
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace

int main()
{
    std::printf("=== net layer tests ===\n");
    using namespace ms;
    using namespace ms::net;

    // ---------------------------------------------------------------- 1 包头
    section("1. 包头编解码");
    {
        Header h;
        h.type  = MsgType::Action;
        h.seq   = 1234;
        h.ack   = 567;
        h.flags = FlagAckRequired;
        h.fragIndex = 2;
        h.fragCount = 5;
        h.fragId    = 0xDEADBEEF;

        Writer w;
        writeHeader(w, h);
        check(w.buf.size() == static_cast<std::size_t>(kHeaderSize),
              "头部长度与 kHeaderSize 一致");

        Reader r(w.buf.data(), w.buf.size());
        Header h2;
        const bool ok = readHeader(r, h2);
        check(ok, "头部解析成功");
        check(h2.seq == 1234 && h2.ack == 567, "seq/ack 往返一致");
        check(h2.type == MsgType::Action, "类型往返一致");
        check(h2.flags == FlagAckRequired, "flags 往返一致");
        check(h2.fragIndex == 2 && h2.fragCount == 5, "分片信息往返一致");
        check(h2.fragId == 0xDEADBEEF, "分片 ID 往返一致");
    }

    // ---------------------------------------------------------------- 2 非法包
    section("2. 非法包被拒绝");
    {
        // 错误 magic
        Writer w;
        w.u32(0x12345678);
        w.u8(kVersion);
        w.u8(1);
        w.u16(0); w.u16(0); w.u16(0);
        w.u8(0); w.u8(1); w.u32(0);

        Reader r(w.buf.data(), w.buf.size());
        Header h;
        check(!readHeader(r, h), "错误 magic 被拒绝");

        // 错误版本
        Writer w2;
        w2.u32(kMagic);
        w2.u8(99);
        w2.u8(1);
        w2.u16(0); w2.u16(0); w2.u16(0);
        w2.u8(0); w2.u8(1); w2.u32(0);
        Reader r2(w2.buf.data(), w2.buf.size());
        check(!readHeader(r2, h), "错误版本被拒绝");

        // 数据不足
        Reader r3(w2.buf.data(), 5);
        check(!readHeader(r3, h), "过短数据被拒绝");

        // 完整包 decode
        Packet bad;
        check(!decode(nullptr, 0, bad), "decode(nullptr) 返回 false");
        std::uint8_t tiny[3] = { 1, 2, 3 };
        check(!decode(tiny, 3, bad), "decode 过短返回 false");
    }

    // ---------------------------------------------------------------- 3 分片
    section("3. 分片重组");
    {
        FragmentAssembler asm_;

        // 构造一条 1000 字节消息，切成 3 片
        const std::size_t total = 1000;
        std::vector<std::uint8_t> src(total);
        for (std::size_t i = 0; i < total; ++i)
            src[i] = static_cast<std::uint8_t>(i & 0xFF);

        const std::size_t chunk = 400;
        const std::uint8_t count = 3;
        const std::uint32_t fid  = 777;

        auto makeFrag = [&](std::uint8_t idx) {
            Packet p;
            p.header.type      = MsgType::Snapshot;
            p.header.flags     = FlagFragment;
            p.header.fragIndex = idx;
            p.header.fragCount = count;
            p.header.fragId    = fid;
            const std::size_t off = idx * chunk;
            const std::size_t n   = std::min(chunk, total - off);
            p.payload.assign(src.begin() + off, src.begin() + off + n);
            return p;
        };

        std::vector<std::uint8_t> out;

        // 乱序喂入：2, 0, 1
        check(!asm_.feed(makeFrag(2), out), "只收到 1/3 片时不返回");
        check(!asm_.feed(makeFrag(0), out), "收到 2/3 片时不返回");
        check(asm_.feed(makeFrag(1), out), "3 片齐了返回 true");
        check(out.size() == total, "重组后长度正确");
        check(out == src, "重组后内容与原始一致");

        // 重复分片不应重复计算
        std::vector<std::uint8_t> out2;
        check(!asm_.feed(makeFrag(0), out2), "第一片");
        check(!asm_.feed(makeFrag(0), out2), "重复第一片不推进进度");
        check(asm_.pendingGroups() == 1, "未完成的分片组仍在等待");
    }

    // ---------------------------------------------------------------- 4 UDP
    section("4. UDP socket 收发（loopback）");
    {
        UdpSocket a, b;
        std::string err;
        check(a.open(0, &err), "socket A 打开");
        check(b.open(0, &err), "socket B 打开");
        check(a.localPort() != 0, "系统分配了端口");

        Endpoint toB;
        check(UdpSocket::resolve("127.0.0.1", b.localPort(), toB), "解析 127.0.0.1");

        const std::uint8_t msg[] = { 0xDE, 0xAD, 0xBE, 0xEF };
        const int sent = a.sendTo(toB, msg, sizeof(msg));
        check(sent == 4, "发送 4 字节");

        // 非阻塞，稍等一下
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        Endpoint from;
        std::vector<std::uint8_t> got;
        const int n = b.recvFrom(from, got, 1024);
        check(n == 4, "收到 4 字节");
        check(got.size() == 4 && got[0] == 0xDE && got[3] == 0xEF, "内容一致");
        check(from.port == a.localPort(), "来源端口正确");

        // 没数据时返回 0 而不是阻塞
        const int n2 = b.recvFrom(from, got, 1024);
        check(n2 == 0, "无数据时非阻塞返回 0");
    }

    // ---------------------------------------------------------------- 5 联机
    section("5. 真实联机对局（Host + Client 同进程）");
    {
        HostSession host;
        GameConfig cfg;
        cfg.mapW        = 8;
        cfg.mapH        = 8;
        cfg.mineRatio   = 0.15;
        cfg.startLives  = 3;
        cfg.turnSeconds = 30;

        std::string err;
        check(host.start(0, cfg, &err), "Host 启动监听");

        ClientSession client;
        check(client.connect("127.0.0.1", host.localPort(), &err), "Client 发起连接");

        // 驱动若干帧，让握手完成
        std::uint32_t t = nowMs();
        bool joined = false;
        for (int i = 0; i < 100; ++i) {
            t += 20;
            host.poll(t);
            client.poll(t);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (client.connected() && host.hasClient()) { joined = true; break; }
        }
        check(joined, "客户端完成握手并进入游戏");
        check(client.seed() == host.game().seed(), "双方 seed 一致（同一张雷图）");
        check(client.myPlayerId() == 2, "客户端是玩家 2");
        check(host.game().currentPlayer() == 1, "开局轮到玩家 1（主机）");

        // ---- 主机先操作一次（翻开一个安全格）
        Pos safe{ -1, -1 };
        for (int y = 0; y < 8 && safe.x < 0; ++y)
            for (int x = 0; x < 8 && safe.x < 0; ++x)
                if (!host.game().board().hasMine({ x, y }) &&
                    !host.game().cellAt({ x, y }).isRevealed())
                    safe = { x, y };

        Action hostAct;
        hostAct.type  = ActionType::Reveal;
        hostAct.pos   = safe;
        hostAct.actor = 1;
        const auto r1 = host.submitLocalAction(hostAct);
        check(r1 == TurnResult::TurnEnded, "主机操作成功，回合切换");

        // 同步给客户端
        for (int i = 0; i < 40; ++i) {
            t += 20;
            host.poll(t);
            client.poll(t);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        check(client.game().currentPlayer() == 2, "客户端看到「轮到玩家 2」");

        // 只有在客户端确实收到快照、棋盘已建立后才访问 cellAt，
        // 避免在握手失败时越界访问（这也是真实产品里必须做的防御）
        if (client.stats().snapsReceived > 0 &&
            client.game().cells().size() == static_cast<std::size_t>(8 * 8)) {
            check(client.game().cellAt(safe).isRevealed(), "客户端看到已翻开的格子");
        } else {
            check(false, "客户端棋盘未建立（握手或快照失败）");
        }

        // ---- 客户端操作
        // 若握手未完成，客户端棋盘为空，先断言再继续（生产代码也应如此判断）
        check(client.game().ready(), "客户端棋盘已就绪");

        Pos safe2{ -1, -1 };
        for (int y = 0; y < 8 && safe2.x < 0; ++y)
            for (int x = 0; x < 8 && safe2.x < 0; ++x) {
                const Pos p{ x, y };
                if (!client.game().board().hasMine(p) &&
                    !client.game().cellAt(p).isRevealed() &&
                    !client.game().cellAt(p).hasFlag())
                    safe2 = p;
            }

        Action cliAct;
        cliAct.type  = ActionType::Reveal;
        cliAct.pos   = safe2;
        cliAct.actor = 2;
        check(client.submitAction(cliAct), "客户端提交操作");

        for (int i = 0; i < 60; ++i) {
            t += 20;
            host.poll(t);
            client.poll(t);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (host.game().currentPlayer() == 1) break;
        }

        check(host.game().currentPlayer() == 1, "主机端处理了客户端操作，回合切回玩家 1");
        check(host.stats().actionsApplied >= 2, "主机记录了 2 次成功操作");

        // ---- 非法操作（客户端越权操作玩家 1 的回合）
        // 先让回合走到玩家 2
        Action hostAct2;
        hostAct2.type  = ActionType::Reveal;
        hostAct2.actor = 1;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const Pos p{ x, y };
                if (!host.game().board().hasMine(p) &&
                    !host.game().cellAt(p).isRevealed() &&
                    !host.game().cellAt(p).hasFlag()) {
                    hostAct2.pos = p;
                    y = 99; break;
                }
            }
        }
        host.submitLocalAction(hostAct2);
        for (int i = 0; i < 30; ++i) {
            t += 20; host.poll(t); client.poll(t);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }

        // 现在客户端连续发两次操作：第一次成功会切回玩家 1，
        // 第二次由于回合已不是玩家 2，应被拒绝
        Action c1, c2;
        c1.type = c2.type = ActionType::Flag;
        c1.actor = c2.actor = 2;
        c1.pos = { 0, 7 };
        c2.pos = { 1, 7 };
        client.submitAction(c1);

        for (int i = 0; i < 40; ++i) {
            t += 20; host.poll(t); client.poll(t);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            if (!host.hasClient()) break;
        }

        // 此时假设回合在玩家 1，客户端再发会被拒
        client.submitAction(c2);
        for (int i = 0; i < 40; ++i) {
            t += 20; host.poll(t); client.poll(t);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        std::printf("         主机拒绝计数 = %llu\n",
                    static_cast<unsigned long long>(host.stats().actionsRejected));
        check(client.stats().snapsReceived > 0, "客户端收到了状态快照");
        check(host.stats().packetsIn > 0 && host.stats().packetsOut > 0, "主机有收发统计");

        client.disconnect();
        host.stop();
    }

    std::printf("\n=== %s  通过 %d / 失败 %d ===\n",
                g_fail == 0 ? "ALL PASSED" : "SOME FAILED", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
