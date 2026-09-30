#include <algorithm>
#include "Serializer.h"
#include "EditMesh.h"
#include "RobloxFile.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Player.h"
#include "PlayerModel.h"
#include "../renderer/MeshLibrary.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <unordered_map>

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
        case NodeKind::Light:  return "Light";
        case NodeKind::ForceField: return "ForceField";
        case NodeKind::Sound:  return "Sound";
        case NodeKind::Attachment: return "Attachment";
        case NodeKind::Constraint: return "Constraint";
        case NodeKind::Tool:   return "Tool";
        case NodeKind::Value:  return "Value";
        case NodeKind::Decal:  return "Decal";
        case NodeKind::Animation: return "Animation";
        case NodeKind::Gui:    return "Gui";
        default:               return "Part";
    }
}
NodeKind kindFrom(const std::string& s) {
    if (s == "Model")  return NodeKind::Model;
    if (s == "Script") return NodeKind::Script;
    if (s == "Light")  return NodeKind::Light;
    if (s == "ForceField") return NodeKind::ForceField;
    if (s == "Sound")  return NodeKind::Sound;
    if (s == "Attachment") return NodeKind::Attachment;
    if (s == "Constraint") return NodeKind::Constraint;
    if (s == "Tool")   return NodeKind::Tool;
    if (s == "Value")  return NodeKind::Value;
    if (s == "Decal")  return NodeKind::Decal;
    if (s == "Animation") return NodeKind::Animation;
    if (s == "Gui")    return NodeKind::Gui;
    return NodeKind::Part;
}

const char* shapeName(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Cube:     return "Cube";
        case PrimitiveType::Sphere:   return "Sphere";
        case PrimitiveType::Plane:    return "Plane";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::Mesh:     return "Mesh";
        default:                      return "None";
    }
}
PrimitiveType shapeFrom(const std::string& s) {
    if (s == "Cube")     return PrimitiveType::Cube;
    if (s == "Sphere")   return PrimitiveType::Sphere;
    if (s == "Plane")    return PrimitiveType::Plane;
    if (s == "Cylinder") return PrimitiveType::Cylinder;
    if (s == "Mesh")     return PrimitiveType::Mesh;
    return PrimitiveType::None;
}

Material materialFrom(const std::string& s) {
    for (int i = 0; i < kMaterialCount; ++i) if (s == kMaterialNames[i]) return (Material)i;
    return Material::Plastic;
}

} // namespace

