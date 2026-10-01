#include "Server.h"
#include "../online/Protocol.h"
#include "../core/Account.h"
#include "../core/Paths.h"
#include "../net/Socket.h"
#include "ServerUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <set>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace ServerUtil;


GbServer::GbServer(Options opts) : m_opts(std::move(opts)) {
    if (!m_opts.official.empty()) Account::setOfficialId(m_opts.official);
}
GbServer::~GbServer() = default;

// ---------------------------------------------------------------------------
// Starting and running
// ---------------------------------------------------------------------------

bool GbServer::start(std::string& error) {
    std::error_code ec;
    fs::create_directories(m_opts.data / "files", ec);
    if (ec) { error = "Couldn't make the data folder " + m_opts.data.string() + ": " + ec.message(); return false; }
    load();
    addExampleGames();
    m_listener = std::make_unique<Net::Listener>();
    if (!m_listener->open(m_opts.port, error)) return false;
    m_running = true;
    return true;
}

void GbServer::runForever() {
    while (m_running) step(5);
}

void GbServer::step(int waitMs) {
    long long now = Online::unixNow();
    while (auto c = m_listener->accept()) {
        if (m_clients.size() >= 1024) continue;   // too busy: drop it
        auto cl = std::make_unique<Client>();
        cl->conn = std::move(c);
        cl->lastActive = cl->since = now;
        m_clients.push_back(std::move(cl));
    }
    for (size_t i = 0; i < m_clients.size(); ++i) {   // (relay requests can add clients while we go)
        Client* c = m_clients[i].get();
        if (!c->conn->poll() || c->closing) continue;
        std::string msg;
        while (!c->closing && c->conn->pop(msg)) {
            c->lastActive = now;
            if (c->mode == Client::Mode::Pipe) {           // game traffic: pass it on untouched
                if (c->peer) { c->peer->conn->send(msg); c->peer->lastActive = now; }
                continue;
            }
            json req = json::parse(msg, nullptr, false);
            if (c->mode == Client::Mode::HostControl) continue;   // pings keep it alive
            if (c->mode == Client::Mode::PendingJoin) continue;   // waiting for the host
            if (!req.is_object()) { c->conn->send(fail("That wasn't a proper request.").dump()); continue; }
            if (req.value("t", std::string()) == "accept") { relayAccept(*c, req.value("ticket", std::string())); continue; }
            if (req.value("op", std::string()).rfind("relay.", 0) == 0) { relayRequest(*c, req); continue; }
            json reply = handle(req);
            if (req.contains("id")) reply["id"] = req["id"];   // lets clients match replies
            c->conn->send(reply.dump());
        }
    }
    for (auto& c : m_clients) if (c->conn->pendingBytes()) c->conn->poll();   // send what we queued straight away
    relayStep(now);
    dropClients(now);
    // Forget old nonces.
    for (auto it = m_seenNonces.begin(); it != m_seenNonces.end();)
        it = now - it->second > 2 * Online::kMaxClockSkew ? m_seenNonces.erase(it) : std::next(it);
    std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
}

// ---------------------------------------------------------------------------
// Checking requests
// ---------------------------------------------------------------------------

json GbServer::handle(const json& req) {
    User* me = nullptr;
    json bad = checkRequest(req, me);
    if (!bad.is_null()) return bad;
    json args = req.contains("args") && req["args"].is_object() ? req["args"] : json::object();
    try {
        return op(req.value("op", std::string()), *me, args);
    } catch (const std::exception& e) {
        return fail(std::string("The server hit a problem: ") + e.what());
    }
}

