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
    };
    struct Asset {
        std::string id, kind, name, description, creator;
        long long   price = 0, created = 0, sales = 0, plays = 0;
        size_t      size = 0;
        nlohmann::json meta = nlohmann::json::object();
    };

    // Requests
    nlohmann::json op(const std::string& name, User& me, const nlohmann::json& args);
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
    std::filesystem::path blobPath(const std::string& assetId) const;

    struct Client;
    Options m_opts;
    std::unique_ptr<Net::Listener> m_listener;
    std::vector<std::unique_ptr<Client>> m_clients;
    std::map<std::string, User>  m_users;
    std::map<std::string, Asset> m_assets;
    std::map<std::string, long long> m_seenNonces;   // account+nonce -> when, to stop replays
    bool m_running = false;
};