namespace Serializer {
// Game UI properties (also sent to other players when they change).
nlohmann::json guiToJson(const GuiProps& g) {
    auto ud = [](const UDim2& u) { return json::array({u.xs, u.xo, u.ys, u.yo}); };
    json j = {{"class", kGuiClassNames[(int)g.type]}, {"pos", ud(g.pos)}, {"size", ud(g.size)},
              {"anchor", json::array({g.anchor.x, g.anchor.y})}, {"bg", vec(g.bg)}, {"bgT", g.bgTransparency},
              {"borderColor", vec(g.borderColor)}, {"border", g.border}, {"z", g.zIndex}, {"clips", g.clips}};
    if (g.type == GuiType::TextLabel || g.type == GuiType::TextButton) {
        j["text"] = g.text; j["textColor"] = vec(g.textColor); j["textSize"] = g.textSize;
        j["scaled"] = g.textScaled; j["wrapped"] = g.textWrapped; j["bold"] = g.bold;
        j["xAlign"] = g.xAlign; j["yAlign"] = g.yAlign; j["textT"] = g.textTransparency;
        j["strokeColor"] = vec(g.strokeColor); j["strokeT"] = g.strokeTransparency;
    }
    if (g.type == GuiType::ImageLabel || g.type == GuiType::ImageButton) {
        j["image"] = g.image; j["imageColor"] = vec(g.imageColor); j["imageT"] = g.imageTransparency;
    }
    if (g.type == GuiType::TextButton || g.type == GuiType::ImageButton) j["autoColor"] = g.autoButtonColor;
    if (g.type == GuiType::ScreenGui) j["order"] = g.displayOrder;
    if (g.type == GuiType::UICorner) j["corner"] = ud(g.corner);
    if (g.type == GuiType::UIStroke) j["thickness"] = g.thickness;
    return j;
}

void guiFromJson(GuiProps& g, const nlohmann::json& j) {
    auto ud = [&](const char* k, UDim2 d) {
        if (!j.contains(k) || !j[k].is_array() || j[k].size() != 4) return d;
        try { return UDim2{j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>(), j[k][3].get<float>()}; }
        catch (...) { return d; }
    };
    std::string cls = get<std::string>(j, "class", std::string("Frame"));
    for (int i = 0; i < kGuiTypeCount; ++i) if (cls == kGuiClassNames[i]) g.type = (GuiType)i;
    g.pos = ud("pos", g.pos);
    g.size = ud("size", g.size);
    if (j.contains("anchor") && j["anchor"].is_array() && j["anchor"].size() == 2)
        try { g.anchor = {j["anchor"][0].get<float>(), j["anchor"][1].get<float>()}; } catch (...) {}
    g.bg = vec(j, "bg", g.bg);
    g.bgTransparency = get<float>(j, "bgT", g.bgTransparency);
    g.borderColor = vec(j, "borderColor", g.borderColor);
    g.border = get<int>(j, "border", g.border);
    g.zIndex = get<int>(j, "z", g.zIndex);
    g.clips = get<bool>(j, "clips", g.clips);
    g.text = get<std::string>(j, "text", g.text);
    g.textColor = vec(j, "textColor", g.textColor);
    g.textSize = get<float>(j, "textSize", g.textSize);
    g.textScaled = get<bool>(j, "scaled", g.textScaled);
    g.textWrapped = get<bool>(j, "wrapped", g.textWrapped);
    g.bold = get<bool>(j, "bold", g.bold);
    g.xAlign = std::clamp(get<int>(j, "xAlign", g.xAlign), 0, 2);
    g.yAlign = std::clamp(get<int>(j, "yAlign", g.yAlign), 0, 2);
    g.textTransparency = get<float>(j, "textT", g.textTransparency);
    g.strokeColor = vec(j, "strokeColor", g.strokeColor);
    g.strokeTransparency = get<float>(j, "strokeT", g.strokeTransparency);
    g.image = get<std::string>(j, "image", g.image);
    g.imageColor = vec(j, "imageColor", g.imageColor);
    g.imageTransparency = get<float>(j, "imageT", g.imageTransparency);
    g.autoButtonColor = get<bool>(j, "autoColor", g.autoButtonColor);
    g.displayOrder = get<int>(j, "order", g.displayOrder);
    g.corner = ud("corner", g.corner);
    g.thickness = get<float>(j, "thickness", g.thickness);
}
} // namespace Serializer

