// Saved outfits, favourite games, "Continue playing" and private messages
// (worker/server.js has the same requests and limits).
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kMaxOutfits = 30, kMaxFavorites = 200;
constexpr size_t kMaxInbox = 100, kMaxSent = 50;
constexpr int    kMessagesPerDay = 40;
} // namespace

// "Continue playing": the games you opened last, newest first (Server.cpp "get").
void GbServer::rememberPlayed(User& me, const std::string& gameId) {
    if (me.userId == 0) return;
    me.recent.erase(std::remove(me.recent.begin(), me.recent.end(), gameId), me.recent.end());
    me.recent.insert(me.recent.begin(), gameId);
    if (me.recent.size() > kMaxRecentGames) me.recent.resize(kMaxRecentGames);
}

json GbServer::socialOp(const std::string& name, User& me, const json& args) {
    const long long now = Online::unixNow();
    const std::string today = Online::utcDay(now);
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };

    // --- Saved outfits: your whole look, kept to put back on later ---
    if (name.rfind("outfit.", 0) == 0) {
        if (me.userId == 0) return fail("Sign up first.");
        if (!me.outfits.is_array()) me.outfits = json::array();
        auto list = [&]() {
            json out = json::array();
            for (const json& o : me.outfits) {
                json wearing = json::array();
                if (o.contains("avatar") && o["avatar"].contains("wearing"))
                    for (const json& id : o["avatar"]["wearing"])
                        if (id.is_string()) if (auto it = m_assets.find(id.get<std::string>()); it != m_assets.end())
                            wearing.push_back(publicAsset(it->second));
                out.push_back({{"id", o.value("id", std::string())}, {"name", o.value("name", std::string())},
                               {"avatar", o.value("avatar", json::object())}, {"created", o.value("created", 0LL)}, {"wearing", wearing}});
            }
            json r = okay(); r["outfits"] = out; r["me"] = meJson(me); return r;
        };
        auto find = [&]() -> json* {
            for (json& o : me.outfits) if (o.value("id", std::string()) == str("id")) return &o;
            return nullptr;
        };
        if (name == "outfit.list") return list();
        if (name == "outfit.save") {
            if (!me.avatar.is_object()) return fail("Change your look first, then save it as an outfit.");
            std::string title = Online::cleanText(str("name"), 40);
            if (title.empty()) title = "Outfit " + std::to_string(me.outfits.size() + 1);
            if (json* old = find()) { (*old)["avatar"] = me.avatar; (*old)["name"] = title; }
            else {
                if (me.outfits.size() >= kMaxOutfits) return fail("You have " + std::to_string(kMaxOutfits) + " outfits. Delete one first.");
                me.outfits.insert(me.outfits.begin(), json{{"id", Account::randomHex(6)}, {"name", title}, {"avatar", me.avatar}, {"created", now}});
            }
            saveUsers();
            return list();
        }
        json* o = find();
        if (!o) return fail("That outfit isn't there any more.");
        if (name == "outfit.wear") {
            json av = (*o)["avatar"];
            json wearing = json::array();   // things sold or traded away since are left off
            if (av.contains("wearing") && av["wearing"].is_array())
                for (const json& w : av["wearing"]) if (w.is_string() && me.owned.count(w.get<std::string>())) wearing.push_back(w);
            av["wearing"] = wearing;
            av["updated"] = now;
            me.avatar = av;
            saveUsers();
            return list();
        }
        if (name == "outfit.rename") {
            const std::string title = Online::cleanText(str("name"), 40);
            if (title.empty()) return fail("Give it a name.");
            (*o)["name"] = title;
            saveUsers();
            return list();
        }
        if (name == "outfit.delete") {
            const std::string id = str("id");
            json kept = json::array();
            for (const json& x : me.outfits) if (x.value("id", std::string()) != id) kept.push_back(x);
            me.outfits = kept;
            saveUsers();
            return list();
        }
        return fail("Unknown request.");
    }

    // --- Favourite games, and the ones you played last ---
    if (name == "game.favorite") {
        if (me.userId == 0) return fail("Sign up first.");
        auto it = m_assets.find(str("id"));
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        Asset& a = it->second;
        const bool had = std::find(me.favorites.begin(), me.favorites.end(), a.id) != me.favorites.end();
        const bool want = !(args.contains("on") && args["on"].is_boolean() && !args["on"].get<bool>());
        long long count = a.meta.is_object() ? a.meta.value("favorites", 0LL) : 0LL;
        if (want && !had) {
            if (me.favorites.size() >= kMaxFavorites) return fail("You have " + std::to_string(kMaxFavorites) + " favourites. Take one off first.");
            me.favorites.insert(me.favorites.begin(), a.id);
            ++count;
        } else if (!want && had) {
            me.favorites.erase(std::remove(me.favorites.begin(), me.favorites.end(), a.id), me.favorites.end());
            count = std::max(0LL, count - 1);
        }
        a.meta["favorites"] = count;
        saveUsers();
        saveAssets();
        json pa = publicAsset(a);
        pa["myFavorite"] = want;
        json r = okay(); r["asset"] = pa; return r;
    }
    if (name == "games.mine") {   // which: "recent" (Continue playing) or "favorites"
        const std::vector<std::string>& ids = str("which") == "favorites" ? me.favorites : me.recent;
        long long limit = args.contains("limit") && args["limit"].is_number_integer() ? args["limit"].get<long long>() : 30;
        limit = std::clamp(limit, 1LL, 200LL);
        json out = json::array();
        for (const std::string& id : ids) {
            auto it = m_assets.find(id);
            if (it == m_assets.end() || it->second.kind != "game") continue;
            json pa = publicAsset(it->second);
            pa["myFavorite"] = std::find(me.favorites.begin(), me.favorites.end(), id) != me.favorites.end();
            out.push_back(pa);
            if ((long long)out.size() >= limit) break;
        }
        json r = okay(); r["assets"] = out; return r;
    }

    // --- Private messages. Guests can't send or get them. ---
    if (name == "message.send") {
        if (me.userId == 0) return fail("Sign up first.");
        const std::string want = str("to");
        bool number = !want.empty();
        for (size_t i = 0; i < want.size(); ++i) if (!(std::isdigit((unsigned char)want[i]) || (i == 0 && want[i] == '#'))) number = false;
        User* them = number && want != "#" ? findUserId(std::atoll(want.c_str() + (want[0] == '#' ? 1 : 0))) : findPerson(want);
        if (!them || them->userId == 0) return fail("There's no account with that ID on this server.");
        if (them->id == me.id) return fail("You can't send a message to yourself.");
        if (them->banned) return fail("You can't send messages to that account.");
        const std::string& setting = them->privacyMessages;
        if (setting == "nobody" || (setting == "friends" && !them->friends.count(me.id)))
            return fail(setting == "friends" ? them->name + " only gets messages from friends." : them->name + " doesn't get messages.");
        std::string subject = Online::cleanText(str("subject"), 80);
        if (subject.empty()) subject = "(no subject)";
        const std::string body = Online::cleanText(str("body"), 2000, true);
        if (body.empty()) return fail("Write something first.");
        if (me.messageDay != today) { me.messageDay = today; me.messagesToday = 0; }
        if (me.messagesToday >= kMessagesPerDay) return fail("That's " + std::to_string(kMessagesPerDay) + " messages today. Try again tomorrow.");
        me.messagesToday++;
        const std::string id = Account::randomHex(8);
        if (!them->inbox.is_array()) them->inbox = json::array();
        them->inbox.insert(them->inbox.begin(), json{{"id", id}, {"from", me.id}, {"subject", subject}, {"body", body}, {"at", now}, {"read", false}});
        while (them->inbox.size() > kMaxInbox) them->inbox.erase(them->inbox.end() - 1);
        if (!me.sent.is_array()) me.sent = json::array();
        me.sent.insert(me.sent.begin(), json{{"id", id}, {"to", them->id}, {"subject", subject}, {"body", body}, {"at", now}});
        while (me.sent.size() > kMaxSent) me.sent.erase(me.sent.end() - 1);
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "message.list") {   // box: "inbox" or "sent"
        const bool sent = str("box") == "sent";
        auto who = [&](const std::string& id) {
            auto it = m_users.find(id);
            if (it == m_users.end()) return json{{"id", id}, {"userId", 0}, {"name", "?"}};
            return json{{"id", id}, {"userId", it->second.userId}, {"name", it->second.name}, {"verified", isVerified(it->second)}};
        };
        json out = json::array();
        const json& box = sent ? me.sent : me.inbox;
        if (me.userId != 0 && box.is_array())
            for (const json& m : box)
                out.push_back({{"id", m.value("id", std::string())}, {"subject", m.value("subject", std::string())},
                               {"body", m.value("body", std::string())}, {"at", m.value("at", 0LL)},
                               {"read", sent ? true : m.value("read", false)},
                               {"from", sent ? who(me.id) : who(m.value("from", std::string()))},
                               {"to", sent ? who(m.value("to", std::string())) : who(me.id)}});
        json r = okay(); r["messages"] = out; r["me"] = meJson(me); return r;
    }
    if (name == "message.read" || name == "message.delete") {
        json& box = str("box") == "sent" ? me.sent : me.inbox;
        if (!box.is_array()) return fail("That message isn't there any more.");
        for (size_t i = 0; i < box.size(); ++i) {
            if (box[i].value("id", std::string()) != str("id")) continue;
            if (name == "message.read") box[i]["read"] = true;
            else box.erase(box.begin() + (long)i);
            saveUsers();
            json r = okay(); r["me"] = meJson(me); return r;
        }
        return fail("That message isn't there any more.");
    }
    return fail("Unknown request.");
}
