// Groups (communities) on the Guts&Bolts server: members with roles, a shout,
// a wall people can post on, and join requests for closed groups.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr long long kGroupFee      = 50;   // making a group (free for Verified people)
constexpr size_t    kMaxOwned      = 5;
constexpr size_t    kMaxJoined     = 50;
constexpr size_t    kWallSize      = 200;
constexpr long long kPostCooldown  = 10;   // seconds between wall posts
} // namespace

json GbServer::badgesOf(const User& u) const {
    json b = json::array();
    if (isOfficial(u)) b.push_back("admin");
    for (const auto& [key, sig] : u.grants)
        if (Online::grantValid(Account::officialId(), key, u.id, sig)) b.push_back(key);
    return b;
}

std::vector<const GbServer::Group*> GbServer::groupsOf(const std::string& userId) const {
    std::vector<const Group*> out;
    for (const auto& [id, g] : m_groups) if (g.members.count(userId)) out.push_back(&g);
    return out;
}

json GbServer::publicGroup(const Group& g) const {
    auto it = m_users.find(g.owner);
    return {{"id", g.id}, {"name", g.name}, {"description", g.description}, {"owner", g.owner},
            {"ownerName", it != m_users.end() ? it->second.name : "?"},
            {"ownerVerified", it != m_users.end() && isVerified(it->second)},
            {"members", g.members.size()}, {"color", g.color}, {"open", g.open}, {"created", g.created},
            {"shout", g.shout.text}};
}