namespace {
using Serializer::guiToJson;
using Serializer::guiFromJson;

json toJson(const SceneNode& n) {
    json j;
    j["id"]   = n.id;
    j["name"] = n.name;
    j["kind"] = kindName(n.kind);
    // (A part posed in the Animation Editor is saved where it really is.)
    j["pos"]  = vec(n.restPose ? n.restPose->position : n.transform.position);
    j["rot"]  = vec(n.restPose ? n.restPose->rotation : n.transform.rotation);
    j["size"] = vec(n.transform.scale);
    if (n.kind == NodeKind::Part) {
        j["shape"]        = shapeName(n.primitiveType);
        if (const char* body = n.primitiveType == PrimitiveType::Mesh ? PlayerModel::nameOf(n.editMesh.get()) : nullptr) {
            j["body"] = body;   // the default character's shape: no need to save its points
        } else if (n.primitiveType == PrimitiveType::Mesh && n.editMesh) {
            // A custom mesh: "v" = x,y,z,x,y,z..., "f" = lists of corner numbers.
            json v = json::array(), f = json::array();
            for (const auto& p : n.editMesh->verts) { v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); }
            for (const auto& face : n.editMesh->faces) f.push_back(face);
            j["mesh"] = {{"v", v}, {"f", f}, {"smooth", n.editMesh->smooth}};
        }
        j["color"]        = vec(n.color);
        j["transparency"] = n.transparency;
        j["material"]     = kMaterialNames[(int)n.material];
        j["anchored"]     = n.anchored;
        j["canCollide"]   = n.canCollide;
        j["castShadow"]   = n.castShadow;
        if (n.density >= 0)    j["density"]    = n.density;
        if (n.friction >= 0)   j["friction"]   = n.friction;
        if (n.elasticity >= 0) j["elasticity"] = n.elasticity;
    }
    if (n.kind == NodeKind::Constraint) {
        j["type"] = kConstraintNames[(int)n.constraintType];
        j["ref0"] = n.ref0; j["ref1"] = n.ref1;
        j["length"] = n.length; j["stiffness"] = n.stiffness; j["damping"] = n.damping;
        j["motorSpeed"] = n.motorSpeed; j["motorTorque"] = n.motorTorque;
        j["thickness"] = n.thickness; j["color"] = vec(n.color); j["enabled"] = n.enabled;
    }
    if (n.kind == NodeKind::Animation) j["source"] = n.source;
    if (n.kind == NodeKind::Script) {
        j["source"]  = n.source;
        j["enabled"] = n.enabled;
    }
    if (n.kind == NodeKind::Gui) {
        j["gui"] = guiToJson(n.gui);
        j["enabled"] = n.enabled;
    }
    if (n.kind == NodeKind::Decal) {
        j["texture"] = n.texture;
        j["face"] = kFaceNames[(int)n.face];
        j["color"] = vec(n.color);
        j["transparency"] = n.transparency;
    }
    if (n.kind == NodeKind::Value) {
        json v = {{"t", (int)n.value.type}, {"int", n.intValue}};
        switch (n.value.type) {
            case Attribute::Bool:   v["v"] = n.value.b; break;
            case Attribute::Number: v["v"] = n.value.n; break;
            case Attribute::String: v["v"] = n.value.s; break;
            default:                v["v"] = vec(n.value.v); break;
        }
        j["value"] = v;
    }
    if (n.kind == NodeKind::Tool) {
        j["enabled"] = n.enabled;
        j["toolTip"] = n.toolTip;
        j["canBeDropped"] = n.canBeDropped;
        j["starterTool"] = n.starterTool;
        j["gripPos"] = vec(n.gripPos);
    }
    if (n.kind == NodeKind::Sound) {
        j["soundId"] = n.soundId; j["volume"] = n.volume; j["pitch"] = n.pitch;
        j["looped"] = n.looped; j["autoplay"] = n.autoplay;
    }
    if (n.kind == NodeKind::Light) {
        j["lightType"]  = n.lightType == LightType::Spot ? "Spot" : "Point";
        j["color"]      = vec(n.color);
        j["brightness"] = n.brightness;
        j["range"]      = n.range;
        j["spotAngle"]  = n.spotAngle;
        j["enabled"]    = n.enabled;
    }
    if (!n.visible) j["visible"]  = false;
    if (n.internal) j["internal"] = true;
    if (n.isModule) j["module"]   = true;
    if (n.locked)   j["locked"]   = true;
    if (!n.tags.empty()) j["tags"] = n.tags;
    if (!n.attributes.empty()) {
        json attrs = json::array();
        for (const Attribute& a : n.attributes) {
            json e = {{"n", a.name}, {"t", (int)a.type}};
            switch (a.type) {
                case Attribute::Bool:   e["v"] = a.b; break;
                case Attribute::Number: e["v"] = a.n; break;
                case Attribute::String: e["v"] = a.s; break;
                default:                e["v"] = vec(a.v); break;
            }
            attrs.push_back(e);
        }
        j["attrs"] = attrs;
    }

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
        if (n->primitiveType == PrimitiveType::Mesh && j.contains("body")) {
            std::string body = get<std::string>(j, "body", std::string());
            std::string keep = n->name;
            n->name = body;
            if (!PlayerModel::apply(*n)) MeshEdit::attach(*n, MeshEdit::fromPrimitive(PrimitiveType::Cube));
            n->name = keep;
        } else if (n->primitiveType == PrimitiveType::Mesh) {
            auto m = std::make_shared<EditMesh>();
            if (auto it = j.find("mesh"); it != j.end() && it->is_object()) {
                const json& v = (*it)["v"];
                for (size_t i = 0; i + 2 < v.size(); i += 3)
                    m->verts.push_back({v[i].get<float>(), v[i + 1].get<float>(), v[i + 2].get<float>()});
                for (const auto& face : (*it)["f"]) {
                    std::vector<uint32_t> fv;
                    for (const auto& k : face) if (k.get<uint32_t>() < m->verts.size()) fv.push_back(k.get<uint32_t>());
                    if (fv.size() >= 3) m->faces.push_back(std::move(fv));
                }
                m->smooth = get<bool>(*it, "smooth", false);
            }
            if (m->faces.empty()) m = MeshEdit::fromPrimitive(PrimitiveType::Cube);
            MeshEdit::attach(*n, m);
        }
        n->color         = vec(j, "color", n->color);
        n->transparency  = get<float>(j, "transparency", 0.0f);
        n->material      = materialFrom(get<std::string>(j, "material", "Plastic"));
        n->anchored      = get<bool>(j, "anchored", true);
        n->canCollide    = get<bool>(j, "canCollide", true);
        n->castShadow    = get<bool>(j, "castShadow", true);
        n->density       = get<float>(j, "density", -1.0f);
        n->friction      = get<float>(j, "friction", -1.0f);
        n->elasticity    = get<float>(j, "elasticity", -1.0f);
    }
    if (n->kind == NodeKind::Constraint) {
        std::string type = get<std::string>(j, "type", "Rope");
        for (int i = 0; i < 5; ++i) if (type == kConstraintNames[i]) n->constraintType = (ConstraintType)i;
        n->ref0 = get<uint64_t>(j, "ref0", 0);
        n->ref1 = get<uint64_t>(j, "ref1", 0);
        n->length = get<float>(j, "length", -1.0f);
        n->stiffness = get<float>(j, "stiffness", 200.0f);
        n->damping = get<float>(j, "damping", 5.0f);
        n->motorSpeed = get<float>(j, "motorSpeed", 0.0f);
        n->motorTorque = get<float>(j, "motorTorque", 0.0f);
        n->thickness = get<float>(j, "thickness", 0.1f);
        n->color = vec(j, "color", {0.45f, 0.32f, 0.2f});
    }
    if (n->kind == NodeKind::Light) {
        n->lightType  = get<std::string>(j, "lightType", "Point") == "Spot" ? LightType::Spot : LightType::Point;
        n->color      = vec(j, "color", {1, 1, 1});
        n->brightness = get<float>(j, "brightness", 2.0f);
        n->range      = get<float>(j, "range", 14.0f);
        n->spotAngle  = get<float>(j, "spotAngle", 60.0f);
    }
    if (n->kind == NodeKind::Gui) {
        if (j.contains("gui") && j["gui"].is_object()) guiFromJson(n->gui, j["gui"]);
        n->enabled = get<bool>(j, "enabled", true);
    }
    if (n->kind == NodeKind::Decal) {
        n->texture = get<std::string>(j, "texture", std::string());
        std::string f = get<std::string>(j, "face", std::string("Front"));
        for (int i = 0; i < 6; ++i) if (f == kFaceNames[i]) n->face = (Face)i;
        n->color = vec(j, "color", {1, 1, 1});
        n->transparency = get<float>(j, "transparency", 0.0f);
    }
    if (n->kind == NodeKind::Value && j.contains("value") && j["value"].is_object()) {
        const json& vj = j["value"];
        n->value.type = (Attribute::Type)std::clamp(vj.value("t", 1), 0, 4);
        n->intValue = vj.value("int", false);
        const json v = vj.contains("v") ? vj["v"] : json();
        if (n->value.type == Attribute::Bool && v.is_boolean()) n->value.b = v.get<bool>();
        else if (n->value.type == Attribute::Number && v.is_number()) n->value.n = v.get<double>();
        else if (n->value.type == Attribute::String && v.is_string()) n->value.s = v.get<std::string>();
        else if (v.is_array() && v.size() == 3) n->value.v = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
    }
    if (n->kind == NodeKind::Tool) {
        n->toolTip      = get<std::string>(j, "toolTip", std::string());
        n->canBeDropped = get<bool>(j, "canBeDropped", true);
        n->starterTool  = get<bool>(j, "starterTool", false);
        n->gripPos      = vec(j, "gripPos", {0, 0, 0});
    }
    if (n->kind == NodeKind::Sound) {
        n->soundId  = get<std::string>(j, "soundId", "coin");
        n->volume   = get<float>(j, "volume", 0.6f);
        n->pitch    = get<float>(j, "pitch", 1.0f);
        n->looped   = get<bool>(j, "looped", false);
        n->autoplay = get<bool>(j, "autoplay", false);
    }
    n->source        = get<std::string>(j, "source", "");
    n->enabled = get<bool>(j, "enabled", true);
    n->visible       = get<bool>(j, "visible", true);
    n->internal      = get<bool>(j, "internal", false);
    n->isModule      = get<bool>(j, "module", false);
    n->locked        = get<bool>(j, "locked", false);
    if (auto t = j.find("tags"); t != j.end() && t->is_array())
        for (auto& v : *t) if (v.is_string()) n->tags.push_back(v.get<std::string>());
    if (auto at = j.find("attrs"); at != j.end() && at->is_array())
        for (auto& e : *at) {
            if (!e.is_object() || !e.contains("n") || !e.contains("v")) continue;
            Attribute a;
            a.name = e["n"].get<std::string>();
            a.type = (Attribute::Type)std::clamp(e.value("t", 1), 0, 4);
            const json& v = e["v"];
            if (a.type == Attribute::Bool && v.is_boolean()) a.b = v.get<bool>();
            else if (a.type == Attribute::Number && v.is_number()) a.n = v.get<double>();
            else if (a.type == Attribute::String && v.is_string()) a.s = v.get<std::string>();
            else if (v.is_array() && v.size() == 3) a.v = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
            n->attributes.push_back(a);
        }

    if (auto it = j.find("children"); it != j.end() && it->is_array())
        for (const auto& c : *it) n->addChild(fromJson(c, freshIds));
    return n;
}

