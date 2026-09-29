#pragma once
#include <cstdint>
#include <memory>
#include <string>

// Tiny cross-platform TCP layer. Messages are length-prefixed strings (we send
// JSON). Everything is non-blocking so the game never freezes on the network.
//
// A host written "wss://example.com" (or "ws://..." without encryption) means a
// Guts&Bolts server on the web, like the one on Cloudflare: the connection is
// then a WebSocket to its /ws, with each message sent as one WebSocket message.
// Everything else about a Connection works the same.
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
    struct Web;                    // the WebSocket (and TLS) side, for wss:// hosts
    bool webPoll();
    friend bool httpPost(const std::string&, int, const std::string&, const std::string&, std::string&, std::string&, int);

    intptr_t    m_sock;
    std::string m_address;
    std::string m_in, m_out;
    bool        m_alive = true;
    std::unique_ptr<Web> m_web;
};

// "wss://..." or "ws://...": a server on the web (see above).
bool isWebAddress(const std::string& host);

// One HTTP(S) POST to a web server (host "wss://example.com" means https, like
// above; port 443 usually). Fills `reply` with the answer's body.
bool httpPost(const std::string& host, int port, const std::string& path, const std::string& body,
              std::string& reply, std::string& error, int timeoutMs = 30000);

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
