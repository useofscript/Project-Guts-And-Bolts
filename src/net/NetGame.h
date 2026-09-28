#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Socket.h"

class Scene;
class GameSession;

// Chat messages and the speech bubbles above characters' heads.
struct ChatLog {
    struct Line { std::string from, text; bool system = false; };
    std::vector<Line> lines;
    std::unordered_map<std::string, std::pair<std::string, float>> bubbles;   // name -> text, seconds left

    void add(const std::string& from, const std::string& text, bool system = false);
    void update(float dt);
};

inline constexpr int kDefaultPort = 7777;

// Hosting a multiplayer game. The host runs the scripts and physics for the
// world; every joined player simulates their own character and sends its pose.
class NetServer {
public:
    NetServer(Scene* scene, GameSession* session);
    ~NetServer();

    bool start(int port, std::string& error);
    void stop();
    void update(float dt);
    void say(const std::string& text);                 // the host's own chat
    std::vector<std::string> playerNames() const;
    ChatLog& chat() { return m_chat; }
    int port() const { return m_port; }

private:
    struct Client;
    struct NodeState;
    void handle(Client& c, const std::string& msg);
    void sendTick();
    void dropClient(size_t index, const char* reason);
    void broadcast(const std::string& msg, const Client* except = nullptr);
    std::string worldMessage(bool full);

    Scene*       m_scene;
    GameSession* m_session;
    Net::Listener m_listener;
    std::vector<std::unique_ptr<Client>> m_clients;
    std::unordered_map<uint64_t, std::string> m_sent;   // node id -> last replicated state
    std::string m_lastEnv, m_lastGui;
    ChatLog     m_chat;
    float       m_tick = 0.0f;
    int         m_nextId = 1;
    int         m_port = kDefaultPort;
};

// Joining someone else's game.
class NetClient {
public:
    enum class State { Idle, Connecting, Joined, Failed };

    NetClient(Scene* scene, GameSession* session);
    ~NetClient();

    bool connect(const std::string& host, int port);
    void disconnect();
    void update(float dt);
    void say(const std::string& text);
    void reportTouch(uint64_t partId, const std::string& limb);
    void reportClick(uint64_t partId);

    State state() const { return m_state; }
    const std::string& error() const { return m_error; }
    const std::string& gameTitle() const { return m_title; }
    std::vector<std::string> playerNames() const { return m_players; }
    ChatLog& chat() { return m_chat; }

private:
    void handle(const std::string& msg);

    Scene*       m_scene;
    GameSession* m_session;
    std::unique_ptr<Net::Connection> m_conn;
    State        m_state = State::Idle;
    std::string  m_error, m_title;
    uint64_t     m_myServerRoot = 0;     // our character on the host (hidden here)
    std::map<uint64_t, std::string> m_charNames;
    std::vector<std::string> m_players;
    ChatLog      m_chat;
    float        m_tick = 0.0f;
    bool         m_wasDead = false;
};
