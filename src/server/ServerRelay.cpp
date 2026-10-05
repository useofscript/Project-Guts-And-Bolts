// Game servers through the Guts&Bolts server (the "relay").
//
// Someone's computer still runs the game (the host), but nobody connects to
// anybody directly: the host and every player connect to this server, and it
// passes the game's messages along. So players never see each other's IP
// address, and nobody has to open ports on their router.
//
//   host:    relay.host {game, title, private, max}  -> this connection becomes its control line
//   player:  relay.join {session | code}             -> we tell the host: {"t":"incoming","ticket"}
//   host:    new connection, {"t":"accept","ticket"}  -> the two connections are joined into a pipe
//
// Public servers are what the Play button finds (servers.play). Private ones
// need the host to be your friend, or their code.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../net/Socket.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr int       kDefaultMax     = 12;
constexpr int       kMostPlayers    = 30;
constexpr size_t    kHostedEach     = 3;     // servers one account can host at once
constexpr long long kJoinWait       = 15;    // seconds for the host to pick up a joining player
constexpr long long kHostSilence    = 90;    // hosts ping every 20 s
constexpr long long kPipeSilence    = 120;

std::string makeCode() {
    static const char* letters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // no 0/O or 1/I mix-ups
    std::string hex = Account::randomHex(6), code;
    for (size_t i = 0; i < 6; ++i) code += letters[std::stoi(hex.substr(i * 2, 2), nullptr, 16) % 32];
    return code;
}
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
} // namespace

json GbServer::sessionJson(const Session& s) const {
    const User* h = nullptr;
    if (auto it = m_users.find(s.host); it != m_users.end()) h = &it->second;
    // Who's in it (the first few, with their avatars), for the server cards on game pages.
    json people = json::array();
    auto person = [&](const std::string& id) {
        if (people.size() >= 5) return;
        auto it = m_users.find(id);
        people.push_back({{"id", id}, {"name", it != m_users.end() ? it->second.name : "?"},
                          {"avatar", it != m_users.end() && !it->second.avatar.is_null() ? it->second.avatar : json()}});
    };
    person(s.host);
    for (const Client* p : s.players) person(p->account);
    return {{"id", s.id}, {"game", s.game}, {"title", s.title}, {"players", (int)s.players.size() + 1},
            {"max", s.max}, {"private", s.priv}, {"hostName", h ? h->name : "?"},
            {"hostVerified", h && isVerified(*h)}, {"people", people}};
}

const GbServer::Session* GbServer::sessionOf(const std::string& userId) const {
    for (const auto& [id, s] : m_sessions) {
        if (s.host == userId) return &s;
        for (const Client* p : s.players) if (p->account == userId) return &s;
    }
    return nullptr;
}

json GbServer::serverOp(const std::string& name, User& me, const json& args) {
    std::string game = Online::cleanText(args.value("game", std::string()), 80);
    if (auto f = findAsset(game); f != m_assets.end()) game = f->first;   // (its number works too)

    if (name == "servers.play") {
        // The fullest public server of this game that still has room, like Roblox.
        const Session* best = nullptr;
        for (const auto& [id, s] : m_sessions) {
            if (s.priv || s.game != game || s.host == me.id || (int)s.players.size() + 1 >= s.max) continue;
            if (!best || s.players.size() > best->players.size()) best = &s;
        }
        json r = okay();
        if (best) r["join"] = best->id;
        else      r["host"] = true;   // nobody's playing: you start a new public server
        return r;
    }
    if (name == "servers.list") {
        json list = json::array();
        for (const auto& [id, s] : m_sessions) {
            if (!game.empty() && s.game != game) continue;
            bool friendHost = me.friends.count(s.host) > 0;
            if (s.priv && !friendHost && s.host != me.id) continue;   // private: only the host's friends see it
            json j = sessionJson(s);
            int friends = friendHost ? 1 : 0;
            for (const Client* p : s.players) if (me.friends.count(p->account)) ++friends;
            j["friends"] = friends;
            list.push_back(j);
            if (list.size() >= 100) break;
        }
        json r = okay();
        r["servers"] = list;
        return r;
    }
    return fail("Unknown request.");
}