json GbServer::groupOp(const std::string& name, User& me, const json& args) {
    const long long now = Online::unixNow();
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto userJson = [&](const std::string& id) {
        auto it = m_users.find(id);
        return json{{"id", id}, {"name", it != m_users.end() ? it->second.name : "?"},
                    {"verified", it != m_users.end() && isVerified(it->second)}};
    };

    if (name == "groups.list") {
        std::string q = lower(Online::cleanText(str("query"), 64));
        std::vector<const Group*> found;
        for (const auto& [id, g] : m_groups)
            if (q.empty() || lower(g.name).find(q) != std::string::npos) found.push_back(&g);
        std::sort(found.begin(), found.end(), [](const Group* a, const Group* b) {
            return a->members.size() != b->members.size() ? a->members.size() > b->members.size() : a->created > b->created;
        });
        json list = json::array();
        for (const Group* g : found) { if (list.size() >= 60) break; list.push_back(publicGroup(*g)); }
        json r = okay(); r["groups"] = list; return r;
    }
    if (name == "groups.mine") {
        json list = json::array();
        for (const Group* g : groupsOf(me.id)) { json pg = publicGroup(*g); pg["role"] = g->members.at(me.id); list.push_back(pg); }
        json r = okay(); r["groups"] = list; return r;
    }
    if (name == "groups.create") {
        std::string title = say(str("name"), 40);
        if (title.size() < 3) return fail("Group names need at least 3 letters.");
        if (Account::nameIsReserved(title) && !isOfficial(me)) return fail("That name belongs to Guts&Bolts.");
        for (const auto& [id, g] : m_groups)
            if (lower(g.name) == lower(title)) return fail("There's already a group called that.");
        size_t owned = 0;
        for (const auto& [id, g] : m_groups) if (g.owner == me.id) ++owned;
        if (owned >= kMaxOwned) return fail("You already own " + std::to_string(kMaxOwned) + " groups.");
        if (groupsOf(me.id).size() >= kMaxJoined) return fail("You're in too many groups. Leave one first.");
        const long long fee = isVerified(me) ? 0 : kGroupFee;
        if (balance(me) < fee)
            return fail("Making a group costs " + std::to_string(fee) + " Bolts (free for Verified people).");
        Group g;
        g.id = "g-" + Account::randomHex(5);
        g.name = title;
        g.description = say(str("description"), 1000, true);
        g.owner = me.id;
        g.created = now;
        g.color = (int)std::clamp(args.value("color", 0x3A7BD5LL), 0LL, 0xFFFFFFLL);
        g.open = args.value("open", true);
        g.members[me.id] = "Owner";
        m_groups[g.id] = g;
        if (fee > 0) add(me, -fee, "Made the group " + title, "group:" + g.id);
        saveGroups();
        log(me.name + " made the group \"" + title + "\"");
        json r = okay(); r["group"] = publicGroup(g); r["me"] = meJson(me); return r;
    }

    // Everything else is about one group.
    auto it = m_groups.find(str("id"));
    if (it == m_groups.end()) return fail("That group doesn't exist (any more).");
    Group& g = it->second;
    auto roleOf = [&](const std::string& id) {
        auto m = g.members.find(id);
        return m == g.members.end() ? std::string() : m->second;
    };
    const std::string myRole = roleOf(me.id);
    const bool canManage = myRole == "Owner" || myRole == "Admin";

    if (name == "groups.get") {
        json r = okay();
        r["group"] = publicGroup(g);
        r["myRole"] = myRole;
        r["requested"] = g.requests.count(me.id) > 0;
        json members = json::array();
        std::vector<std::pair<std::string, std::string>> sorted(g.members.begin(), g.members.end());
        auto rank = [](const std::string& role) { return role == "Owner" ? 0 : role == "Admin" ? 1 : 2; };
        std::stable_sort(sorted.begin(), sorted.end(), [&](const auto& a, const auto& b) { return rank(a.second) < rank(b.second); });
        for (const auto& [id, role] : sorted) {
            if (members.size() >= 500) break;
            json u = userJson(id);
            u["role"] = role;
            members.push_back(u);
        }
        r["memberList"] = members;
        json shout = json::object();
        if (!g.shout.text.empty()) { shout = userJson(g.shout.by); shout["text"] = g.shout.text; shout["time"] = g.shout.time; }
        r["shoutInfo"] = shout;
        json wall = json::array();
        size_t from = g.wall.size() > 60 ? g.wall.size() - 60 : 0;
        for (size_t i = from; i < g.wall.size(); ++i) {
            json p = userJson(g.wall[i].by);
            p["text"] = g.wall[i].text;
            p["time"] = g.wall[i].time;
            wall.push_back(p);
        }
        r["wall"] = wall;
        if (canManage) {
            json reqs = json::array();
            for (const auto& id : g.requests) reqs.push_back(userJson(id));
            r["requests"] = reqs;
        }
        return r;
    }
    if (name == "groups.join") {
        if (!myRole.empty()) return okay();
        if (groupsOf(me.id).size() >= kMaxJoined) return fail("You're in too many groups. Leave one first.");
        if (!g.open) {
            g.requests.insert(me.id);
            saveGroups();
            json r = okay(); r["requested"] = true; return r;
        }
        g.members[me.id] = "Member";
        saveGroups();
        return okay();
    }
    if (name == "groups.leave") {
        if (myRole == "Owner") return fail("Owners can't leave. Give the group to someone else first, or delete it.");
        g.members.erase(me.id);
        g.requests.erase(me.id);
        saveGroups();
        return okay();
    }
    if (name == "groups.post") {
        if (myRole.empty()) return fail("Join the group to post on its wall.");
        std::string text = say(str("text"), 300, true);
        if (text.empty()) return fail("Write something first.");
        if (now - m_lastPost[me.id] < kPostCooldown) return fail("Slow down a little - wait a few seconds between posts.");
        m_lastPost[me.id] = now;
        g.wall.push_back({me.id, text, now});
        if (g.wall.size() > kWallSize) g.wall.erase(g.wall.begin());
        saveGroups();
        return okay();
    }
    if (name == "groups.deletePost") {
        long long t = args.value("time", 0LL);
        std::string by = str("by");
        auto p = std::find_if(g.wall.begin(), g.wall.end(), [&](const Post& x) { return x.time == t && x.by == by; });
        if (p == g.wall.end()) return fail("That post is already gone.");
        if (p->by != me.id && !canManage && !isStaff(me)) return fail("You can only delete your own posts.");
        g.wall.erase(p);
        saveGroups();
        return okay();
    }
    if (name == "groups.shout") {
        if (!canManage) return fail("Only the group's owner and admins can shout.");
        g.shout = {me.id, say(str("text"), 200), now};
        saveGroups();
        return okay();
    }
    if (name == "groups.edit") {
        if (!canManage) return fail("Only the group's owner and admins can change it.");
        if (args.contains("description")) g.description = say(str("description"), 1000, true);
        if (args.contains("color")) g.color = (int)std::clamp(args.value("color", 0LL), 0LL, 0xFFFFFFLL);
        if (args.contains("open")) {
            g.open = args.value("open", true);
            if (g.open) {   // opening up lets everyone who asked in
                for (const auto& id : g.requests) g.members.emplace(id, "Member");
                g.requests.clear();
            }
        }
        saveGroups();
        json r = okay(); r["group"] = publicGroup(g); return r;
    }
    if (name == "groups.request") {
        if (!canManage) return fail("Only the group's owner and admins can let people in.");
        std::string who = lower(str("user"));
        if (!g.requests.erase(who)) return fail("They're not waiting any more.");
        if (args.value("accept", false)) g.members[who] = "Member";
        saveGroups();
        return okay();
    }
    if (name == "groups.member") {
        std::string who = lower(str("user")), action = str("action");
        std::string theirRole = roleOf(who);
        if (theirRole.empty()) return fail("They're not in this group.");
        if (who == me.id) return fail("You can't do that to yourself.");
        if (action == "kick") {
            // Admins can remove members; the owner can remove anyone (but not themselves).
            if (!(myRole == "Owner" || (myRole == "Admin" && theirRole == "Member")) && !isStaff(me))
                return fail("You can't remove them.");
            if (theirRole == "Owner") return fail("The owner can't be removed.");
            g.members.erase(who);
        } else if (action == "admin" || action == "member") {
            if (myRole != "Owner") return fail("Only the owner can change roles.");
            g.members[who] = action == "admin" ? "Admin" : "Member";
        } else if (action == "owner") {
            if (myRole != "Owner") return fail("Only the owner can give the group away.");
            g.members[who] = "Owner";
            g.members[me.id] = "Admin";
            g.owner = who;
        } else {
            return fail("Unknown member action.");
        }
        saveGroups();
        log(me.name + " " + action + " " + who.substr(0, 8) + " in \"" + g.name + "\"");
        return okay();
    }
    if (name == "groups.delete") {
        if (myRole != "Owner" && !isStaff(me)) return fail("Only the owner can delete the group.");
        log(me.name + " deleted the group \"" + g.name + "\"");
        m_groups.erase(it);
        saveGroups();
        return okay();
    }
    return fail("The server doesn't know how to do \"" + name + "\". It might need updating.");
}