// Every Environment field, listed once so saving and loading can't drift apart.
#define ENV_FIELDS(X)                                                              \
    X(clockTime) X(sunAzimuth) X(sunElevation) X(sunColor) X(sunIntensity) X(sunSize)            \
    X(shadows) X(shadowSoftness) X(shadowStrength) X(shadowDistance)                \
    X(ambientColor) X(groundAmbient) X(ambientIntensity) X(reflections)             \
    X(showSky) X(skyZenith) X(skyHorizon) X(skyGround) X(skyBrightness)             \
    X(clouds) X(cloudCover) X(cloudSpeed) X(cloudColor) X(stars)                    \
    X(fogEnabled) X(fogColor) X(fogDensity) X(fogSunGlow)                           \
    X(exposure) X(bloomIntensity) X(bloomThreshold) X(aoIntensity) X(contrast)      \
    X(saturation) X(vignette) X(tint)

json toJsonValue(float v) { return v; }
json toJsonValue(bool v) { return v; }
json toJsonValue(const glm::vec3& v) { return vec(v); }
void fromJsonValue(const json& j, const char* k, float& v) { v = get<float>(j, k, v); }
void fromJsonValue(const json& j, const char* k, bool& v)  { v = get<bool>(j, k, v); }
void fromJsonValue(const json& j, const char* k, glm::vec3& v) { v = vec(j, k, v); }

