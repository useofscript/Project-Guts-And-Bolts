#include "AssetCache.h"
#include "OnlineClient.h"
#include "Protocol.h"
#include "../core/Paths.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#include <fstream>
#include <set>

namespace Online {

namespace {
std::set<std::string>& inFlight() { static std::set<std::string> s; return s; }

std::string extensionFor(const nlohmann::json& asset) {
    std::string kind = asset.value("kind", std::string());
    if (kind == "game") return ".gbscene";
    if (kind == "plugin") return ".lua";
    if (kind == "shirt" || kind == "pants" || kind == "face" || kind == "tshirt") return ".png";   // clothing / face pictures
    if (Online::isAccessory(kind)) return ".json";   // an accessory made in Studio
    if (kind == "audio" || kind == "decal") {
        std::string ext = asset.contains("meta") ? asset["meta"].value("ext", std::string(kind == "decal" ? "png" : "mp3"))
                                                 : std::string(kind == "decal" ? "png" : "mp3");
        return "." + ext;
    }
    return ".json";
}
} // namespace

void download(const std::string& id, Downloaded done, bool redownload) {
    if (!redownload) {
        std::filesystem::path have = Paths::downloaded(id);
        if (!have.empty()) { if (done) done(true, have, nlohmann::json::object()); return; }
    }
    if (inFlight().count(id) && !done) return;
    inFlight().insert(id);
    request("get", {{"id", id}}, [id, done](const nlohmann::json& r) {
        inFlight().erase(id);
        std::string bytes;
        if (!r.value("ok", false) || !base64Decode(r.value("data", std::string()), bytes)) {
            if (done) done(false, {}, nlohmann::json{{"error", r.value("error", std::string("Download failed."))}});
            return;
        }
        std::filesystem::path out = Paths::downloadsFolder() / (id + extensionFor(r["asset"]));
        std::ofstream f(out, std::ios::binary);
        f.write(bytes.data(), (std::streamsize)bytes.size());
        f.close();
        if (done) done(bool(f), out, r["asset"]);
    }, 60);
}

void fetchSounds(Scene& scene) {
    if (!online()) return;
    scene.forEach([](SceneNode* n) {
        const std::string* id = n->isSound() ? &n->soundId : (n->isDecal() || n->isPart()) && !n->texture.empty() ? &n->texture : nullptr;
        if (id && id->rfind("gb:", 0) == 0 && Paths::downloaded(id->substr(3)).empty())
            download(id->substr(3));
    });
}

} // namespace Online