// ---------------------------------------------------------------------------

void GbServer::saveGroups() {
    json all = json::object();
    for (const auto& [id, g] : m_groups) {
        json wall = json::array();
        for (const Post& p : g.wall) wall.push_back({p.by, p.text, p.time});
        all[id] = {{"name", g.name}, {"description", g.description}, {"owner", g.owner}, {"created", g.created},
                   {"color", g.color}, {"open", g.open}, {"members", g.members}, {"requests", g.requests},
                   {"shout", {g.shout.by, g.shout.text, g.shout.time}}, {"wall", wall}};
    }
    writeFile(m_opts.data / "groups.json", all.dump(1));
}

void GbServer::loadGroups() {
    std::string text;
    if (!readFile(m_opts.data / "groups.json", text)) return;
    json all = json::parse(text, nullptr, false);
    if (!all.is_object()) return;
    for (auto& [id, j] : all.items()) {
        Group g;
        g.id = id;
        g.name = j.value("name", std::string());
        g.description = j.value("description", std::string());
        g.owner = j.value("owner", std::string());
        g.created = j.value("created", 0LL);
        g.color = j.value("color", 0x3A7BD5);
        g.open = j.value("open", true);
        if (j.contains("members") && j["members"].is_object())
            for (auto& [k, v] : j["members"].items()) if (v.is_string()) g.members[k] = v.get<std::string>();
        if (j.contains("requests")) for (const auto& r : j["requests"]) if (r.is_string()) g.requests.insert(r.get<std::string>());
        if (j.contains("shout") && j["shout"].is_array() && j["shout"].size() == 3)
            g.shout = {j["shout"][0].get<std::string>(), j["shout"][1].get<std::string>(), j["shout"][2].get<long long>()};
        if (j.contains("wall"))
            for (const auto& p : j["wall"])
                if (p.is_array() && p.size() == 3) g.wall.push_back({p[0].get<std::string>(), p[1].get<std::string>(), p[2].get<long long>()});
        if (!g.name.empty() && !g.owner.empty()) m_groups[id] = g;
    }
}
