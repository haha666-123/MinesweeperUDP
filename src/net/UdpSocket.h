#pragma once
// ============================================================================
//  UdpSocket：跨平台 UDP 收发封装
//  ---------------------------------------------------------------------------
//  Windows: Winsock2 (WSAStartup + sendto/recvfrom)
//  POSIX  : BSD sockets
//  全部非阻塞：recv 没有数据立即返回 0，不阻塞主循环。
//  这也是游戏循环需要的方式——每帧 poll 一次，有就处理，没有就继续渲染。
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace ms {
namespace net {

struct Endpoint {
    std::uint32_t ip   = 0;      // 主机字节序
    std::uint16_t port = 0;      // 主机字节序

    [[nodiscard]] std::string toString() const;
    friend bool operator==(const Endpoint& a, const Endpoint& b)
    {
        return a.ip == b.ip && a.port == b.port;
    }
};

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&)            = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    //! 绑定本地端口（port = 0 表示系统分配）；返回实际端口
    bool open(std::uint16_t localPort, std::string* error);
    void close();
    [[nodiscard]] bool isOpen() const;

    [[nodiscard]] std::uint16_t localPort() const { return m_localPort; }

    //! 设置非阻塞（默认已开启）
    void setNonBlocking(bool on);

    //! 发送；返回字节数，失败返回 -1
    int sendTo(const Endpoint& to, const std::uint8_t* data, std::size_t len);

    //! 接收一条；无数据返回 0，出错返回 -1
    int recvFrom(Endpoint& from, std::vector<std::uint8_t>& out, std::size_t maxBytes = 2048);

    [[nodiscard]] std::string lastError() const { return m_lastError; }

    //! 解析主机名或 IP（如 "127.0.0.1" / "localhost"）
    static bool resolve(const std::string& host, std::uint16_t port, Endpoint& out);

private:
    void setError(const std::string& s);

#ifdef _WIN32
    std::uintptr_t m_sock = static_cast<std::uintptr_t>(~0ull);   // INVALID_SOCKET
    static bool    s_wsaReady;
#else
    int m_sock = -1;
#endif
    std::uint16_t m_localPort = 0;
    std::string   m_lastError;
};

} // namespace net
} // namespace ms
