#pragma once
#include <cstdint>
#include <memory>
#include <string>

// Tiny cross-platform TCP layer. Messages are length-prefixed strings (we send
// JSON). Everything is non-blocking so the game never freezes on the network.
namespace Net {

class Connection {
public:
    explicit Connection(intptr_t sock, std::string address);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Connect to host:port (waits up to `timeoutMs`). Returns null + error text on failure.
    static std::unique_ptr<Connection> connectTo(const std::string& host, int port,
                                                 std::string& error, int timeoutMs = 4000);

    void send(const std::string& message);   // queued; sent by poll()
    bool poll();                              // send + receive; false once disconnected
    bool pop(std::string& message);           // next complete message, if any
    bool alive() const { return m_alive; }
    const std::string& address() const { return m_address; }
    size_t pendingBytes() const { return m_out.size(); }

private:
    intptr_t    m_sock;
    std::string m_address;
    std::string m_in, m_out;
    bool        m_alive = true;
};

class Listener {
public:
    ~Listener();
    bool open(int port, std::string& error);
    void close();
    std::unique_ptr<Connection> accept();     // null if nobody is waiting
    bool isOpen() const { return m_sock >= 0; }

private:
    intptr_t m_sock = -1;
};

// "192.168.1.20" etc. — this computer's addresses, to tell friends where to join.
std::string localAddresses();

} // namespace Net
