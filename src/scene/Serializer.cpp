#include "Serializer.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Player.h"
#include "../renderer/MeshLibrary.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace {

json vec(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

glm::vec3 vec(const json& j, const char* key, glm::vec3 fallback) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 3) return fallback;
    return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
}

template <typename T>
T get(const json& j, const char* key, T fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    try { return it->get<T>(); } catch (...) { return fallback; }
}

const char* kindName(NodeKind k) {
    switch (k) {
        case NodeKind::Model:  return "Model";
        case NodeKind::Script: return "Script";
        default:               return "Part";
    }
}
NodeKind kindFrom(const std::string& s) {
    if (s == "Model")  return NodeKind::Model;
    if (s == "Script") return NodeKind::Script;
    return NodeKind::Part;
}

const char* shapeName(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Cube:     return "Cube";
        case PrimitiveType::Sphere:   return "Sphere";
        case PrimitiveType::Plane:    return "Plane";
        case PrimitiveType::Cylinder: return "Cylinder";
        default:                      return "None";
    }
}
PrimitiveType shapeFrom(const std::string& s) {
    if (s == "Cube")     return PrimitiveType::Cube;
    if (s == "Sphere")   return PrimitiveType::Sphere;
    if (s == "Plane")    return PrimitiveType::Plane;
    if (s == "Cylinder") return PrimitiveType::Cylinder;
    return PrimitiveType::None;
}

const char* materialNames[] = {"Plastic", "Metal", "Neon", "Wood"};
Material materialFrom(const std::string& s) {
    for (int i = 0; i < 4; ++i) if (s == materialNames[i]) return (Material)i;
    return Material::Plastic;
}

json toJson(const SceneNode& n) {
    json j;
    j["id"]   = n.id;
    j["name"] = n.name;
    j["kind"] = kindName(n.kind);
    j["pos"]  = vec(n.transform.position);
    j["rot"]  = vec(n.transform.rotation);
    j["size"] = vec(n.transform.scale);
    if (n.kind == NodeKind::Part) {
        j["shape"]        = shapeName(n.primitiveType);
        j["color"]        = vec(n.color);
        j["transparency"] = n.transparency;
        j["material"]     = materialNames[(int)n.material];
        j["anchored"]     = n.anchored;
        j["canCollide"]   = n.canCollide;
        j["castShadow"]   = n.castShadow;
    }
    if (n.kind == NodeKind::Script) {
        j["source"]  = n.source;
        j["enabled"] = n.scriptEnabled;
    }
    if (!n.visible) j["visible"]  = false;
    if (n.internal) j["internal"] = true;

    json kids = json::array();
    for (auto& c : n.children) kids.push_back(toJson(*c));
    if (!kids.empty()) j["children"] = std::move(kids);
    return j;
}

std::unique_ptr<SceneNode> fromJson(const json& j, bool freshIds) {
    auto n = std::make_unique<SceneNode>(get<std::string>(j, "name", "Part"),
                                         kindFrom(get<std::string>(j, "kind", "Part")));
    if (!freshIds && j.contains("id")) {
        n->id = j["id"].get<uint64_t>();
        SceneNode::reserveId(n->id);
    }
    n->transform.position = vec(j, "pos",  {0, 0, 0});
    n->transform.rotation = vec(j, "rot",  {0, 0, 0});
    n->transform.scale    = vec(j, "size", {1, 1, 1});
    if (n->kind == NodeKind::Part) {
        n->primitiveType = shapeFrom(get<std::string>(j, "shape", "None"));
        n->mesh          = MeshLibrary::get(n->primitiveType);
        n->color         = vec(j, "color", n->color);
        n->transparency  = get<float>(j, "transparency", 0.0f);
        n->material      = materialFrom(get<std::string>(j, "material", "Plastic"));
        n->anchored      = get<bool>(j, "anchored", true);
        n->canCollide    = get<bool>(j, "canCollide", true);
        n->castShadow    = get<bool>(j, "castShadow", true);
    }
    n->source        = get<std::string>(j, "source", "");
    n->scriptEnabled = get<bool>(j, "enabled", true);
    n->visible       = get<bool>(j, "visible", true);
    n->internal      = get<bool>(j, "internal", false);

    if (auto it = j.find("children"); it != j.end() && it->is_array())
        for (const auto& c : *it) n->addChild(fromJson(c, freshIds));
    return n;
}

json envToJson(const Environment& e) {
    return {
        {"sunAzimuth", e.sunAzimuth}, {"sunElevation", e.sunElevation},
        {"sunColor", vec(e.sunColor)}, {"sunIntensity", e.sunIntensity},
        {"shadows", e.shadows},
        {"ambientColor", vec(e.ambientColor)}, {"ambientIntensity", e.ambientIntensity},
        {"showSky", e.showSky}, {"skyZenith", vec(e.skyZenith)},
        {"skyHorizon", vec(e.skyHorizon)}, {"skyGround", vec(e.skyGround)},
        {"fogEnabled", e.fogEnabled}, {"fogColor", vec(e.fogColor)}, {"fogDensity", e.fogDensity},
    };
}

