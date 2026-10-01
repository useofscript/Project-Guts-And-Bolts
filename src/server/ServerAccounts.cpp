// Signing up and logging in.
//
// An account is still a key pair (it signs every request), but now it can
// also have a username and a user number:
//   - user numbers count up (1, 2, 3...) and are never given out twice;
//   - usernames can never be used again, even after a ban;
//   - user 1 is "Guts", the official staff account.
//
// To log in on another device, the app keeps a copy of your key on the
// server, locked with a key made from your password (see Account::passwordKeys).
// We only ever see a login token made from the password, never the password,
// and can't unlock the copy ourselves.
#include <cctype>
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr int       kMaxWrongPasswords = 5;     // then wait...
constexpr long long kLockoutSeconds    = 600;   // ...10 minutes
} // namespace

void GbServer::claimOfficial(User& u) {
    if (!isOfficial(u) || u.userId == 1) return;
    u.userId = 1;
    u.username = Account::kStaffName;
    m_takenNames.insert(lower(u.username));
}

void GbServer::gutsFollows(User& u) {   // (worker/server.js gutsFollows)
    User* g = findUserId(1);
    if (!g || u.userId <= 1 || g->id == u.id) return;
    if (g->following.count(u.id) && u.followers.count(g->id)) return;
    g->following.insert(u.id);
    u.followers.insert(g->id);
    saveUsers();
}

// The user number (5 or "#5") is the main way to name someone; long account keys,
// "@username" and names work too. (worker/server.js findPerson)
GbServer::User* GbServer::findPerson(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    {
        std::string digits = !s.empty() && s[0] == '#' ? s.substr(1) : s;
        if (!digits.empty() && digits.size() <= 11 && std::all_of(digits.begin(), digits.end(), ::isdigit))
            return findUserId(std::atoll(digits.c_str()));
    }
    if (User* u = findUser(s)) return u;
    if (User* u = findUsername(!s.empty() && s[0] == '@' ? s.substr(1) : s)) return u;
    std::string want = s;
    for (char& c : want) c = (char)std::tolower((unsigned char)c);
    for (auto& [id, u] : m_users) {
        if (u.userId <= 0) continue;
        std::string n = u.name;
        for (char& c : n) c = (char)std::tolower((unsigned char)c);
        if (n == want) return &u;
    }
    return nullptr;
}

GbServer::User* GbServer::findUsername(const std::string& username) {
    std::string want = lower(username);
    for (auto& [id, u] : m_users) if (u.userId > 0 && lower(u.username) == want) return &u;
    return nullptr;
}

GbServer::User* GbServer::findUserId(long long userId) {
    if (userId <= 0) return nullptr;
    for (auto& [id, u] : m_users) if (u.userId == userId) return &u;
    return nullptr;
}

void GbServer::saveIds() {
    writeFile(m_opts.data / "ids.json", json{{"next", m_nextUserId}, {"taken", m_takenNames}}.dump(1));
}

void GbServer::loadIds() {
    std::string text;
    if (readFile(m_opts.data / "ids.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object()) {
            m_nextUserId = std::max<long long>(2, j.value("next", 2LL));
            if (j.contains("taken"))
                for (const auto& n : j["taken"]) if (n.is_string()) m_takenNames.insert(n.get<std::string>());
        }
    }
    // Whatever the file says, never hand out a number or name that's in use.
    for (auto& [id, u] : m_users) {
        claimOfficial(u);
        if (u.userId > 0) {
            m_nextUserId = std::max(m_nextUserId, u.userId + 1);
            m_takenNames.insert(lower(u.username));
        }
    }
    m_takenNames.insert("guts");
    saveIds();
    for (auto& [id, u] : m_users) gutsFollows(u);
}

