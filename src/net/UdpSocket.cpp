#include "UdpSocket.h"

#include <cstring>
#include <sstream>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "ws2_32.lib")
#  endif
using raw_sock_t = SOCKET;
#  define MS_INVALID_SOCK INVALID_SOCKET
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
using raw_sock_t = int;
#  define MS_INVALID_SOCK (-1)
#endif

namespace ms {
namespace net {

#ifdef _WIN32
bool UdpSocket::s_wsaReady = false;
#endif

namespace {

void setBlockingImpl(raw_sock_t s, bool blocking)
{
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(s, FIONBIO, &mode);
    (void)s;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    if (flags >= 0)
        fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
#endif
}

int lastSocketError()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool isWouldBlock(int err)
{
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAEINTR;
#else
    return err == EAGAIN || err == EWOULDBLOCK || err == EINTR;
#endif
}

} // namespace

std::string Endpoint::toString() const
{
    std::ostringstream os;
    os << ((ip >> 24) & 0xFF) << '.' << ((ip >> 16) & 0xFF) << '.'
       << ((ip >> 8) & 0xFF) << '.' << (ip & 0xFF) << ':' << port;
    return os.str();
}

UdpSocket::UdpSocket() = default;

UdpSocket::~UdpSocket()
{
    close();
}

void UdpSocket::setError(const std::string& s)
{
    m_lastError = s;
}

bool UdpSocket::isOpen() const
{
#ifdef _WIN32
    return m_sock != static_cast<std::uintptr_t>(MS_INVALID_SOCK);
#else
    return m_sock >= 0;
#endif
}

bool UdpSocket::open(std::uint16_t localPort, std::string* error)
{
    close();

#ifdef _WIN32
    if (!s_wsaReady) {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            setError("WSAStartup 失败");
            if (error) *error = m_lastError;
            return false;
        }
        s_wsaReady = true;
    }
#endif

    raw_sock_t s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == MS_INVALID_SOCK) {
        setError("socket() 失败, err=" + std::to_string(lastSocketError()));
        if (error) *error = m_lastError;
        return false;
    }

    // 允许端口复用，避免「上次没关干净」导致绑定失败
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(localPort);

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        setError("bind() 失败端口 " + std::to_string(localPort) +
                 ", err=" + std::to_string(lastSocketError()));
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        if (error) *error = m_lastError;
        return false;
    }

#ifdef _WIN32
    m_sock = static_cast<std::uintptr_t>(s);
#else
    m_sock = s;
#endif

    setBlockingImpl(s, false);      // 非阻塞

    // 查询系统实际分配的端口（当 localPort == 0 时）
    sockaddr_in actual{};
#ifdef _WIN32
    int alen = sizeof(actual);
#else
    socklen_t alen = sizeof(actual);
#endif
    if (getsockname(s, reinterpret_cast<sockaddr*>(&actual), &alen) == 0)
        m_localPort = ntohs(actual.sin_port);
    else
        m_localPort = localPort;

    return true;
}

void UdpSocket::close()
{
    if (!isOpen())
        return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(m_sock));
    m_sock = static_cast<std::uintptr_t>(MS_INVALID_SOCK);
#else
    ::close(m_sock);
    m_sock = -1;
#endif
    m_localPort = 0;
}

void UdpSocket::setNonBlocking(bool on)
{
    if (isOpen())
        setBlockingImpl(static_cast<raw_sock_t>(m_sock), !on);
}

int UdpSocket::sendTo(const Endpoint& to, const std::uint8_t* data, std::size_t len)
{
    if (!isOpen() || !data)
        return -1;

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(to.ip);
    addr.sin_port        = htons(to.port);

    const int n = static_cast<int>(
        ::sendto(static_cast<raw_sock_t>(m_sock),
                 reinterpret_cast<const char*>(data), static_cast<int>(len), 0,
                 reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
    if (n < 0)
        setError("sendto 失败, err=" + std::to_string(lastSocketError()));
    return n;
}

int UdpSocket::recvFrom(Endpoint& from, std::vector<std::uint8_t>& out, std::size_t maxBytes)
{
    out.clear();
    if (!isOpen())
        return -1;

    out.resize(maxBytes);

    sockaddr_in addr{};
#ifdef _WIN32
    int alen = sizeof(addr);
#else
    socklen_t alen = sizeof(addr);
#endif

    const int n = static_cast<int>(
        ::recvfrom(static_cast<raw_sock_t>(m_sock),
                   reinterpret_cast<char*>(out.data()), static_cast<int>(maxBytes), 0,
                   reinterpret_cast<sockaddr*>(&addr), &alen));

    if (n < 0) {
        const int err = lastSocketError();
        out.clear();
        if (isWouldBlock(err))
            return 0;                    // 没有数据，不是错误
        setError("recvfrom 失败, err=" + std::to_string(err));
        return -1;
    }

    out.resize(static_cast<std::size_t>(n));
    from.ip   = ntohl(addr.sin_addr.s_addr);
    from.port = ntohs(addr.sin_port);
    return n;
}

bool UdpSocket::resolve(const std::string& host, std::uint16_t port, Endpoint& out)
{
    out.port = port;

    // 先当纯 IP 解析
    in_addr ia{};
    if (inet_pton(AF_INET, host.c_str(), &ia) == 1) {
        out.ip = ntohl(ia.s_addr);
        return true;
    }

    // 走 DNS
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
        return false;

    const auto* sin = reinterpret_cast<sockaddr_in*>(res->ai_addr);
    out.ip = ntohl(sin->sin_addr.s_addr);
    freeaddrinfo(res);
    return true;
}

} // namespace net
} // namespace ms
