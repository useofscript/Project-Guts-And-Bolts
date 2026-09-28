#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Net { class Connection; class Listener; }

// The Guts&Bolts server: one program that keeps everyone's accounts, Bolts,
// badges and uploads (hats, shirts, pants, audio, plugins and games) in a
// folder, and answers the apps' signed requests (see online/Protocol.h).
//
// Everything is saved as plain JSON files in the data folder, so backing the
// server up is just copying that folder.
class GbServer {
public:
    struct Options {
        int         port = 7780;
        std::filesystem::path data = "server_data";
        std::string official;          // the staff account's ID ("" = the one built in)
        std::string name = "Guts&Bolts";
    };

    explicit GbServer(Options opts);
    ~GbServer();
    bool start(std::string& error);
    void runForever();                 // until stop() / Ctrl+C
    void step(int waitMs);             // one round of accept / read / answer
    void stop() { m_running = false; }

    // Handle one already-parsed request (also used by the tests). Returns the reply.
    nlohmann::json handle(const nlohmann::json& request);

private:
    struct Entry { long long amount = 0; std::string reason; long long time = 0; std::string ref; };
    struct User {
        std::string id, name = "Player";
        long long   created = 0, lastSeen = 0;
        std::vector<Entry> ledger;
        std::map<std::string, std::string> grants;   // badge key -> signature
        std::set<std::string> owned;                 // asset ids
        std::string uploadDay; int uploadsToday = 0;
        std::string playDay;   long long playEarned = 0, lastPlay = 0;
        bool        banned = false;
        std::set<std::string> friends, friendIn, friendOut;   // friends; requests to me; requests I sent
    };
    struct Asset {
        std::string id, kind, name, description, creator;
        long long   price = 0, created = 0, sales = 0, plays = 0;
        size_t      size = 0;
        nlohmann::json meta = nlohmann::json::object();
    };

    struct Post { std::string by, text; long long time = 0; };
    struct Group {
        std::string id, name, description, owner;
        long long   created = 0;
        int         color = 0x3A7BD5;                   // emblem colour (0xRRGGBB)
        bool        open = true;                         // anyone can join (false: ask first)
        std::map<std::string, std::string> members;      // account id -> "Owner" / "Admin" / "Member"
        std::set<std::string> requests;                  // waiting to join
        Post        shout;
        std::vector<Post> wall;                          // newest last, at most 200
    };

    // A connection to the server. Most just send requests; the relay turns some
    // into a host's control line or into one end of a pipe between a player and a host.
    struct Client {
        enum class Mode { Request, HostControl, PendingJoin, Pipe };
        std::unique_ptr<Net::Connection> conn;
        Mode        mode = Mode::Request;
        long long   lastActive = 0, since = 0;
        std::string account, session, ticket;
        Client*     peer = nullptr;      // the other end of a pipe
        bool        closing = false;     // drop once everything queued has been sent
    };
    // A game server running on someone's computer, reached only through us, so
    // players never learn each other's IP addresses.
    struct Session {
        std::string id, game, title, host, code;   // code: private servers only
        bool        priv = false;
        int         max = 12;
        long long   created = 0;
        Client*     control = nullptr;
        std::set<Client*> players;               // the player end of each pipe
    };

    // Requests
    nlohmann::json op(const std::string& name, User& me, const nlohmann::json& args);
    nlohmann::json groupOp(const std::string& name, User& me, const nlohmann::json& args);   // ServerGroups.cpp
    nlohmann::json friendOp(const std::string& name, User& me, const nlohmann::json& args);   // ServerFriends.cpp
    nlohmann::json serverOp(const std::string& name, User& me, const nlohmann::json& args);   // ServerRelay.cpp
    nlohmann::json checkRequest(const nlohmann::json& req, User*& me);   // null = fine, else the failure reply
    void relayRequest(Client& c, const nlohmann::json& req);            // relay.host / relay.join
    void relayAccept(Client& c, const std::string& ticket);
    void relayStep(long long now);
    void dropClients(long long now);
    nlohmann::json sessionJson(const Session& s) const;
    const Session* sessionOf(const std::string& userId) const;          // the game they're in right now
    bool isOnline(const User& u) const;
    nlohmann::json publicGroup(const Group& g) const;
    nlohmann::json badgesOf(const User& u) const;         // badge keys that check out
    std::vector<const Group*> groupsOf(const std::string& userId) const;
    nlohmann::json meJson(const User& u) const;
    nlohmann::json publicUser(const User& u) const;
    nlohmann::json publicAsset(const Asset& a) const;

    // Accounts
    User&     user(const std::string& id);          // makes a new account (with the welcome gift) if needed
    User*     findUser(const std::string& id);
    long long balance(const User& u) const;
    bool      hasRef(const User& u, const std::string& ref) const;
    void      add(User& u, long long amount, const std::string& reason, const std::string& ref);
    bool      isOfficial(const User& u) const;
    bool      isStaff(const User& u) const;         // official, or has the Staff badge
    bool      isVerified(const User& u) const;      // staff always are

    // Files
    void load();
    void saveUsers();
    void saveAssets();
    void saveGroups();
    void loadGroups();
    std::filesystem::path blobPath(const std::string& assetId) const;

    Options m_opts;
    std::unique_ptr<Net::Listener> m_listener;
    std::vector<std::unique_ptr<Client>> m_clients;
    std::map<std::string, User>  m_users;
    std::map<std::string, Asset> m_assets;
    std::map<std::string, Group> m_groups;
    std::map<std::string, Session> m_sessions;
    std::map<std::string, long long> m_lastPost;     // account -> when they last wrote on a wall
    std::map<std::string, long long> m_seenNonces;   // account+nonce -> when, to stop replays
    bool m_running = false;
};