void GbServer::relayRequest(Client& c, const json& req) {
    auto reply = [&](json r) {
        r["t"] = "relay";
        c.conn->send(r.dump());
    };
    User* me = nullptr;
    json bad = checkRequest(req, me);
    if (!bad.is_null()) { reply(bad); c.closing = true; return; }
    std::string op = req.value("op", std::string());
    json args = req.contains("args") && req["args"].is_object() ? req["args"] : json::object();
    long long now = Online::unixNow();

    if (op == "relay.host") {
        size_t hosting = 0;
        for (const auto& [id, s] : m_sessions) if (s.host == me->id) ++hosting;
        if (hosting >= kHostedEach) { reply(fail("You're already running " + std::to_string(kHostedEach) + " servers.")); c.closing = true; return; }
        Session s;
        s.id = "s-" + Account::randomHex(6);
        s.game = Online::cleanText(args.value("game", std::string()), 80);
        if (auto f = findAsset(s.game); f != m_assets.end()) s.game = f->first;
        s.title = Online::cleanText(args.value("title", std::string()), 60);
        if (s.title.empty()) s.title = "A game";
        s.host = me->id;
        s.priv = args.value("private", false);
        s.max = std::clamp(args.contains("max") && args["max"].is_number_integer() ? args["max"].get<int>() : kDefaultMax, 2, kMostPlayers);
        s.created = now;
        s.control = &c;
        if (s.priv) {
            do s.code = makeCode();
            while (std::any_of(m_sessions.begin(), m_sessions.end(), [&](const auto& kv) { return kv.second.code == s.code; }));
        }
        c.mode = Client::Mode::HostControl;
        c.account = me->id;
        c.session = s.id;
        json r = okay();
        r["session"] = s.id;
        r["code"] = s.code;
        r["private"] = s.priv;
        log(std::string(s.priv ? "private" : "public") + " server " + s.id + " (" + s.title + ") by " + me->id.substr(0, 8));
        m_sessions[s.id] = std::move(s);
        reply(r);
        return;
    }

    if (op == "relay.join") {
        Session* s = nullptr;
        std::string code = upper(Online::cleanText(args.value("code", std::string()), 12));
        if (!code.empty()) {
            for (auto& [id, x] : m_sessions) if (x.code == code) s = &x;
            if (!s) { reply(fail("There's no private server with that code (it may have closed).")); c.closing = true; return; }
        } else if (auto it = m_sessions.find(args.value("session", std::string())); it != m_sessions.end()) {
            s = &it->second;
        }
        if (!s) { reply(fail("That server has closed.")); c.closing = true; return; }
        if (s->host == me->id) { reply(fail("That's your own server.")); c.closing = true; return; }
        if (s->priv && code.empty() && !me->friends.count(s->host)) {
            reply(fail("That's a private server. You need to be the host's friend, or have its code."));
            c.closing = true;
            return;
        }
        int waiting = 0;
        for (const auto& o : m_clients) if (o->mode == Client::Mode::PendingJoin && o->session == s->id) ++waiting;
        if ((int)s->players.size() + 1 + waiting >= s->max) { reply(fail("That server is full.")); c.closing = true; return; }
        c.mode = Client::Mode::PendingJoin;
        c.account = me->id;
        c.session = s->id;
        c.ticket = Account::randomHex(16);
        c.since = now;
        // The host only hears a ticket (and who's coming), never an address.
        s->control->conn->send(json{{"t", "incoming"}, {"ticket", c.ticket}, {"account", me->id}, {"name", me->name},
                                                    {"guest", me->userId == 0}}.dump());
        return;
    }
    reply(fail("Unknown request."));
    c.closing = true;
}

void GbServer::relayAccept(Client& c, const std::string& ticket) {
    Client* joiner = nullptr;
    if (ticket.size() >= 16)
        for (auto& o : m_clients)
            if (o->mode == Client::Mode::PendingJoin && o->ticket == ticket) joiner = o.get();
    auto it = joiner ? m_sessions.find(joiner->session) : m_sessions.end();
    if (!joiner || it == m_sessions.end()) { c.closing = true; return; }
    Session& s = it->second;
    joiner->mode = c.mode = Client::Mode::Pipe;
    joiner->peer = &c;
    joiner->ticket.clear();
    c.peer = joiner;
    c.session = s.id;
    c.account = s.host;
    s.players.insert(joiner);
    // Everything after this line is the game's own messages, passed straight through.
    joiner->conn->send(json{{"t", "relay"}, {"ok", true}, {"title", s.title}}.dump());
}

void GbServer::relayStep(long long now) {
    for (auto& c : m_clients) {
        if (c->mode != Client::Mode::PendingJoin || c->closing) continue;
        bool gone = !m_sessions.count(c->session);
        if (gone || now - c->since > kJoinWait) {
            c->conn->send(json{{"t", "relay"}, {"ok", false},
                               {"error", gone ? "That server just closed." : "The server's host didn't answer. Try another server."}}.dump());
            c->closing = true;
        }
    }
}

void GbServer::dropClients(long long now) {
    std::set<Client*> dead;
    for (auto& up : m_clients) {
        Client* c = up.get();
        long long quiet = now - c->lastActive;
        bool idle = c->mode == Client::Mode::HostControl ? quiet > kHostSilence
                  : c->mode == Client::Mode::Pipe        ? quiet > kPipeSilence
                  : c->mode == Client::Mode::PendingJoin ? false
                  : quiet > 120 && c->conn->pendingBytes() == 0;
        if (!c->conn->alive() || idle || (c->closing && c->conn->pendingBytes() == 0)) dead.insert(c);
    }
    if (dead.empty()) return;
    // A pipe takes its other end with it; a host leaving closes its server (and everyone in it).
    for (bool grew = true; grew;) {
        grew = false;
        for (Client* c : std::vector<Client*>(dead.begin(), dead.end()))
            if (c->peer && dead.insert(c->peer).second) grew = true;
        for (auto it = m_sessions.begin(); it != m_sessions.end();) {
            if (!dead.count(it->second.control)) { ++it; continue; }
            for (Client* p : it->second.players) if (dead.insert(p).second) grew = true;
            log("server " + it->first + " closed");
            it = m_sessions.erase(it);
        }
    }
    for (auto& [id, s] : m_sessions)
        for (Client* c : dead) s.players.erase(c);
    m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(),
                                   [&](const std::unique_ptr<Client>& c) { return dead.count(c.get()) > 0; }),
                    m_clients.end());
}