json GbServer::checkRequest(const json& req, User*& out) {
    std::string opName = req.value("op", std::string());
    std::string account = lower(req.value("account", std::string()));
    std::string nonce = req.value("nonce", std::string());
    long long   time = req.value("time", 0LL);
    json        args = req.contains("args") && req["args"].is_object() ? req["args"] : json::object();

    if (opName.empty() || opName.size() > 40) return fail("Unknown request.");
    if (!isHex(account, 64, 64)) return fail("Missing account.");
    if (!isHex(nonce, 8, 64)) return fail("Missing nonce.");
    long long now = Online::unixNow();
    if (std::llabs(now - time) > Online::kMaxClockSkew)
        return fail("Your computer's clock is too far off from the server's. Fix the date and time and try again.");
    if (!Account::verify(account, Online::requestText(opName, account, time, nonce, args), req.value("sig", std::string())))
        return fail("That request wasn't signed by its account.");
    std::string key = account + nonce;
    if (m_seenNonces.count(key)) return fail("That request was already sent once.");
    m_seenNonces[key] = now;

    User& me = user(account);
    me.lastSeen = now;
    if (me.banned && me.bannedUntil > 0 && now >= me.bannedUntil) {   // a timed ban is over
        me.banned = false;
        me.banReason.clear(); me.banNote.clear();
        me.bannedAt = me.bannedUntil = 0;
        saveUsers();
    }
    if (me.banned && opName != "hello") return fail(Online::banMessage(me.banReason, me.banNote));
    // Everything else needs a signed-up account (hello just says who we are),
    // except looking around: visitors to the website can browse before signing up.
    // Guests can also play: download games, find and join servers (they can't chat in games).
    static const std::set<std::string> kLookOnly = {"list", "profile", "users.search", "groups.list", "groups.get",
                                                    "servers.list", "stats", "thumb.get", "updates.list",
                                                    "get", "servers.play", "relay.host", "relay.join"};
    if (me.userId == 0 && opName != "hello" && opName != "ping" && opName.rfind("account.", 0) != 0 &&
        !kLookOnly.count(opName))
        return fail("Sign up or log in first.");
    out = &me;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Accounts
// ---------------------------------------------------------------------------

GbServer::User* GbServer::findUser(const std::string& id) {
    auto it = m_users.find(lower(id));
    return it == m_users.end() ? nullptr : &it->second;
}

GbServer::User& GbServer::user(const std::string& id) {
    auto it = m_users.find(id);
    if (it != m_users.end()) return it->second;
    User& u = m_users[id];
    u.id = id;
    u.created = Online::unixNow();
    claimOfficial(u);   // (the welcome Bolts come with signing up)
    log("new account " + id.substr(0, 8));
    return u;
}

long long GbServer::balance(const User& u) const {
    long long b = 0;
    for (const Entry& e : u.ledger) b += e.amount;
    return b;
}

bool GbServer::hasRef(const User& u, const std::string& ref) const {
    for (const Entry& e : u.ledger) if (e.ref == ref) return true;
    return false;
}

void GbServer::add(User& u, long long amount, const std::string& reason, const std::string& ref) {
    u.ledger.push_back({amount, reason, Online::unixNow(), ref});
    if (u.ledger.size() > 2000) {   // keep the history short, but keep the balance
        long long old = 0;
        size_t cut = u.ledger.size() - 1500;
        for (size_t i = 0; i < cut; ++i) old += u.ledger[i].amount;
        u.ledger.erase(u.ledger.begin(), u.ledger.begin() + (long)cut);
        u.ledger.insert(u.ledger.begin(), Entry{old, "Earlier history", 0, "carry"});
    }
    saveUsers();
}

bool GbServer::isOfficial(const User& u) const { return Account::isOfficial(u.id); }

bool GbServer::isStaff(const User& u) const {
    if (isOfficial(u)) return true;
    auto it = u.grants.find("staff");
    return it != u.grants.end() && Online::grantValid(Account::officialId(), "staff", u.id, it->second);
}

bool GbServer::isVerified(const User& u) const {
    if (isOfficial(u)) return true;
    auto it = u.grants.find("verified");
    return it != u.grants.end() && Online::grantValid(Account::officialId(), "verified", u.id, it->second);
}

json GbServer::publicUser(const User& u) const {
    return {{"id", u.id}, {"name", u.name}, {"username", u.username}, {"userId", u.userId},
            {"verified", isVerified(u)}, {"staff", isStaff(u)},
            {"official", isOfficial(u)}, {"created", u.created}, {"banned", u.banned},
            {"banReason", u.banned ? u.banReason : std::string()}};
}

json GbServer::meJson(const User& u) const {
    json j = publicUser(u);
    j["bolts"] = balance(u);
    j["hasPassword"] = !u.keyBlob.empty();   // can log in on other devices
    json g = json::array();
    for (const auto& [k, s] : u.grants) g.push_back({k, s});
    j["grants"] = g;
    std::string today = Online::utcDay(Online::unixNow());
    j["canDaily"] = !hasRef(u, "daily:" + today);
    j["playEarnedToday"] = u.playDay == today ? u.playEarned : 0;
    j["uploadsLeft"] = isVerified(u) ? -1
                     : Online::kDailyUploadsUnverified - (u.uploadDay == today ? u.uploadsToday : 0);
    j["owned"] = json(u.owned);
    j["avatar"] = u.avatar;
    // Banned: what for and until when (the app and the site show a ban screen).
    if (u.banned) {
        const char* title = Online::banReasonTitle(u.banReason);
        j["ban"] = {{"reason", u.banReason}, {"title", title ? title : "Breaking the rules"}, {"note", u.banNote},
                    {"at", u.bannedAt}, {"until", u.bannedUntil}};
    } else {
        j["ban"] = nullptr;
    }
    // Staff warnings not seen yet (shown once, until "I understand").
    json warn = json::array();
    if (u.warnings.is_array())
        for (const json& w : u.warnings) {
            if (!w.is_object() || w.value("seen", false)) continue;
            const char* title = Online::banReasonTitle(w.value("reason", std::string()));
            warn.push_back({{"id", w.value("id", std::string())}, {"reason", w.value("reason", std::string())},
                            {"title", title ? title : "Breaking the rules"}, {"note", w.value("note", std::string())},
                            {"at", w.value("at", 0LL)}});
        }
    j["warnings"] = warn;
    j["warningCount"] = u.warnings.is_array() ? u.warnings.size() : 0;
    return j;
}

json GbServer::publicAsset(const Asset& a) const {
    json j = {{"id", a.id}, {"kind", a.kind}, {"name", a.name}, {"description", a.description},
              {"creator", a.creator}, {"price", a.price}, {"created", a.created}, {"sales", a.sales},
              {"plays", a.plays}, {"size", a.size}, {"meta", a.meta}, {"thumb", a.thumb}};
    if (a.kind == "game") j["badges"] = a.badges;
    auto it = m_users.find(a.creator);
    j["creatorName"] = it != m_users.end() ? it->second.name : "?";
    j["creatorVerified"] = it != m_users.end() && isVerified(it->second);
    j["creatorStaff"] = it != m_users.end() && isStaff(it->second);
    return j;
}

// ---------------------------------------------------------------------------
// The requests themselves
// ---------------------------------------------------------------------------

json GbServer::op(const std::string& name, User& me, const json& args) {
    const long long now = Online::unixNow();
    const std::string today = Online::utcDay(now);
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto num = [&](const char* k) { return args.contains(k) && args[k].is_number() ? args[k].get<long long>() : 0LL; };

    // --- Account -----------------------------------------------------------
    if (name == "hello") {
        std::string n = Online::cleanText(str("name"), 20);
        if (me.userId > 0) n = me.username;   // signed up: your name is your username
        if (!n.empty()) {
            if (Account::nameIsReserved(n) && !isOfficial(me)) n = "Player";
            me.name = n;
        }
        // Badges this player got offline (with a code) come along for the ride.
        if (args.contains("grants") && args["grants"].is_array())
            for (const auto& g : args["grants"])
                if (g.is_array() && g.size() == 2 && g[0].is_string() && g[1].is_string() &&
                    Online::grantValid(Account::officialId(), g[0], me.id, g[1]))
                    me.grants[g[0].get<std::string>()] = g[1].get<std::string>();
        saveUsers();
        json r = okay();
        r["me"] = meJson(me);
        r["server"] = {{"name", m_opts.name}, {"protocol", Online::kProtocol}, {"official", Account::officialId()}};
        return r;
    }
    if (name == "profile") {
        std::string want = str("id");
        User* u = !want.empty() && want.size() < 12 && std::all_of(want.begin(), want.end(), ::isdigit)
                      ? findUserId(std::atoll(want.c_str())) : findUser(want);   // user number or account key
        if (!u || u->userId == 0) return fail("There's no account with that ID on this server.");
        json r = okay();
        r["user"] = publicUser(*u);
        r["user"]["badges"] = badgesOf(*u);
        r["user"]["avatar"] = u->avatar;
        json made = json::array();
        for (const auto& [id, a] : m_assets) if (a.creator == u->id) made.push_back(publicAsset(a));
        r["creations"] = made;
        json groups = json::array();
        for (const Group* g : groupsOf(u->id)) {
            json pg = publicGroup(*g);
            pg["role"] = g->members.at(u->id);
            groups.push_back(pg);
        }
        r["groups"] = groups;
        r["friendCount"] = u->friends.size();
        r["friendship"] = u->id == me.id ? "self" : me.friends.count(u->id) ? "friends"
                        : me.friendOut.count(u->id) ? "sent" : me.friendIn.count(u->id) ? "received" : "none";
        // Like a Roblox profile: what they're wearing, some friends, visits to their games.
        // (friends get theirs too, so their little pictures are dressed)
        auto wornItems = [&](const User& who) {
            json out = json::array();
            if (who.avatar.is_object() && who.avatar.contains("wearing") && who.avatar["wearing"].is_array())
                for (const auto& id : who.avatar["wearing"])
                    if (id.is_string())
                        if (auto it = m_assets.find(id.get<std::string>()); it != m_assets.end()) out.push_back(publicAsset(it->second));
            return out;
        };
        r["wearing"] = wornItems(*u);
        json friends = json::array();
        for (const std::string& fid : u->friends) {
            if (friends.size() >= 9) break;
            auto it = m_users.find(fid);
            if (it == m_users.end()) continue;
            json f = publicUser(it->second);
            f["avatar"] = it->second.avatar;
            f["online"] = isOnline(it->second);
            f["wearing"] = wornItems(it->second);
            friends.push_back(f);
        }
        r["friends"] = friends;
        r["online"] = isOnline(*u);
        long long visits = 0;
        for (const auto& [id, a] : m_assets) if (a.creator == u->id && a.kind == "game") visits += a.plays;
        r["placeVisits"] = visits;
        // Game badges (made by creators, earned in games), newest first.
        json gameBadges = json::array();
        for (const json& e : u->gameBadges) {
            if (!e.is_array() || e.size() < 3) continue;
            auto g = m_assets.find(e[1].get<std::string>());
            if (g == m_assets.end()) continue;
            for (const json& b : g->second.badges)
                if (b.value("id", std::string()) == e[0].get<std::string>())
                {
                    json one = {{"id", b["id"]}, {"name", b["name"]}, {"description", b["description"]},
                                {"color", b["color"]}, {"game", g->first}, {"gameName", g->second.name}, {"earned", e[2]}};
                    gameBadges.insert(gameBadges.begin(), one);
                }
        }
        r["gameBadges"] = gameBadges;
        return r;
    }
    if (name == "users.search") {
        // Anyone can look people up by name (or the start of their account ID).
        std::string q = lower(Online::cleanText(str("query"), 64));
        if (!q.empty() && q[0] == '#') q.erase(0, 1);            // "#12" = user 12
        if (!q.empty() && q[0] == '@') q.erase(0, 1);            // "@name" = username
        long long wantId = !q.empty() && q.size() < 12 && std::all_of(q.begin(), q.end(), ::isdigit) ? std::atoll(q.c_str()) : -1;
        // No name typed: show who's been around lately. Only signed-up accounts show up.
        std::vector<const User*> found;
        for (const auto& [id, u] : m_users)
            if (!u.banned && u.userId > 0 &&
                (q.empty() || u.userId == wantId || lower(u.name).find(q) != std::string::npos ||
                 lower(u.username).find(q) != std::string::npos))
                found.push_back(&u);
        // Exact matches first, then Verified people, then the most recently seen.
        std::sort(found.begin(), found.end(), [&](const User* a, const User* b) {
            bool ea = lower(a->username) == q || a->userId == wantId, eb = lower(b->username) == q || b->userId == wantId;
            if (ea != eb) return ea;
            bool va = isVerified(*a), vb = isVerified(*b);
            if (va != vb) return va;
            return a->lastSeen > b->lastSeen;
        });
        json list = json::array();
        for (const User* u : found) {
            if (list.size() >= 50) break;
            list.push_back(publicUser(*u));
        }
        json r = okay(); r["users"] = list; return r;
    }
    if (name.rfind("account.", 0) == 0) return accountOp(name, me, args);
    if (name.rfind("groups.", 0) == 0) return groupOp(name, me, args);
    if (name.rfind("friends.", 0) == 0) return friendOp(name, me, args);
    if (name.rfind("servers.", 0) == 0) return serverOp(name, me, args);
    if (name == "ping") {   // "I'm still here" (for friends' online dots); the answer keeps your account fresh
        json r = okay(); r["me"] = meJson(me); return r;
    }

    // --- Your look, and pictures of games --------------------------------------
    if (name == "avatar.set") {
        // Shared by the website and the apps. Colours are 0-255 whole numbers.
        const json a = args.contains("avatar") && args["avatar"].is_object() ? args["avatar"] : json();
        if (a.is_null()) return fail("That avatar looks wrong.");
        auto rgb = [&](const char* k, bool allowNone, json& out) {
            if (!a.contains(k) || !a[k].is_array() || a[k].size() != 3) return false;
            out = json::array();
            bool none = true;
            for (const auto& v : a[k]) { if (!v.is_number_integer()) return false; none = none && v.get<long long>() < 0; }
            for (const auto& v : a[k]) out.push_back(allowNone && none ? -1LL : std::clamp(v.get<long long>(), 0LL, 255LL));
            return true;
        };
        json av = json::object();
        for (const char* part : {"head", "torso", "leftArm", "rightArm", "leftLeg", "rightLeg"}) {
            json c;
            if (!rgb(part, false, c)) return fail("That avatar looks wrong.");
            av[part] = c;
        }
        av["hat"] = a.contains("hat") && a["hat"].is_number_integer() ? std::clamp(a["hat"].get<long long>(), 0LL, 3LL) : 0LL;
        json hc;
        av["hatColor"] = rgb("hatColor", true, hc) ? hc : json::array({-1, -1, -1});
        json wearing = json::array();
        if (a.contains("wearing") && a["wearing"].is_array())
            for (const auto& w : a["wearing"])
                if (w.is_string() && me.owned.count(w.get<std::string>()) && wearing.size() < 12) wearing.push_back(w);
        av["wearing"] = wearing;
        av["updated"] = now;
        me.avatar = av;
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "thumb.set") {
        // A picture of a game (Studio sends one when publishing): a PNG or JPG, up to 400 KB.
        auto it = m_assets.find(str("id"));
        if (it == m_assets.end()) return fail("That doesn't exist (any more).");
        Asset& a = it->second;
        if (a.creator != me.id && !isStaff(me)) return fail("You can only change pictures of your own things.");
        std::string data;
        if (!Online::base64Decode(str("data"), data)) return fail("The picture got scrambled. Try again.");
        bool png = data.size() > 8 && data.compare(0, 4, "\x89PNG") == 0;
        bool jpg = data.size() > 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8;
        if (!png && !jpg) return fail("Pictures must be .png or .jpg.");
        if (data.size() > 400 * 1024) return fail("That picture is too big (the most is 400 KB).");
        if (!writeFile(m_opts.data / "files" / ("thumb-" + a.id), data)) return fail("The server couldn't save that picture.");
        a.thumb = now;
        saveAssets();
        json r = okay(); r["asset"] = publicAsset(a); return r;
    }
    if (name == "thumb.get") {
        auto it = m_assets.find(str("id"));
        std::string data;
        if (it == m_assets.end() || !it->second.thumb || !readFile(m_opts.data / "files" / ("thumb-" + it->first), data))
            return fail("No picture.");
        json r = okay(); r["data"] = Online::base64Encode(data); return r;
    }

    // --- Bolts --------------------------------------------------------------
    if (name == "bolts.history") {
        json h = json::array();
        size_t from = me.ledger.size() > 100 ? me.ledger.size() - 100 : 0;
        for (size_t i = from; i < me.ledger.size(); ++i)
            h.push_back({{"amount", me.ledger[i].amount}, {"reason", me.ledger[i].reason}, {"time", me.ledger[i].time}});
        json r = okay();
        r["history"] = h;
        r["me"] = meJson(me);
        return r;
    }
    if (name == "bolts.daily") {
        if (hasRef(me, "daily:" + today)) return fail("You already got today's Bolts. Come back tomorrow!");
        add(me, 25, "Daily reward", "daily:" + today);
        json r = okay(); r["me"] = meJson(me); r["got"] = 25; return r;
    }
    if (name == "bolts.play") {
        // The app asks every 5 minutes of play; the server checks the timing and the daily cap.
        if (now - me.lastPlay < 280) return fail("Keep playing!");
        if (me.playDay != today) { me.playDay = today; me.playEarned = 0; }
        me.lastPlay = now;
        if (me.playEarned >= 50) { saveUsers(); return fail("That's all the Bolts from playing for today."); }
        me.playEarned += 5;
        add(me, 5, "Playing games", "play:" + today);
        json r = okay(); r["me"] = meJson(me); r["got"] = 5; return r;
    }
    if (name == "bolts.redeem") {
        // BOLTS-<amount>-<nonce>-<signature>, made by the official account for this account.
        std::string code;
        for (char c : str("code")) if (!std::isspace((unsigned char)c)) code += c;
        size_t a = code.find('-'), b = a == std::string::npos ? a : code.find('-', a + 1),
               c = b == std::string::npos ? b : code.find('-', b + 1);
        if (code.rfind("BOLTS-", 0) != 0 || c == std::string::npos) return fail("That doesn't look like a Bolts code.");
        long long amount = 0;
        try { amount = std::stoll(code.substr(a + 1, b - a - 1)); } catch (...) {}
        std::string cn = code.substr(b + 1, c - b - 1);
        if (amount <= 0 || !Account::verify(Account::officialId(), "gb-bolts:" + std::to_string(amount) + ":" + me.id + ":" + cn,
                                            code.substr(c + 1)))
            return fail("That code isn't valid for your account.");
        if (hasRef(me, "code:" + cn)) return fail("You already used that code.");
        add(me, amount, "Bolts from Guts&Bolts staff", "code:" + cn);
        json r = okay(); r["me"] = meJson(me); r["got"] = amount; return r;
    }

    // --- Staff ---------------------------------------------------------------
    if (name.rfind("admin.", 0) == 0) {
        if (!isStaff(me)) return fail("Only staff can do that.");
        User* to = findUser(str("to"));
        // Staff can verify (or give Bolts to) someone who hasn't visited this server yet.
        if (!to && name != "admin.find" && isHex(lower(str("to")), 64, 64)) to = &user(lower(str("to")));
        if (name == "admin.find") {
            std::string q = lower(Online::cleanText(str("query"), 64));
            json list = json::array();
            for (const auto& [id, u] : m_users) {
                if (list.size() >= 40) break;
                if (q.empty() || lower(u.name).find(q) != std::string::npos || id.rfind(q, 0) == 0)
                    list.push_back(publicUser(u));
            }
            json r = okay(); r["users"] = list; return r;
        }
        if (!to) return fail("There's no account with that ID on this server.");
        if (name == "admin.grant") {
            // The signed badge comes from the app (made with the staff member's own key).
            std::string key = str("key"), sig = str("sig");
            if (!Online::grantValid(Account::officialId(), key, to->id, sig)) return fail("That badge signature isn't valid.");
            to->grants[key] = sig;
            saveUsers();
            log(me.name + " gave " + to->name + " the " + key + " badge");
            json r = okay(); r["user"] = publicUser(*to); return r;
        }
        if (name == "admin.revoke") {
            std::string key = str("key");
            if (key != "verified" && !isOfficial(me)) return fail("Only the official account can take that badge away.");
            to->grants.erase(key);
            saveUsers();
            log(me.name + " took the " + key + " badge from " + to->name);
            json r = okay(); r["user"] = publicUser(*to); return r;
        }
        if (!isOfficial(me)) return fail("Only the official Guts account can do that.");
        if (name == "admin.giveBolts") {
            long long amount = num("amount");
            if (amount == 0 || std::llabs(amount) > 10000000) return fail("Pick an amount between 1 and 10,000,000.");
            std::string why = Online::cleanText(str("reason"), 80);
            add(*to, amount, why.empty() ? (amount > 0 ? "Bolts from Guts&Bolts staff" : "Taken by staff") : why,
                "gift:" + Account::randomHex(6));
            log(me.name + " gave " + std::to_string(amount) + " Bolts to " + to->name);
            json r = okay(); r["user"] = publicUser(*to); r["bolts"] = balance(*to); return r;
        }
        if (name == "admin.ban") {
            if (isOfficial(*to)) return fail("You can't ban yourself.");
            const bool on = args.value("on", true);
            if (on) {
                std::string reason = str("reason");
                if (!Online::banReasonTitle(reason)) return fail("Pick a reason for the ban.");
                to->banReason = reason;
                to->banNote = Online::cleanText(str("note"), 200);
                to->bannedAt = Online::unixNow();
                const long long days = std::clamp(num("days"), 0LL, 3650LL);   // 0 = for good
                to->bannedUntil = days > 0 ? to->bannedAt + days * 86400 : 0;
            } else {
                to->banReason.clear();
                to->banNote.clear();
                to->bannedAt = to->bannedUntil = 0;
            }
            to->banned = on;
            log(me.name + (on ? " banned " + to->name + " (" + to->banReason + ")" : " unbanned " + to->name));
            saveUsers();
            json r = okay(); r["user"] = publicUser(*to); return r;
        }
        if (name == "admin.warn") {
            // A warning: they see it (with the reason) next time they open the app or the site.
            std::string reason = str("reason");
            if (!Online::banReasonTitle(reason)) return fail("Pick a reason for the warning.");
            if (!to->warnings.is_array()) to->warnings = json::array();
            to->warnings.push_back({{"id", Account::randomHex(4)}, {"reason", reason},
                                    {"note", Online::cleanText(str("note"), 200)}, {"at", Online::unixNow()}, {"seen", false}});
            while (to->warnings.size() > 30) to->warnings.erase(to->warnings.begin());
            log(me.name + " warned " + to->name + " (" + reason + ")");
            saveUsers();
            json r = okay(); r["user"] = publicUser(*to); return r;
        }
        return fail("Unknown staff request.");
    }

    // --- Uploads, the catalog and games -------------------------------------------------
    if (name == "upload") {
        std::string kind = str("kind");
        if (!Online::validKind(kind)) return fail("You can't upload that kind of thing.");
        std::string title = Online::cleanText(str("name"), 50);
        if (title.empty()) return fail("Give it a name.");
        std::string desc = Online::cleanText(str("description"), 1000, true);
        const bool verified = isVerified(me);
        if (Online::isAccessory(kind) && !verified && !isStaff(me))
            return fail("Only Verified creators can make hats and accessories. Shirts and pants are open to everyone!");
        long long price = std::clamp(num("price"), 0LL, 1000000LL);
        if (kind == "game" || Online::alwaysFree(kind)) price = 0;   // games, decals and audio are free
        if (price > 0 && !verified) return fail("Only Verified creators can sell things. Upload it for free, or get Verified!");
        if (!verified) {
            if (me.uploadDay != today) { me.uploadDay = today; me.uploadsToday = 0; }
            if (me.uploadsToday >= Online::kDailyUploadsUnverified)
                return fail("You've uploaded " + std::to_string(Online::kDailyUploadsUnverified) +
                            " things today. Come back tomorrow (Verified creators have no limit).");
        }
        std::string data;
        if (!Online::base64Decode(str("data"), data)) return fail("The upload got scrambled. Try again.");
        if (data.size() > Online::maxSize(kind))
            return fail("That's too big (the most is " + std::to_string(Online::maxSize(kind) / 1024) + " KB).");
        json meta = args.contains("meta") && args["meta"].is_object() ? args["meta"] : json::object();
        if (meta.dump().size() > 4096) return fail("Too much extra information.");
        meta.erase("image");
        meta.erase("model");
        if (Online::isAccessory(kind) && !data.empty()) {   // made in Studio's Accessory window
            json acc = json::parse(data, nullptr, false);
            if (!acc.is_object() || acc.value("format", std::string()) != "gbaccessory")
                return fail("That isn't a Guts&Bolts accessory. Make it in Studio's Accessory window.");
            meta["model"] = true;
        }
        if (kind == "face") {
            if (!isOfficial(me)) return fail("Only Guts can make faces.");
            if (data.size() < 8 || data.compare(1, 3, "PNG") != 0) return fail("Faces must be .png pictures.");
            meta["image"] = true;
            meta["ext"] = "png";
        }
        if (kind == "tshirt") {   // any picture, worn flat on the front of the torso
            bool png = data.size() > 24 && data.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0;
            bool jpg = data.size() > 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8 &&
                       (unsigned char)data[2] == 0xFF;
            if (!png && !jpg) return fail("T-shirts must be .png or .jpg pictures.");
            if (png) {
                auto u = [&](size_t i) { return (uint32_t)(unsigned char)data[i]; };
                uint32_t w = u(16) << 24 | u(17) << 16 | u(18) << 8 | u(19), h = u(20) << 24 | u(21) << 16 | u(22) << 8 | u(23);
                if (w > 1024 || h > 1024) return fail("T-shirt pictures can be at most 1024 x 1024.");
            }
            meta["image"] = true;
            meta["ext"] = png ? "png" : "jpg";
        }
        if ((kind == "shirt" || kind == "pants") && !data.empty()) {   // a clothing template picture
            auto u = [&](size_t i) { return (uint32_t)(unsigned char)data[i]; };
            if (data.size() < 24 || data.compare(1, 3, "PNG") != 0)
                return fail("Clothing pictures must be .png files made from the template.");
            uint32_t w = u(16) << 24 | u(17) << 16 | u(18) << 8 | u(19), h = u(20) << 24 | u(21) << 16 | u(22) << 8 | u(23);
            if (w != 585 || h != 559) return fail("Clothing pictures must be 585 x 559 (the template's size).");
            meta["image"] = true;
            meta["ext"] = "png";
        }
        if (kind == "audio") {
            std::string ext = lower(meta.value("ext", std::string()));
            if (ext != "mp3" && ext != "wav" && ext != "ogg" && ext != "flac") return fail("Audio must be .mp3, .wav, .ogg or .flac.");
            if (data.empty()) return fail("That audio file is empty.");
        }
        if (kind == "decal") {
            // Only real pictures: check the first bytes, not just the name.
            bool png = data.size() > 8 && data.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0;
            bool jpg = data.size() > 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8 &&
                       (unsigned char)data[2] == 0xFF;
            if (!png && !jpg) return fail("Decals must be .png or .jpg pictures.");
            meta["ext"] = png ? "png" : "jpg";
        }
        if (kind == "game" && !json::accept(data)) return fail("That isn't a Guts&Bolts game file.");
        if (kind == "plugin" && data.empty()) return fail("That plugin is empty.");
        const long long fee = verified ? 0 : Online::uploadFee(kind);
        if (fee > 0 && balance(me) < fee)
            return fail("Uploading costs " + std::to_string(fee) + " Bolts, and you have " + std::to_string(balance(me)) +
                        ". (It's free for Verified creators.)");

        Asset a;
        a.id = kind + "-" + Account::randomHex(5);
        a.kind = kind; a.name = title; a.description = desc; a.creator = me.id;
        a.price = price; a.created = now; a.size = data.size(); a.meta = meta;
        if (!writeFile(blobPath(a.id), data)) return fail("The server couldn't save that file.");
        m_assets[a.id] = a;
        saveAssets();
        me.owned.insert(a.id);
        if (!verified) me.uploadsToday++;
        if (fee > 0) add(me, -fee, "Upload fee: " + title, "upload:" + a.id);
        else saveUsers();
        log(me.name + " uploaded " + kind + " \"" + title + "\" (" + std::to_string(data.size()) + " bytes)");
        json r = okay(); r["asset"] = publicAsset(a); r["me"] = meJson(me); r["fee"] = fee; return r;
    }
    // --- Game badges: creators make them on their game's page, game scripts award them.
    if (name == "gamebadge.create" || name == "gamebadge.delete") {
        auto it = m_assets.find(str("game"));
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        Asset& g = it->second;
        if (g.creator != me.id) return fail("Only the game's creator can change its badges.");
        if (name == "gamebadge.delete") {
            json keep = json::array();
            for (const json& b : g.badges) if (b.value("id", std::string()) != str("badge")) keep.push_back(b);
            g.badges = keep;
            saveAssets();
            json r = okay(); r["badges"] = g.badges; return r;
        }
        if (g.badges.size() >= 30) return fail("A game can have up to 30 badges.");
        std::string title = Online::cleanText(str("name"), 40);
        if (title.empty()) return fail("Give the badge a name.");
        json col = json::array({240, 180, 40});
        if (args.contains("color") && args["color"].is_array() && args["color"].size() == 3) {
            col = json::array();
            for (const auto& v : args["color"]) col.push_back(std::clamp(v.is_number() ? v.get<int>() : 0, 0, 255));
        }
        json b = {{"id", "badge-" + Account::randomHex(5)}, {"name", title},
                  {"description", Online::cleanText(str("description"), 300, true)}, {"color", col},
                  {"created", Online::unixNow()}, {"awarded", 0}};
        g.badges.push_back(b);
        saveAssets();
        json r = okay(); r["badge"] = b; r["badges"] = g.badges; return r;
    }
    if (name == "gamebadge.award") {
        // Only the host of a live server of the badge's game, for someone in that server.
        const std::string bid = str("badge");
        Asset* g = nullptr;
        json* b = nullptr;
        for (auto& [id, a] : m_assets) {
            if (a.kind != "game") continue;
            for (json& x : a.badges) if (x.value("id", std::string()) == bid) { g = &a; b = &x; }
            if (g) break;
        }
        if (!b) return fail("There's no badge with that ID.");
        const Session* session = nullptr;
        for (const auto& [sid, s] : m_sessions) if (s.host == me.id && s.game == g->id) session = &s;
        if (!session) return fail("Badges can only be given in an online server of " + g->name + ".");
        auto lower = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
        const std::string who = lower(str("to"));
        User* to = lower(me.name) == who ? &me : nullptr;
        for (const Client* p : session->players) {
            auto u = m_users.find(p->account);
            if (!to && u != m_users.end() && lower(u->second.name) == who) to = &u->second;
        }
        if (!to) return fail("That player isn't in this server.");
        if (to->userId == 0) return fail("Guests can't earn badges. Sign up to collect them!");
        for (const json& e : to->gameBadges)
            if (e.is_array() && !e.empty() && e[0] == bid) { json r = okay(); r["already"] = true; r["name"] = (*b)["name"]; return r; }
        to->gameBadges.push_back(json::array({bid, g->id, Online::unixNow()}));
        (*b)["awarded"] = b->value("awarded", 0) + 1;
        saveUsers();
        saveAssets();
        json r = okay(); r["awarded"] = true; r["name"] = (*b)["name"]; r["player"] = to->name; return r;
    }
    if (name == "update") {
        // The creator replaces their upload (a new version of a game or plugin).
        auto it = m_assets.find(str("id"));
        if (it == m_assets.end()) return fail("That doesn't exist (any more).");
        Asset& a = it->second;
        if (a.creator != me.id) return fail("You can only update your own things.");
        std::string data;
        if (!Online::base64Decode(str("data"), data)) return fail("The upload got scrambled. Try again.");
        if (data.size() > Online::maxSize(a.kind)) return fail("That's too big.");
        if (a.kind == "game" && !json::accept(data)) return fail("That isn't a Guts&Bolts game file.");
        if (!Online::isClothing(a.kind) && !writeFile(blobPath(a.id), data)) return fail("The server couldn't save that file.");
        std::string title = Online::cleanText(str("name"), 50);
        if (!title.empty()) a.name = title;
        if (args.contains("description")) a.description = Online::cleanText(str("description"), 1000, true);
        if (args.contains("price") && a.kind != "game" && !Online::alwaysFree(a.kind)) {
            long long price = std::clamp(num("price"), 0LL, 1000000LL);
            if (price > 0 && !isVerified(me)) return fail("Only Verified creators can sell things.");
            a.price = price;
        }
        if (!Online::isClothing(a.kind)) a.size = data.size();
        saveAssets();
        log(me.name + " updated " + a.kind + " \"" + a.name + "\"");
        json r = okay(); r["asset"] = publicAsset(a); return r;
    }
    if (name == "list") {
        std::string kind = str("kind"), q = lower(Online::cleanText(str("query"), 64)), creator = lower(str("creator"));
        std::string sort = str("sort");
        const bool ownedOnly = args.value("owned", false);   // your inventory (the Avatar page)
        std::vector<const Asset*> found;
        for (const auto& [id, a] : m_assets) {
            if (!kind.empty() && a.kind != kind && !(kind == "clothing" && Online::isClothing(a.kind))) continue;
            if (ownedOnly && !me.owned.count(a.id)) continue;
            if (!creator.empty() && a.creator != creator) continue;
            if (!q.empty() && lower(a.name).find(q) == std::string::npos) continue;
            found.push_back(&a);
        }
        std::sort(found.begin(), found.end(), [&](const Asset* x, const Asset* y) {
            if (sort == "popular") return x->plays + x->sales > y->plays + y->sales;
            return x->created > y->created;   // newest first
        });
        long long offset = std::max(0LL, num("offset")), limit = std::clamp(num("limit"), 1LL, 100LL);
        if (!args.contains("limit")) limit = 60;
        json list = json::array();
        for (size_t i = (size_t)offset; i < found.size() && (long long)list.size() < limit; ++i)
            list.push_back(publicAsset(*found[i]));
        json r = okay(); r["assets"] = list; r["total"] = found.size(); return r;
    }
    if (name == "get") {
        auto it = m_assets.find(str("id"));
        if (it == m_assets.end()) return fail("That doesn't exist (any more).");
        Asset& a = it->second;
        const bool mine = me.owned.count(a.id) || a.creator == me.id;
        if (a.price > 0 && !mine && !isStaff(me) && (a.kind == "plugin" || a.kind == "audio"))
            return fail("Buy it first.");
        std::string data;
        if (!readFile(blobPath(a.id), data)) return fail("The server lost that file.");
        if (a.kind == "game" && a.creator != me.id) { a.plays++; saveAssets(); }   // creators opening their own game don't count
        json r = okay(); r["asset"] = publicAsset(a); r["data"] = Online::base64Encode(data); return r;
    }
    if (name == "buy") {
        auto it = m_assets.find(str("id"));
        if (it == m_assets.end()) return fail("That doesn't exist (any more).");
        Asset& a = it->second;
        if (me.owned.count(a.id)) { json r = okay(); r["me"] = meJson(me); r["already"] = true; return r; }
        if (a.price > 0) {
            if (balance(me) < a.price)
                return fail("You need " + std::to_string(a.price - balance(me)) + " more Bolts for that.");
            add(me, -a.price, "Bought " + a.name, "buy:" + a.id);
            if (User* seller = findUser(a.creator); seller && seller != &me) {
                long long share = a.price * Online::kCreatorSharePercent / 100;
                if (share > 0) add(*seller, share, "Sold " + a.name, "sale:" + a.id + ":" + Account::randomHex(4));
            }
        }
        a.sales++;
        me.owned.insert(a.id);
        saveAssets();
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "delete") {
        auto it = m_assets.find(str("id"));
        if (it == m_assets.end()) return fail("That doesn't exist (any more).");
        if (it->second.creator != me.id && !isStaff(me)) return fail("You can only delete your own things.");
        std::error_code ec;
        fs::remove(blobPath(it->first), ec);
        fs::remove(m_opts.data / "files" / ("thumb-" + it->first), ec);
        log(me.name + " deleted " + it->second.kind + " \"" + it->second.name + "\"");
        m_assets.erase(it);
        saveAssets();
        return okay();
    }
    if (name == "stats") {
        json r = okay();
        r["users"] = m_users.size();
        r["assets"] = m_assets.size();
        r["name"] = m_opts.name;
        return r;
    }
    if (name == "updates.list") {   // the website's update log lives on the main (Cloudflare) server
        json r = okay();
        r["updates"] = json::array();
        r["latest"] = "";
        return r;
    }
    return fail("The server doesn't know how to do \"" + name + "\". It might need updating.");
}

// ---------------------------------------------------------------------------
// Saving and loading
// ---------------------------------------------------------------------------

fs::path GbServer::blobPath(const std::string& assetId) const { return m_opts.data / "files" / assetId; }

void GbServer::saveUsers() {
    json all = json::object();
    for (const auto& [id, u] : m_users) {
        json ledger = json::array();
        for (const Entry& e : u.ledger) ledger.push_back({e.amount, e.reason, e.time, e.ref});
        all[id] = {{"name", u.name}, {"created", u.created}, {"lastSeen", u.lastSeen}, {"ledger", ledger},
                   {"grants", u.grants}, {"owned", u.owned}, {"uploadDay", u.uploadDay}, {"uploadsToday", u.uploadsToday},
                   {"playDay", u.playDay}, {"playEarned", u.playEarned}, {"lastPlay", u.lastPlay}, {"banned", u.banned},
                   {"banReason", u.banReason}, {"banNote", u.banNote}, {"bannedAt", u.bannedAt},
                   {"bannedUntil", u.bannedUntil}, {"warnings", u.warnings},
                   {"friends", u.friends}, {"friendIn", u.friendIn}, {"friendOut", u.friendOut},
                   {"username", u.username}, {"userId", u.userId}, {"pwSalt", u.pwSalt}, {"pwHash", u.pwHash},
                   {"keyBlob", u.keyBlob}, {"avatar", u.avatar}, {"gameBadges", u.gameBadges}};
    }
    writeFile(m_opts.data / "accounts.json", all.dump(1));
}

void GbServer::saveAssets() {
    json all = json::object();
    for (const auto& [id, a] : m_assets)
        all[id] = {{"kind", a.kind}, {"name", a.name}, {"description", a.description}, {"creator", a.creator},
                   {"price", a.price}, {"created", a.created}, {"sales", a.sales}, {"plays", a.plays},
                   {"size", a.size}, {"meta", a.meta}, {"thumb", a.thumb}, {"badges", a.badges}};
    writeFile(m_opts.data / "assets.json", all.dump(1));
}

// The example games that come with Guts&Bolts (the games folder): always on the
// server as the staff account's games, kept up to date (the website's server does the same).
void GbServer::addExampleGames() {
    if (Account::officialId().empty()) return;
    std::error_code ec;
    static const std::pair<const char*, const char*> kGenres[] = {
        {"Demolition Yard", "Destruction"}, {"Mega Water Slide", "Adventure"}, {"Night Plaza", "Showcase"}, {"Obby of Doom", "Obby"}};
    bool changed = false;
    for (const auto& [title, genre] : kGenres) {
        fs::path file = Paths::gamesFolder() / (std::string(title) + ".gbscene");
        std::string text;
        if (!readFile(file, text)) continue;
        json j = json::parse(text, nullptr, false);
        if (!j.is_object()) continue;
        std::string id = "game-";
        for (char c : std::string(title)) id += c == ' ' ? '-' : (char)std::tolower((unsigned char)c);
        Asset& a = m_assets[id];
        if (a.id.empty()) {
            a.id = id; a.kind = "game"; a.creator = Account::officialId(); a.created = Online::unixNow();
            a.meta = {{"builtin", true}, {"genres", json::array({genre})}, {"access", "public"}};
        }
        const json& info = j.contains("info") ? j["info"] : json::object();
        a.name = Online::cleanText(info.value("title", std::string(title)), 50);
        a.description = Online::cleanText(info.value("description", std::string()), 1000);
        if (a.size != text.size()) { a.size = text.size(); writeFile(blobPath(id), text); }
        changed = true;
    }
    if (changed) saveAssets();
}

void GbServer::load() {
    std::string text;
    if (readFile(m_opts.data / "accounts.json", text)) {
        json all = json::parse(text, nullptr, false);
        if (all.is_object())
            for (auto& [id, j] : all.items()) {
                User u;
                u.id = id;
                u.name = j.value("name", std::string("Player"));
                u.created = j.value("created", 0LL);
                u.lastSeen = j.value("lastSeen", 0LL);
                if (j.contains("ledger"))
                    for (const auto& e : j["ledger"])
                        if (e.is_array() && e.size() == 4)
                            u.ledger.push_back({e[0].get<long long>(), e[1].get<std::string>(), e[2].get<long long>(),
                                                e[3].get<std::string>()});
                if (j.contains("grants") && j["grants"].is_object())
                    for (auto& [k, v] : j["grants"].items()) if (v.is_string()) u.grants[k] = v.get<std::string>();
                if (j.contains("owned")) for (const auto& o : j["owned"]) if (o.is_string()) u.owned.insert(o.get<std::string>());
                u.uploadDay = j.value("uploadDay", std::string());
                u.uploadsToday = j.value("uploadsToday", 0);
                u.playDay = j.value("playDay", std::string());
                u.playEarned = j.value("playEarned", 0LL);
                u.lastPlay = j.value("lastPlay", 0LL);
                u.banned = j.value("banned", false);
                u.banReason = j.value("banReason", std::string());
                u.banNote = j.value("banNote", std::string());
                u.bannedAt = j.value("bannedAt", 0LL);
                u.bannedUntil = j.value("bannedUntil", 0LL);
                if (j.contains("warnings") && j["warnings"].is_array()) u.warnings = j["warnings"];
                u.username = j.value("username", std::string());
                u.userId = j.value("userId", 0LL);
                u.pwSalt = j.value("pwSalt", std::string());
                u.pwHash = j.value("pwHash", std::string());
                u.keyBlob = j.value("keyBlob", std::string());
                if (j.contains("avatar") && j["avatar"].is_object()) u.avatar = j["avatar"];
                if (j.contains("gameBadges") && j["gameBadges"].is_array()) u.gameBadges = j["gameBadges"];
                for (const char* k : {"friends", "friendIn", "friendOut"}) {
                    std::set<std::string>& set = std::string(k) == "friends" ? u.friends : std::string(k) == "friendIn" ? u.friendIn : u.friendOut;
                    if (j.contains(k) && j[k].is_array())
                        for (const auto& f : j[k]) if (f.is_string()) set.insert(f.get<std::string>());
                }
                m_users[id] = std::move(u);
            }
    }
    if (readFile(m_opts.data / "assets.json", text)) {
        json all = json::parse(text, nullptr, false);
        if (all.is_object())
            for (auto& [id, j] : all.items()) {
                Asset a;
                a.id = id;
                a.kind = j.value("kind", std::string());
                a.name = j.value("name", std::string());
                a.description = j.value("description", std::string());
                a.creator = j.value("creator", std::string());
                a.price = Online::alwaysFree(a.kind) ? 0LL : j.value("price", 0LL);
                a.created = j.value("created", 0LL);
                a.sales = j.value("sales", 0LL);
                a.plays = j.value("plays", 0LL);
                a.size = j.value("size", (size_t)0);
                if (j.contains("meta")) a.meta = j["meta"];
                if (j.contains("badges") && j["badges"].is_array()) a.badges = j["badges"];
                a.thumb = j.value("thumb", 0LL);
                if (Online::validKind(a.kind)) m_assets[id] = a;
            }
    }
    loadGroups();
    loadIds();
    log("loaded " + std::to_string(m_users.size()) + " accounts, " + std::to_string(m_assets.size()) + " uploads and " +
        std::to_string(m_groups.size()) + " groups from " + m_opts.data.string());
}