json envToJson(const Environment& e) {
    json j;
#define SAVE_FIELD(f) j[#f] = toJsonValue(e.f);
    ENV_FIELDS(SAVE_FIELD)
#undef SAVE_FIELD
    return j;
}

Environment envFromJson(const json& j) {
    Environment e;
#define LOAD_FIELD(f) fromJsonValue(j, #f, e.f);
    ENV_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
    return e;
}

} // namespace

namespace Serializer {

namespace {
json settingsJson(Scene& scene) {
    json j;
    j["format"]  = "GutsAndBolts";
    j["version"] = 2;
    j["info"] = {{"title", scene.info().title}, {"description", scene.info().description},
                 {"author", scene.info().author}};
    if (!scene.info().publishedId.empty()) j["info"]["published"] = scene.info().publishedId;
    j["environment"] = envToJson(scene.environment());
    const WorldSettings& ws = scene.world();
    j["world"] = {{"gravity", ws.gravity}, {"fallenPartsHeight", ws.fallenPartsHeight},
                  {"deathStyle", (int)ws.deathStyle}, {"gore", (int)ws.gore},
                  {"dismemberment", ws.dismemberment}, {"fallDamage", ws.fallDamage},
                  {"fallDamageSpeed", ws.fallDamageSpeed}, {"spawnForceField", ws.spawnForceField},
                  {"fallDamageScale", ws.fallDamageScale}, {"bloodColor", vec(ws.bloodColor)},
                  {"bloodAmount", ws.bloodAmount}, {"bloodStay", ws.bloodStay},
                  {"playerCollisions", ws.playerCollisions}};
    if (Player* p = scene.player()) {
        const Humanoid& h = p->humanoid();
        j["player"] = {
            {"rootId", p->rootId()}, {"spawn", vec(p->spawn())}, {"hat", (int)p->hat()},
            {"humanoid", {{"walkSpeed", h.walkSpeed}, {"jumpPower", h.jumpPower},
                          {"health", h.health}, {"maxHealth", h.maxHealth},
                          {"autoRotate", h.autoRotate}}},
        };
    }
    return j;
}

void applySettings(Scene& scene, const json& j) {
    GameInfo info;
    if (j.contains("info")) {
        info.title       = get<std::string>(j["info"], "title", info.title);
        info.description = get<std::string>(j["info"], "description", info.description);
        info.author      = get<std::string>(j["info"], "author", info.author);
        info.publishedId = get<std::string>(j["info"], "published", std::string());
    }
    scene.info() = info;

    scene.environment() = j.contains("environment") ? envFromJson(j["environment"]) : Environment{};
    WorldSettings w;
    if (j.contains("world")) {
        w.gravity           = get<float>(j["world"], "gravity", w.gravity);
        w.fallenPartsHeight = get<float>(j["world"], "fallenPartsHeight", w.fallenPartsHeight);
        w.deathStyle        = (DeathStyle)get<int>(j["world"], "deathStyle", (int)w.deathStyle);
        w.gore              = (GoreLevel)get<int>(j["world"], "gore", (int)w.gore);
        w.dismemberment     = get<bool>(j["world"], "dismemberment", w.dismemberment);
        w.fallDamage        = get<bool>(j["world"], "fallDamage", w.fallDamage);
        w.fallDamageSpeed   = get<float>(j["world"], "fallDamageSpeed", w.fallDamageSpeed);
        w.spawnForceField   = get<float>(j["world"], "spawnForceField", w.spawnForceField);
        w.fallDamageScale   = get<float>(j["world"], "fallDamageScale", w.fallDamageScale);
        w.playerCollisions  = get<bool>(j["world"], "playerCollisions", w.playerCollisions);
        w.bloodColor        = vec(j["world"], "bloodColor", w.bloodColor);
        w.bloodAmount       = get<float>(j["world"], "bloodAmount", w.bloodAmount);
        w.bloodStay         = get<float>(j["world"], "bloodStay", w.bloodStay);
    }
    scene.world() = w;

    if (Player* p = scene.player()) {
        p->resetSettings();
        p->setRootId(0);
        if (j.contains("player")) {
            const json& pj = j["player"];
            p->setRootId(get<uint64_t>(pj, "rootId", 0));
            Player::upgradeRig(p->root());   // saved with the old blocky character?
            p->upgradeFace();
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
    }
}
} // namespace

std::string saveScene(Scene& scene, bool pretty) {
    json j = settingsJson(scene);
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
        applySettings(scene, j);
        if (Player* p = scene.player())
            if (!p->root()) p->build();   // older files / missing character
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

std::string settingsToString(Scene& scene) { return settingsJson(scene).dump(); }

void settingsFromString(Scene& scene, const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (j.is_object()) {
        try { applySettings(scene, j); } catch (...) {}
    }
}

std::string nodeShallowToString(const SceneNode& node) {
    json j = toJson(node);
    j.erase("children");
    return j.dump();
}

std::unique_ptr<SceneNode> nodeShallowFromString(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return nullptr;
    j.erase("children");
    try { return fromJson(j, false); } catch (...) { return nullptr; }
}

void applyNodeShallow(SceneNode& dst, const std::string& text) {
    auto src = nodeShallowFromString(text);
    if (!src) return;
    // Copy every saved property, keeping the node's place in the tree.
    dst.name = src->name;           dst.kind = src->kind;
    dst.transform = src->transform; dst.primitiveType = src->primitiveType;
    dst.mesh = src->mesh;           dst.color = src->color;
    dst.editMesh = src->editMesh;
    dst.visible = src->visible;     dst.internal = src->internal;
    dst.transparency = src->transparency; dst.material = src->material;
    dst.anchored = src->anchored;   dst.canCollide = src->canCollide; dst.castShadow = src->castShadow;
    dst.source = src->source;       dst.enabled = src->enabled;
    dst.isModule = src->isModule;   dst.locked = src->locked;
    dst.tags = src->tags;           dst.attributes = src->attributes;
    dst.lightType = src->lightType; dst.brightness = src->brightness;
    dst.range = src->range;         dst.spotAngle = src->spotAngle;
    dst.soundId = src->soundId;     dst.volume = src->volume; dst.pitch = src->pitch;
    dst.looped = src->looped;       dst.autoplay = src->autoplay;
    dst.toolTip = src->toolTip;     dst.canBeDropped = src->canBeDropped;
    dst.value = src->value;         dst.intValue = src->intValue;
    dst.texture = src->texture;     dst.face = src->face;
    dst.starterTool = src->starterTool; dst.gripPos = src->gripPos;
    dst.density = src->density;     dst.friction = src->friction; dst.elasticity = src->elasticity;
    dst.constraintType = src->constraintType; dst.ref0 = src->ref0; dst.ref1 = src->ref1;
    dst.length = src->length;       dst.stiffness = src->stiffness; dst.damping = src->damping;
    dst.motorSpeed = src->motorSpeed; dst.motorTorque = src->motorTorque; dst.thickness = src->thickness;
}

std::string nodeToString(const SceneNode& node) { return toJson(node).dump(); }

std::string environmentToString(const Environment& env) { return envToJson(env).dump(); }

void environmentFromString(Environment& env, const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (j.is_object()) env = envFromJson(j);
}

std::unique_ptr<SceneNode> nodeFromString(const std::string& text, bool freshIds) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return nullptr;
    try {
        if (!freshIds) return fromJson(j, false);
        auto original = fromJson(j, false);
        return clone(*original);
    } catch (...) { return nullptr; }
}

namespace {
void collectIds(const SceneNode& a, const SceneNode& b, std::unordered_map<uint64_t, uint64_t>& map) {
    map[a.id] = b.id;
    for (size_t i = 0; i < a.children.size() && i < b.children.size(); ++i)
        collectIds(*a.children[i], *b.children[i], map);
}
void remapRefs(SceneNode& n, const std::unordered_map<uint64_t, uint64_t>& map) {
    if (n.isConstraint()) {
        if (auto it = map.find(n.ref0); it != map.end()) n.ref0 = it->second;
        if (auto it = map.find(n.ref1); it != map.end()) n.ref1 = it->second;
    }
    for (auto& c : n.children) remapRefs(*c, map);
}
} // namespace

std::unique_ptr<SceneNode> clone(const SceneNode& node) {
    auto copy = fromJson(toJson(node), true);
    // Ropes / hinges inside the copy connect to the copied attachments.
    std::unordered_map<uint64_t, uint64_t> map;
    collectIds(node, *copy, map);
    remapRefs(*copy, map);
    return copy;
}

bool loadGameFile(Scene& scene, const std::string& path, std::string* error) {
    if (RobloxFile::isPlace(path)) {
        RobloxFile::Report report;
        std::string err;
        if (!RobloxFile::importPlace(scene, path, report, err)) { if (error) *error = err; return false; }
        return true;
    }
    std::string text;
    if (!readFile(path, text)) { if (error) *error = "couldn't read the file"; return false; }
    return loadScene(scene, text, error);
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
