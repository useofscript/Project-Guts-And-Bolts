#include "Socket.h"

#include <csignal>
#include <algorithm>
#include <cstring>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#define GB_CLOSE closesocket
#define GB_WOULDBLOCK (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINPROGRESS)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define GB_CLOSE ::close
#define GB_WOULDBLOCK (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS)
#endif

namespace Net {

namespace {

constexpr size_t kMaxMessage = 64u * 1024u * 1024u;

bool initSockets() {
#ifdef _WIN32
    static bool ok = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ok;
#else
    static bool ok = [] {
        signal(SIGPIPE, SIG_IGN);   // a dropped connection must not kill the game
        return true;
    }();
    return ok;
#endif
}

void setNonBlocking(intptr_t s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket((SOCKET)s, FIONBIO, &on);
#else
    int flags = fcntl((int)s, F_GETFL, 0);
    fcntl((int)s, F_SETFL, flags | O_NONBLOCK);
#endif
    int one = 1;
    setsockopt((decltype(socket(0, 0, 0)))s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

} // namespace

// ---------------------------------------------------------------------------

Connection::Connection(intptr_t sock, std::string address)
    : m_sock(sock), m_address(std::move(address)) {
    setNonBlocking(m_sock);
}

Connection::~Connection() {
    if (m_sock >= 0) GB_CLOSE((decltype(socket(0, 0, 0)))m_sock);
}

std::unique_ptr<Connection> Connection::connectTo(const std::string& host, int port,
                                                  std::string& error, int timeoutMs) {
    if (!initSockets()) { error = "Networking isn't available on this computer."; return nullptr; }

    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        error = "Couldn't find \"" + host + "\". Check the address.";
        return nullptr;
    }

    auto s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if ((intptr_t)s < 0) { freeaddrinfo(res); error = "Couldn't create a network socket."; return nullptr; }
    setNonBlocking((intptr_t)s);

    int rc = ::connect(s, res->ai_addr, (socklen_t)res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0 && !GB_WOULDBLOCK) {
        GB_CLOSE(s);
        error = "Couldn't connect to " + host + ":" + portStr + ".";
        return nullptr;
    }
    if (rc != 0) {
        fd_set wr;
        FD_ZERO(&wr);
        FD_SET(s, &wr);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        if (select((int)s + 1, nullptr, &wr, nullptr, &tv) <= 0) {
            GB_CLOSE(s);
            error = "Nobody answered at " + host + ":" + portStr + ". Is the game hosted and the port open?";
            return nullptr;
        }
        int soErr = 0;
        socklen_t len = sizeof(soErr);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &len);
        if (soErr != 0) {
            GB_CLOSE(s);
            error = "The connection to " + host + ":" + portStr + " was refused.";
            return nullptr;
        }
    }
    return std::make_unique<Connection>((intptr_t)s, host + ":" + portStr);
}

void Connection::send(const std::string& message) {
    if (!m_alive) return;
    uint32_t n = (uint32_t)message.size();
    unsigned char hdr[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16),
                            (unsigned char)(n >> 8), (unsigned char)n};
    m_out.append((const char*)hdr, 4);
    m_out += message;
}

bool Connection::poll() {
    if (!m_alive) return false;
    auto s = (decltype(socket(0, 0, 0)))m_sock;

    // Send as much as the network will take right now.
    while (!m_out.empty()) {
        int sent = ::send(s, m_out.data(), (int)std::min<size_t>(m_out.size(), 1 << 16), 0);
        if (sent > 0) { m_out.erase(0, (size_t)sent); continue; }
        if (sent < 0 && GB_WOULDBLOCK) break;
        m_alive = false;
        return false;
    }

    // Read everything that has arrived.
    char buf[65536];
    for (;;) {
        int got = ::recv(s, buf, sizeof(buf), 0);
        if (got > 0) { m_in.append(buf, (size_t)got); continue; }
        if (got < 0 && GB_WOULDBLOCK) break;
        m_alive = false;   // 0 = closed by the other side, <0 = error
        return false;
    }
    return true;
}

bool Connection::pop(std::string& message) {
    if (m_in.size() < 4) return false;
    const unsigned char* p = (const unsigned char*)m_in.data();
    uint32_t n = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    if (n > kMaxMessage) { m_alive = false; return false; }
    if (m_in.size() < 4 + (size_t)n) return false;
    message.assign(m_in, 4, n);
    m_in.erase(0, 4 + (size_t)n);
    return true;
}

// ---------------------------------------------------------------------------

Listener::~Listener() { close(); }

bool Listener::open(int port, std::string& error) {
    close();
    if (!initSockets()) { error = "Networking isn't available on this computer."; return false; }
    auto s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if ((intptr_t)s < 0) { error = "Couldn't create a network socket."; return false; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 16) != 0) {
        GB_CLOSE(s);
        error = "Port " + std::to_string(port) + " is already in use (is another game hosted?).";
        return false;
    }
    setNonBlocking((intptr_t)s);
    m_sock = (intptr_t)s;
    return true;
}

void Listener::close() {
    if (m_sock >= 0) GB_CLOSE((decltype(socket(0, 0, 0)))m_sock);
    m_sock = -1;
}

std::unique_ptr<Connection> Listener::accept() {
    if (m_sock < 0) return nullptr;
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    auto c = ::accept((decltype(socket(0, 0, 0)))m_sock, (sockaddr*)&addr, &len);
    if ((intptr_t)c < 0) return nullptr;
    char ip[64] = "?";
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::make_unique<Connection>((intptr_t)c, ip);
}

std::string localAddresses() {
    std::string out;
#ifdef _WIN32
    initSockets();
    char name[256];
    if (gethostname(name, sizeof(name)) != 0) return "";
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &res) != 0) return "";
    for (addrinfo* a = res; a; a = a->ai_next) {
        char ip[64];
        inet_ntop(AF_INET, &((sockaddr_in*)a->ai_addr)->sin_addr, ip, sizeof(ip));
        if (std::strncmp(ip, "127.", 4) == 0) continue;
        if (!out.empty()) out += ", ";
        out += ip;
    }
    freeaddrinfo(res);
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return "";
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        char ip[64];
        inet_ntop(AF_INET, &((sockaddr_in*)a->ifa_addr)->sin_addr, ip, sizeof(ip));
        if (std::strncmp(ip, "127.", 4) == 0) continue;
        if (!out.empty()) out += ", ";
        out += ip;
    }
    freeifaddrs(list);
#endif
    return out;
}

} // namespace Net