Environment envFromJson(const json& j) {
    Environment e;
    e.sunAzimuth       = get<float>(j, "sunAzimuth", e.sunAzimuth);
    e.sunElevation     = get<float>(j, "sunElevation", e.sunElevation);
    e.sunColor         = vec(j, "sunColor", e.sunColor);
    e.sunIntensity     = get<float>(j, "sunIntensity", e.sunIntensity);
    e.shadows          = get<bool>(j, "shadows", e.shadows);
    e.ambientColor     = vec(j, "ambientColor", e.ambientColor);
    e.ambientIntensity = get<float>(j, "ambientIntensity", e.ambientIntensity);
    e.showSky          = get<bool>(j, "showSky", e.showSky);
    e.skyZenith        = vec(j, "skyZenith", e.skyZenith);
    e.skyHorizon       = vec(j, "skyHorizon", e.skyHorizon);
    e.skyGround        = vec(j, "skyGround", e.skyGround);
    e.fogEnabled       = get<bool>(j, "fogEnabled", e.fogEnabled);
    e.fogColor         = vec(j, "fogColor", e.fogColor);
    e.fogDensity       = get<float>(j, "fogDensity", e.fogDensity);
    return e;
}

} // namespace

namespace Serializer {

std::string saveScene(Scene& scene, bool pretty) {
    json j;
    j["format"]  = "GutsAndBolts";
    j["version"] = 2;
    j["info"] = {{"title", scene.info().title}, {"description", scene.info().description},
                 {"author", scene.info().author}};
    j["environment"] = envToJson(scene.environment());
    j["world"] = {{"gravity", scene.world().gravity},
                  {"fallenPartsHeight", scene.world().fallenPartsHeight}};
    if (Player* p = scene.player()) {
        const Humanoid& h = p->humanoid();
        j["player"] = {
            {"rootId", p->rootId()}, {"spawn", vec(p->spawn())}, {"hat", (int)p->hat()},
            {"humanoid", {{"walkSpeed", h.walkSpeed}, {"jumpPower", h.jumpPower},
                          {"health", h.health}, {"maxHealth", h.maxHealth},
                          {"autoRotate", h.autoRotate}}},
        };
    }
    j["workspace"] = toJson(*scene.root());
    return pretty ? j.dump(2) : j.dump();
}

bool loadScene(Scene& scene, const std::string& text, std::string* error) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("workspace")) {
        if (error) *error = "This doesn't look like a Guts and Bolts scene file.";
        return false;
    }
    try {
        auto root = fromJson(j["workspace"], false);
        root->kind = NodeKind::Model;
        scene.replaceRoot(std::move(root));

        GameInfo info;
        if (j.contains("info")) {
            info.title       = get<std::string>(j["info"], "title", info.title);
            info.description = get<std::string>(j["info"], "description", info.description);
            info.author      = get<std::string>(j["info"], "author", info.author);
        }
        scene.info() = info;

        scene.environment() = j.contains("environment") ? envFromJson(j["environment"]) : Environment{};
        WorldSettings w;
        if (j.contains("world")) {
            w.gravity           = get<float>(j["world"], "gravity", w.gravity);
            w.fallenPartsHeight = get<float>(j["world"], "fallenPartsHeight", w.fallenPartsHeight);
        }
        scene.world() = w;

        if (Player* p = scene.player()) {
            p->resetSettings();
            p->setRootId(0);
            if (j.contains("player")) {
                const json& pj = j["player"];
                p->setRootId(get<uint64_t>(pj, "rootId", 0));
                p->setSpawn(vec(pj, "spawn", {0, 0, 0}));
                p->rememberHat((HatStyle)get<int>(pj, "hat", 0));
                if (pj.contains("humanoid")) {
                    const json& hj = pj["humanoid"];
                    Humanoid& h  = p->humanoid();
                    h.walkSpeed  = get<float>(hj, "walkSpeed", h.walkSpeed);
                    h.jumpPower  = get<float>(hj, "jumpPower", h.jumpPower);
                    h.maxHealth  = get<float>(hj, "maxHealth", h.maxHealth);
                    h.health     = get<float>(hj, "health", h.maxHealth);
                    h.autoRotate = get<bool>(hj, "autoRotate", h.autoRotate);
                }
            }
            if (!p->root()) p->build();   // older files / missing character
        }
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

std::string nodeToString(const SceneNode& node) { return toJson(node).dump(); }

std::unique_ptr<SceneNode> nodeFromString(const std::string& text, bool freshIds) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return nullptr;
    try { return fromJson(j, freshIds); } catch (...) { return nullptr; }
}

std::unique_ptr<SceneNode> clone(const SceneNode& node) {
    return fromJson(toJson(node), true);
}

bool writeFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << text;
    return (bool)f;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

} // namespace Serializer
