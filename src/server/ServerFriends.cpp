// Friends on the Guts&Bolts server: requests, accepting, and a list that
// shows who's online and which game they're in (so you can join them).
// Only you can see your own friends list.
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t    kMaxFriends  = 200;
constexpr size_t    kMaxRequests = 100;   // waiting for someone to answer
constexpr long long kOnlineFor   = 150;   // seconds since we last heard from them
constexpr size_t    kMaxFollowing = 2000;
} // namespace

bool GbServer::isOnline(const User& u) const {
    return Online::unixNow() - u.lastSeen <= kOnlineFor || sessionOf(u.id) != nullptr;
}

json GbServer::friendOp(const std::string& name, User& me, const json& args) {
    auto person = [&](const User& u) {
        return json{{"id", u.id}, {"name", u.name}, {"verified", isVerified(u)}};
    };

    if (name == "friends.list") {
        json friends = json::array(), in = json::array(), out = json::array();
        for (const std::string& id : me.friends) {
            const User* u = findUser(id);
            if (!u) continue;
            json f = person(*u);
            f["online"] = isOnline(*u);
            if (const Session* s = sessionOf(u->id)) {
                // Friends can join each other's private servers too.
                f["playing"] = {{"session", s->id}, {"title", s->title}, {"private", s->priv},
                                {"full", (int)s->players.size() + 1 >= s->max}};
            }
            friends.push_back(f);
        }
        for (const std::string& id : me.friendIn) if (const User* u = findUser(id)) in.push_back(person(*u));
        for (const std::string& id : me.friendOut) if (const User* u = findUser(id)) out.push_back(person(*u));
        json r = okay();
        r["friends"] = friends;
        r["incoming"] = in;
        r["outgoing"] = out;
        return r;
    }

    User* them = findPerson(args.contains("user") && args["user"].is_number_integer()
                                ? std::to_string(args["user"].get<long long>()) : args.value("user", std::string()));
    if (!them || them->userId == 0) return fail("There's no account with that ID on this server.");
    // Where you stand with someone (the in-game player list asks before showing its menu).
    if (name == "friends.relation") {
        json r = okay();
        r["id"] = them->id;
        r["name"] = them->name;
        r["friendship"] = them->id == me.id ? "self" : me.friends.count(them->id) ? "friends"
                        : me.friendOut.count(them->id) ? "sent" : me.friendIn.count(them->id) ? "received" : "none";
        r["following"] = me.following.count(them->id) > 0;
        r["followers"] = them->followers.size();
        return r;
    }
    // Following: one way, no asking (like Roblox).
    if (name == "follow.add" || name == "follow.remove") {
        if (them->id == me.id) return fail("You can't follow yourself.");
        if (name == "follow.add") {
            if (them->banned) return fail("You can't follow that account.");
            if (!me.following.count(them->id) && me.following.size() >= kMaxFollowing)
                return fail("You already follow " + std::to_string(kMaxFollowing) + " people.");
            me.following.insert(them->id);
            them->followers.insert(me.id);
        } else {
            me.following.erase(them->id);
            them->followers.erase(me.id);
        }
        saveUsers();
        json r = okay();
        r["following"] = name == "follow.add";
        r["followers"] = them->followers.size();
        return r;
    }
    if (them->id == me.id) return fail("You can't be friends with yourself (but we like you).");

    auto becomeFriends = [&]() {
        if (me.friends.size() >= kMaxFriends || them->friends.size() >= kMaxFriends)
            return fail("One of you already has " + std::to_string(kMaxFriends) + " friends.");
        me.friendIn.erase(them->id);   me.friendOut.erase(them->id);
        them->friendIn.erase(me.id);   them->friendOut.erase(me.id);
        me.friends.insert(them->id);
        them->friends.insert(me.id);
        saveUsers();
        json r = okay();
        r["status"] = "friends";
        return r;
    };

    if (name == "friends.add") {
        if (me.friends.count(them->id)) { json r = okay(); r["status"] = "friends"; return r; }
        if (me.friendIn.count(them->id)) return becomeFriends();   // they asked first
        if (them->banned) return fail("You can't add that account.");
        if (me.friendOut.size() >= kMaxRequests) return fail("You have too many friend requests waiting. Cancel some first.");
        if (them->friendIn.size() >= kMaxRequests) return fail(them->name + " has too many friend requests waiting.");
        me.friendOut.insert(them->id);
        them->friendIn.insert(me.id);
        saveUsers();
        json r = okay();
        r["status"] = "sent";
        return r;
    }
    if (name == "friends.accept") {
        if (!me.friendIn.count(them->id)) return fail("That friend request isn't there any more.");
        return becomeFriends();
    }
    if (name == "friends.decline" || name == "friends.cancel" || name == "friends.remove") {
        me.friendIn.erase(them->id);   me.friendOut.erase(them->id);
        them->friendIn.erase(me.id);   them->friendOut.erase(me.id);
        if (name == "friends.remove") { me.friends.erase(them->id); them->friends.erase(me.id); }
        saveUsers();
        json r = okay();
        r["status"] = "none";
        return r;
    }
    return fail("Unknown request.");
}
