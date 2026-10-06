// DataStores: what games save between visits (coins, levels...), kept on the server
// so every server of a game sees the same data (worker/server.js dataOp has the same).
//
// Only servers we trust can read and write a game's data: game server machines and
// other staff, and the game's creator. Games other people host save on their own
// computer instead (ScriptEngine falls back), so nobody can hand themselves free coins.
//
//   data.can       {game}                     -> {save}  may this account use it?
//   data.get       {game, store, key}         -> {value} (null if nothing's saved)
//   data.set       {game, store, key, value}  (no value / null: remove it)
//   data.increment {game, store, key, delta}  -> {value} (in one step, so two servers adding at once both count)
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <cctype>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kDataName  = 100;           // store names and keys
constexpr size_t kDataValue = 256 * 1024;    // one value, as JSON
constexpr size_t kDataKeys  = 100000;        // keys in one game
} // namespace

json& GbServer::gameData(const std::string& game) {
    auto it = m_gameData.find(game);
    if (it != m_gameData.end()) return it->second;
    json& d = m_gameData[game];
    std::string text;
    if (readFile(m_opts.data / "gamedata" / (game + ".json"), text)) d = json::parse(text, nullptr, false);
    if (!d.is_object()) d = json::object();
    return d;
}

void GbServer::saveGameData(const std::string& game) {
    std::error_code ec;
    fs::create_directories(m_opts.data / "gamedata", ec);
    writeFile(m_opts.data / "gamedata" / (game + ".json"), gameData(game).dump());
}

json GbServer::dataOp(const std::string& name, User& me, const json& args) {
    std::string game = Online::cleanText(args.value("game", std::string()), 80);
    auto a = findAsset(game);
    if (a == m_assets.end() || a->second.kind != "game") return fail("That game isn't there.");
    game = a->first;
    for (char c : game)   // (it names a file)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return fail("That game isn't there.");
    const bool trusted = isStaff(me) || a->second.creator == me.id;
    if (name == "data.can") { json r = okay(); r["save"] = trusted; return r; }
    if (!trusted) return fail("Only the game's own servers can use its saved data.");
    const std::string store = args.contains("store") && args["store"].is_string() ? args["store"].get<std::string>() : "";
    const std::string key = args.contains("key") && args["key"].is_string() ? args["key"].get<std::string>() : "";
    if (store.empty() || key.empty() || store.size() > kDataName || key.size() > kDataName)
        return fail("DataStore names and keys are 1-" + std::to_string(kDataName) + " letters.");
    json& all = gameData(game);
    auto read = [&]() -> json {
        if (all.contains(store) && all[store].contains(key)) return all[store][key];
        return nullptr;
    };
    auto write = [&](const json& value) -> json {   // null = fine, else the failure reply
        if (value.is_null()) {
            if (all.contains(store)) { all[store].erase(key); if (all[store].empty()) all.erase(store); }
        } else {
            if (value.dump().size() > kDataValue) return fail("That's too much to save in one key (256 KB at most).");
            if (read().is_null()) {
                size_t n = 0;
                for (auto& [s, keys] : all.items()) n += keys.size();
                if (n >= kDataKeys) return fail("This game has saved " + std::to_string(kDataKeys) + " keys already. Remove some first.");
            }
            all[store][key] = value;
        }
        saveGameData(game);
        return nullptr;
    };
    if (name == "data.get") { json r = okay(); r["value"] = read(); return r; }
    if (name == "data.set") {
        json bad = write(args.contains("value") ? args["value"] : json());
        return bad.is_null() ? okay() : bad;
    }
    if (name == "data.increment") {
        json old = read();
        if (!old.is_null() && !old.is_number()) return fail("IncrementAsync: that key doesn't hold a number.");
        const double delta = args.contains("delta") && args["delta"].is_number() ? args["delta"].get<double>() : 1.0;
        json value;
        if ((old.is_null() || old.is_number_integer()) && (!args.contains("delta") || args["delta"].is_number_integer()))
            value = (old.is_null() ? 0LL : old.get<long long>()) + (long long)delta;
        else
            value = (old.is_null() ? 0.0 : old.get<double>()) + delta;
        json bad = write(value);
        if (!bad.is_null()) return bad;
        json r = okay(); r["value"] = value; return r;
    }
    return fail("Unknown request.");
}