json GbServer::accountOp(const std::string& name, User& me, const json& args) {
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    std::string username = Online::cleanText(str("username"), 30);

    if (name == "account.ackWarning") {   // "I understand" on a staff warning
        std::string id = str("id");
        if (me.warnings.is_array())
            for (json& w : me.warnings)
                if (w.is_object() && w.value("id", std::string()) == id) {
                    w["seen"] = true;
                    saveUsers();
                    json r = okay(); r["me"] = meJson(me); return r;
                }
        return fail("That warning is gone.");
    }
    if (name == "account.check") {   // is this username free? (for the sign-up page, as you type)
        json r = okay();
        std::string problem = Online::usernameProblem(username, isOfficial(me));
        if (problem.empty() && m_takenNames.count(lower(username)) && !(isOfficial(me) && lower(username) == "guts"))
            problem = "That username is taken.";
        r["available"] = problem.empty();
        r["problem"] = problem;
        return r;
    }

    if (name == "account.signup") {
        std::string salt = lower(str("salt")), auth = lower(str("auth")), blob = lower(str("key"));
        if (!isHex(salt, 32, 32) || !isHex(auth, 64, 64) || !isHex(blob, 208, 208))
            return fail("The sign-up form was missing something. Update the app and try again.");
        bool official = isOfficial(me);
        if (me.userId > 0 && !(official && me.keyBlob.empty()))
            return fail("This device is already signed up as " + me.username + ".");
        if (official) {
            // Guts just sets a password (the name and number are already theirs).
            if (lower(username) != "guts") return fail("The staff account's username is Guts.");
            me.name = me.username;
        } else {
            if (std::string problem = Online::usernameProblem(username); !problem.empty()) return fail(problem);
            if (m_takenNames.count(lower(username))) return fail("That username is taken. Try another one.");
            me.userId = m_nextUserId++;
            me.username = username;
            m_takenNames.insert(lower(username));
            me.name = username;
            gutsFollows(me);
        }
        me.pwSalt = salt;
        me.pwHash = Account::hashHex(auth);
        me.keyBlob = blob;
        saveIds();
        if (!hasRef(me, "welcome")) add(me, 100, "Welcome to Guts&Bolts!", "welcome");   // (also saves)
        else saveUsers();
        log("signed up: #" + std::to_string(me.userId) + " " + me.username);
        json r = okay();
        r["me"] = meJson(me);
        return r;
    }

    // Logging in happens from a new device, so `me` is that device's own
    // (empty) account; the real one is found by its username.
    User* u = findUsername(username);
    // An account made on a device that never set a password can't be logged into anywhere else yet.
    auto noLogin = [](const User* acc) -> std::string {
        if (!acc) return "There's no account with that username.";
        return "That account hasn't set a password yet, so it only works on the device it was made on. On that device, open the "
               "Guts&Bolts Player, go to Avatar > Your account and press \"Set a password\". Then you can log in here.";
    };
    if (name == "account.salt") {
        if (!u || u->keyBlob.empty()) return fail(noLogin(u));
        json r = okay();
        r["salt"] = u->pwSalt;
        return r;
    }
    if (name == "account.login") {
        if (!u || u->keyBlob.empty()) return fail(noLogin(u));
        if (u->banned) return fail(Online::banMessage(u->banReason, u->banNote));
        long long now = Online::unixNow();
        auto& fails = m_failedLogins[lower(username)];
        fails.erase(std::remove_if(fails.begin(), fails.end(), [&](long long t) { return now - t > kLockoutSeconds; }), fails.end());
        if ((int)fails.size() >= kMaxWrongPasswords)
            return fail("Too many wrong passwords. Wait 10 minutes and try again.");
        std::string auth = lower(str("auth"));
        if (!isHex(auth, 64, 64) || Account::hashHex(auth) != u->pwHash) {
            fails.push_back(now);
            return fail("Wrong password.");
        }
        fails.clear();
        log("log in: #" + std::to_string(u->userId) + " " + u->username + " on a new device");
        json r = okay();
        r["account"] = u->id;
        r["key"] = u->keyBlob;
        r["username"] = u->username;
        r["name"] = u->name;
        return r;
    }
    return fail("Unknown request.");
}
