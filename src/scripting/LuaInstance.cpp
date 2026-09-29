// Scene objects for scripts: Instance (parts, models, scripts), Humanoid,
// Lighting, and the Signal / Connection event objects.
#include "LuaApi.h"
#include "ScriptEngine.h"
#include "../scene/Player.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Serializer.h"
#include "../renderer/MeshLibrary.h"
#include "../core/Audio.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace {

constexpr const char* kInst  = "Instance";
constexpr const char* kSig   = "RBXScriptSignal";
constexpr const char* kConn  = "RBXScriptConnection";
constexpr const char* kHum   = "Humanoid";
constexpr const char* kLight = "Lighting";

struct InstRef   { uint64_t id; };
struct SignalRef { SignalKind kind; uint64_t id; };
struct ConnRef   { int index; };

bool is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

bool parseMaterial(const std::string& s, Material& out) {
    for (int i = 0; i < kMaterialCount; ++i)
        if (s == kMaterialNames[i]) { out = (Material)i; return true; }
    if (s == "SmoothPlastic") { out = Material::Plastic; return true; }
    if (s == "DiamondPlate" || s == "Foil" || s == "CorrodedMetal") { out = Material::Metal; return true; }
    if (s == "WoodPlanks") { out = Material::Wood; return true; }
    return false;
}

const char* shapeName(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Sphere:   return "Ball";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::Plane:    return "Plane";
        default:                      return "Block";
    }
}
bool parseShape(const std::string& s, PrimitiveType& out) {
    if (s == "Block" || s == "Cube" || s == "Part") { out = PrimitiveType::Cube;     return true; }
    if (s == "Ball"  || s == "Sphere")              { out = PrimitiveType::Sphere;   return true; }
    if (s == "Cylinder")                            { out = PrimitiveType::Cylinder; return true; }
    if (s == "Plane")                               { out = PrimitiveType::Plane;    return true; }
    return false;
}

ScriptEngine* E(lua_State* L) { return LuaApi::engine(L); }

bool isCharacterRoot(lua_State* L, const SceneNode* n) {
    return E(L)->scene()->isCharacterRoot(n->id);
}

const char* className(lua_State* L, const SceneNode* n) {
    if (n == E(L)->scene()->root()) return "Workspace";
    switch (n->kind) {
        case NodeKind::Model:  return "Model";
        case NodeKind::Script: return "Script";
        case NodeKind::Light:  return n->lightType == LightType::Spot ? "SpotLight" : "PointLight";
        case NodeKind::ForceField: return "ForceField";
        case NodeKind::Tool:       return "Tool";
        case NodeKind::Value:      return n->valueClass();
        case NodeKind::Sound:      return "Sound";
        case NodeKind::Attachment: return "Attachment";
        case NodeKind::Constraint:
            switch (n->constraintType) {
                case ConstraintType::Rope:   return "RopeConstraint";
                case ConstraintType::Rod:    return "RodConstraint";
                case ConstraintType::Spring: return "SpringConstraint";
                case ConstraintType::Weld:   return "WeldConstraint";
                default:                     return "HingeConstraint";
            }
        default:               return n->primitiveType == PrimitiveType::Mesh ? "MeshPart" : "Part";
    }
}

bool isA(lua_State* L, const SceneNode* n, const std::string& cls) {
    if (cls == "Instance") return true;
    std::string mine = className(L, n);
    if (cls == mine) return true;
    if (n->kind == NodeKind::Part && (cls == "BasePart" || cls == "Part" || cls == "PVInstance")) return true;
    if (n->kind == NodeKind::Model && (cls == "Model" || cls == "Folder" || cls == "PVInstance")) return true;
    if (n->kind == NodeKind::Script && (cls == "BaseScript" || cls == "LuaSourceContainer")) return true;
    if (n->kind == NodeKind::Light && cls == "Light") return true;
    if (n->kind == NodeKind::Constraint && cls == "Constraint") return true;
    if (n->kind == NodeKind::Tool && cls == "BackpackItem") return true;
    if (n->kind == NodeKind::Value && cls == "ValueBase") return true;
    return false;
}

// Rotation-only matrix from Euler degrees (same order as Transform::matrix).
glm::mat4 rotationMatrix(const glm::vec3& deg) {
    glm::mat4 m(1.0f);
    m = glm::rotate(m, glm::radians(deg.z), {0, 0, 1});
    m = glm::rotate(m, glm::radians(deg.y), {0, 1, 0});
    m = glm::rotate(m, glm::radians(deg.x), {1, 0, 0});
    return m;
}

glm::vec3 worldPosition(const SceneNode* n) { return glm::vec3(n->worldMatrix()[3]); }

void setWorldPosition(lua_State* L, SceneNode* n, const glm::vec3& p) {
    // Moving the character's HumanoidRootPart teleports the whole character.
    if (n->parent && isCharacterRoot(L, n->parent) && n->name == "HumanoidRootPart") {
        n->parent->transform.position += p - worldPosition(n);
        return;
    }
    glm::vec3 local = p;
    if (n->parent) local = glm::vec3(glm::inverse(n->parent->worldMatrix()) * glm::vec4(p, 1.0f));
    n->transform.position = local;
}

glm::mat4 getCFrame(const SceneNode* n) {
    glm::mat4 m = rotationMatrix(n->transform.rotation);
    m[3] = glm::vec4(worldPosition(n), 1.0f);
    return m;
}

void setCFrame(lua_State* L, SceneNode* n, const glm::mat4& cf) {
    glm::mat3 r(cf);
    for (int i = 0; i < 3; ++i) r[i] = glm::normalize(r[i]);
    float z, y, x;
    glm::extractEulerAngleZYX(glm::mat4(r), z, y, x);
    n->transform.rotation = glm::degrees(glm::vec3(x, y, z));
    setWorldPosition(L, n, glm::vec3(cf[3]));
}

// ===========================================================================
// Instance methods
// ===========================================================================

int m_FindFirstChild(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string name = luaL_checkstring(L, 2);
    bool recursive = lua_toboolean(L, 3);
    if (name == "Humanoid" && isCharacterRoot(L, n)) { LuaApi::pushHumanoid(L, n->id); return 1; }
    SceneNode* c = n->findChild(name, recursive);
    LuaApi::pushInstance(L, c ? c->id : 0);
    return 1;
}

int m_FindFirstChildOfClass(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string cls = luaL_checkstring(L, 2);
    if (cls == "Humanoid" && isCharacterRoot(L, n)) { LuaApi::pushHumanoid(L, n->id); return 1; }
    for (auto& c : n->children)
        if (!c->internal && isA(L, c.get(), cls)) { LuaApi::pushInstance(L, c->id); return 1; }
    lua_pushnil(L);
    return 1;
}

int m_WaitForChild(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string name = luaL_checkstring(L, 2);
    if (name == "Humanoid" && isCharacterRoot(L, n)) { LuaApi::pushHumanoid(L, n->id); return 1; }
    SceneNode* c = n->findChild(name);
    if (!c) return luaL_error(L, "'%s' has no child called '%s'", n->name.c_str(), name.c_str());
    LuaApi::pushInstance(L, c->id);
    return 1;
}

int m_GetChildren(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_newtable(L);
    int i = 1;
    for (auto& c : n->children) {
        if (c->internal) continue;
        LuaApi::pushInstance(L, c->id);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

void collectDescendants(lua_State* L, SceneNode* n, int& i) {
    for (auto& c : n->children) {
        if (c->internal) continue;
        LuaApi::pushInstance(L, c->id);
        lua_rawseti(L, -2, i++);
        collectDescendants(L, c.get(), i);
    }
}
int m_GetDescendants(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_newtable(L);
    int i = 1;
    collectDescendants(L, n, i);
    return 1;
}

int m_Destroy(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string err;
    if (!E(L)->destroy(n, err)) return luaL_error(L, "%s", err.c_str());
    return 0;
}

int m_ClearAllChildren(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::vector<SceneNode*> kids;
    for (auto& c : n->children) if (!c->internal) kids.push_back(c.get());
    std::string err;
    for (auto* k : kids) E(L)->destroy(k, err);
    return 0;
}

int m_Clone(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (isCharacterRoot(L, n) || n == E(L)->scene()->root())
        return luaL_error(L, "You can't Clone %s", n->name.c_str());
    SceneNode* copy = E(L)->adopt(Serializer::clone(*n));
    LuaApi::pushInstance(L, copy->id);
    return 1;
}

// part:ApplyImpulse(Vector3) / part:ApplyAngularImpulse(Vector3)
float partMass(const SceneNode* n) {
    glm::vec3 s(glm::length(glm::vec3(n->worldMatrix()[0])), glm::length(glm::vec3(n->worldMatrix()[1])),
                glm::length(glm::vec3(n->worldMatrix()[2])));
    float d = n->density >= 0 ? n->density : 1.0f;
    return std::max(0.001f, d * s.x * s.y * s.z);
}
int m_ApplyImpulse(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (!n->isPart()) return 0;
    n->velocity += LuaApi::checkVector3(L, 2) / partMass(n);
    n->sleepTime = 0;
    return 0;
}
int m_ApplyAngularImpulse(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (!n->isPart()) return 0;
    n->angularVelocity += LuaApi::checkVector3(L, 2) / partMass(n);
    n->sleepTime = 0;
    return 0;
}

// sound:Play() / sound:Stop()
void startSound(lua_State* L, SceneNode* n) {
    Audio::stop(n->audioHandle);
    bool in3d = n->parent && n->parent->isPart();
    glm::vec3 at = in3d ? glm::vec3(n->parent->worldMatrix()[3]) : glm::vec3(0.0f);
    n->audioHandle = Audio::play(n->soundId, n->volume, n->pitch, n->looped, in3d ? &at : nullptr);
    if (!n->looped) E(L)->scene()->pushFx(FxEvent::Sound, at, n->volume, n->soundId);   // multiplayer
}
int m_Play(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (!n->isSound()) return luaL_error(L, "Play only works on Sound objects");
    startSound(L, n);
    return 0;
}
int m_Stop(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (n->isSound()) { Audio::stop(n->audioHandle); n->audioHandle = 0; }
    return 0;
}

// character:BreakJoints() — kill the character violently.
int m_BreakJoints(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    if (isCharacterRoot(L, n)) E(L)->scene()->killCharacter(n->id, 1.0f, glm::vec3(0, 4, 0));
    return 0;
}

int m_IsA(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_pushboolean(L, isA(L, n, luaL_checkstring(L, 2)));
    return 1;
}

int m_IsDescendantOf(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    SceneNode* other = LuaApi::checkNode(L, 2);
    lua_pushboolean(L, other->isAncestorOf(n));
    return 1;
}

int m_GetFullName(lua_State* L) {
    lua_pushstring(L, LuaApi::checkNode(L, 1)->fullName().c_str());
    return 1;
}

int m_GetPivot(lua_State* L) {
    LuaApi::pushCFrame(L, getCFrame(LuaApi::checkNode(L, 1)));
    return 1;
}
int m_PivotTo(lua_State* L) {
    setCFrame(L, LuaApi::checkNode(L, 1), LuaApi::checkCFrame(L, 2));
    return 0;
}

// --- Attributes: obj:SetAttribute("Coins", 5), obj:GetAttribute("Coins") ---

void pushAttribute(lua_State* L, const Attribute& a) {
    switch (a.type) {
        case Attribute::Bool:    lua_pushboolean(L, a.b); break;
        case Attribute::Number:   // whole numbers come back as integers (5, not 5.0)
            if (a.n == std::floor(a.n) && std::fabs(a.n) < 9e15) lua_pushinteger(L, (lua_Integer)a.n);
            else lua_pushnumber(L, a.n);
            break;
        case Attribute::String:  lua_pushstring(L, a.s.c_str()); break;
        case Attribute::Vector3: LuaApi::pushVector3(L, a.v); break;
        case Attribute::Color3:  LuaApi::pushColor3(L, a.v); break;
    }
}

int m_GetAttribute(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    const Attribute* a = n->findAttribute(luaL_checkstring(L, 2));
    if (a) pushAttribute(L, *a); else lua_pushnil(L);
    return 1;
}

int m_GetAttributes(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_newtable(L);
    for (const auto& a : n->attributes) { pushAttribute(L, a); lua_setfield(L, -2, a.name.c_str()); }
    return 1;
}

int m_SetAttribute(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string name = luaL_checkstring(L, 2);
    if (name.empty() || name.size() > 100 || name.rfind("RBX", 0) == 0)
        return luaL_error(L, "'%s' can't be used as an attribute name", name.c_str());
    for (char c : name)
        if (!std::isalnum((unsigned char)c) && c != '_')
            return luaL_error(L, "Attribute names can only use letters, numbers and _ ('%s')", name.c_str());

    auto it = std::find_if(n->attributes.begin(), n->attributes.end(), [&](const Attribute& a) { return a.name == name; });
    if (lua_isnoneornil(L, 3)) {                 // SetAttribute(name, nil) removes it
        if (it == n->attributes.end()) return 0;
        n->attributes.erase(it);
        E(L)->fireAttributeChanged(n->id, name);
        return 0;
    }
    Attribute v;
    v.name = name;
    if (lua_type(L, 3) == LUA_TBOOLEAN)      { v.type = Attribute::Bool;   v.b = lua_toboolean(L, 3); }
    else if (lua_type(L, 3) == LUA_TNUMBER)  { v.type = Attribute::Number; v.n = lua_tonumber(L, 3); }
    else if (lua_type(L, 3) == LUA_TSTRING)  { v.type = Attribute::String; v.s = lua_tostring(L, 3); }
    else if (glm::vec3* p = LuaApi::toVector3(L, 3)) { v.type = Attribute::Vector3; v.v = *p; }
    else if (glm::vec3* c = LuaApi::toColor3(L, 3))  { v.type = Attribute::Color3;  v.v = *c; }
    else return luaL_error(L, "Attributes can hold true/false, numbers, text, Vector3 or Color3 (got %s)",
                           luaL_typename(L, 3));
    if (it != n->attributes.end()) {
        const Attribute& o = *it;
        bool same = o.type == v.type && o.b == v.b && o.n == v.n && o.s == v.s && o.v == v.v;
        *it = v;
        if (same) return 0;
    } else {
        n->attributes.push_back(v);
    }
    E(L)->fireAttributeChanged(n->id, name);
    return 0;
}

int m_GetAttributeChangedSignal(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    const char* name = luaL_checkstring(L, 2);
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.attrSignal");
    LuaApi::pushSignal(L, SignalKind::AttributeChanged, n->id);
    lua_pushstring(L, name);
    lua_call(L, 2, 1);
    return 1;
}

// --- Tags: obj:AddTag("Enemy"), obj:HasTag("Enemy") ---

int m_HasTag(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string tag = luaL_checkstring(L, 2);
    lua_pushboolean(L, std::find(n->tags.begin(), n->tags.end(), tag) != n->tags.end());
    return 1;
}
int m_AddTag(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string tag = luaL_checkstring(L, 2);
    if (std::find(n->tags.begin(), n->tags.end(), tag) != n->tags.end()) return 0;
    n->tags.push_back(tag);
    E(L)->fireTag(true, n->id, tag);
    return 0;
}
int m_RemoveTag(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string tag = luaL_checkstring(L, 2);
    auto it = std::find(n->tags.begin(), n->tags.end(), tag);
    if (it == n->tags.end()) return 0;
    n->tags.erase(it);
    E(L)->fireTag(false, n->id, tag);
    return 0;
}
int m_GetTags(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    lua_newtable(L);
    for (size_t i = 0; i < n->tags.size(); ++i) { lua_pushstring(L, n->tags[i].c_str()); lua_rawseti(L, -2, (int)i + 1); }
    return 1;
}

const luaL_Reg kMethods[] = {
    {"FindFirstChild", m_FindFirstChild}, {"FindFirstChildOfClass", m_FindFirstChildOfClass},
    {"WaitForChild", m_WaitForChild}, {"GetChildren", m_GetChildren},
    {"GetDescendants", m_GetDescendants}, {"Destroy", m_Destroy}, {"Remove", m_Destroy},
    {"ClearAllChildren", m_ClearAllChildren}, {"Clone", m_Clone}, {"IsA", m_IsA},
    {"IsDescendantOf", m_IsDescendantOf}, {"GetFullName", m_GetFullName},
    {"GetPivot", m_GetPivot}, {"PivotTo", m_PivotTo}, {"BreakJoints", m_BreakJoints},
    {"Play", m_Play}, {"Stop", m_Stop},
    {"ApplyImpulse", m_ApplyImpulse}, {"ApplyAngularImpulse", m_ApplyAngularImpulse},
    {"GetAttribute", m_GetAttribute}, {"SetAttribute", m_SetAttribute}, {"GetAttributes", m_GetAttributes},
    {"GetAttributeChangedSignal", m_GetAttributeChangedSignal},
    {"HasTag", m_HasTag}, {"AddTag", m_AddTag}, {"RemoveTag", m_RemoveTag}, {"GetTags", m_GetTags},
    {nullptr, nullptr}};

// ===========================================================================
// Instance properties
// ===========================================================================

int inst_index(lua_State* L) {
    auto* ref = static_cast<InstRef*>(luaL_checkudata(L, 1, kInst));
    const char* k = luaL_checkstring(L, 2);

    // Methods first (they work the same for every object).
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");
    if (lua_getfield(L, -1, k) != LUA_TNIL) return 1;
    lua_pop(L, 2);

    SceneNode* n = E(L)->resolve(ref->id);
    if (!n) {
        if (is(k, "Parent")) { lua_pushnil(L); return 1; }
        return luaL_error(L, "Tried to read '%s' of an object that has been destroyed", k);
    }
    bool part = n->kind == NodeKind::Part;

    if (is(k, "Name"))      { lua_pushstring(L, n->name.c_str()); return 1; }
    if (is(k, "ClassName")) { lua_pushstring(L, className(L, n)); return 1; }
    if (is(k, "AttributeChanged")) { LuaApi::pushSignal(L, SignalKind::AttributeChanged, n->id); return 1; }
    if (n->isAttachment()) {
        if (is(k, "Position"))      { LuaApi::pushVector3(L, n->transform.position); return 1; }
        if (is(k, "WorldPosition")) { LuaApi::pushVector3(L, worldPosition(n)); return 1; }
    }
    if (n->isConstraint()) {
        bool weld = n->constraintType == ConstraintType::Weld;
        if ((!weld && is(k, "Attachment0")) || (weld && is(k, "Part0"))) { LuaApi::pushInstance(L, n->ref0); return 1; }
        if ((!weld && is(k, "Attachment1")) || (weld && is(k, "Part1"))) { LuaApi::pushInstance(L, n->ref1); return 1; }
        if (is(k, "Length") || is(k, "FreeLength")) { lua_pushnumber(L, n->length); return 1; }
        if (is(k, "Stiffness"))      { lua_pushnumber(L, n->stiffness); return 1; }
        if (is(k, "Damping"))        { lua_pushnumber(L, n->damping); return 1; }
        if (is(k, "AngularVelocity")){ lua_pushnumber(L, n->motorSpeed); return 1; }
        if (is(k, "MotorMaxTorque")) { lua_pushnumber(L, n->motorTorque); return 1; }
        if (is(k, "ActuatorType"))   { lua_pushstring(L, n->motorTorque > 0 ? "Motor" : "None"); return 1; }
        if (is(k, "Thickness"))      { lua_pushnumber(L, n->thickness); return 1; }
        if (is(k, "Color"))          { LuaApi::pushColor3(L, n->color); return 1; }
        if (is(k, "Enabled"))        { lua_pushboolean(L, n->enabled); return 1; }
    }
    if (is(k, "Parent"))    { LuaApi::pushInstance(L, n->parent ? n->parent->id : 0); return 1; }
    if (is(k, "Position"))  { LuaApi::pushVector3(L, worldPosition(n)); return 1; }
    if (is(k, "Orientation") || is(k, "Rotation")) { LuaApi::pushVector3(L, n->transform.rotation); return 1; }
    if (is(k, "Size"))      { LuaApi::pushVector3(L, n->transform.scale); return 1; }
    if (is(k, "CFrame"))    { LuaApi::pushCFrame(L, getCFrame(n)); return 1; }
    if (is(k, "Visible"))   { lua_pushboolean(L, n->visible); return 1; }

    bool hrp = n->parent && isCharacterRoot(L, n->parent) && n->name == "HumanoidRootPart";
    if (hrp && (is(k, "Velocity") || is(k, "AssemblyLinearVelocity"))) {
        Player* me = E(L)->scene()->player();
        LuaApi::pushVector3(L, me && n->parent->id == me->rootId() ? me->velocity() : glm::vec3(0.0f));
        return 1;
    }

    if (part) {
        if (is(k, "Color"))        { LuaApi::pushColor3(L, n->color); return 1; }
        if (is(k, "Transparency")) { lua_pushnumber(L, n->transparency); return 1; }
        if (is(k, "Anchored"))     { lua_pushboolean(L, n->anchored); return 1; }
        if (is(k, "CanCollide"))   { lua_pushboolean(L, n->canCollide); return 1; }
        if (is(k, "CastShadow"))   { lua_pushboolean(L, n->castShadow); return 1; }
        if (is(k, "Material"))     { lua_pushstring(L, kMaterialNames[(int)n->material]); return 1; }
        if (is(k, "Shape"))        { lua_pushstring(L, shapeName(n->primitiveType)); return 1; }
        if (is(k, "Velocity") || is(k, "AssemblyLinearVelocity")) { LuaApi::pushVector3(L, n->velocity); return 1; }
        if (is(k, "RotVelocity") || is(k, "AssemblyAngularVelocity")) { LuaApi::pushVector3(L, n->angularVelocity); return 1; }
        if (is(k, "Density"))    { lua_pushnumber(L, n->density); return 1; }
        if (is(k, "Friction"))   { lua_pushnumber(L, n->friction); return 1; }
        if (is(k, "Elasticity")) { lua_pushnumber(L, n->elasticity); return 1; }
        if (is(k, "Touched"))      { LuaApi::pushSignal(L, SignalKind::Touched, n->id); return 1; }
        if (is(k, "Clicked"))      { LuaApi::pushSignal(L, SignalKind::Clicked, n->id); return 1; }
    }
    if (n->isValue()) {
        if (is(k, "Value"))   { LuaApi::pushValue(L, *n); return 1; }
        if (is(k, "Changed")) { LuaApi::pushSignal(L, SignalKind::Changed, n->id); return 1; }
    }
    if (n->isTool()) {
        if (is(k, "Enabled"))        { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "ToolTip"))        { lua_pushstring(L, n->toolTip.c_str()); return 1; }
        if (is(k, "CanBeDropped"))   { lua_pushboolean(L, n->canBeDropped); return 1; }
        if (is(k, "RequiresHandle")) { lua_pushboolean(L, true); return 1; }
        if (is(k, "GripPos"))        { LuaApi::pushVector3(L, n->gripPos); return 1; }
        if (is(k, "Activated"))      { LuaApi::pushSignal(L, SignalKind::Activated, n->id); return 1; }
        if (is(k, "Deactivated"))    { LuaApi::pushSignal(L, SignalKind::Deactivated, n->id); return 1; }
        if (is(k, "Equipped"))       { LuaApi::pushSignal(L, SignalKind::Equipped, n->id); return 1; }
        if (is(k, "Unequipped"))     { LuaApi::pushSignal(L, SignalKind::Unequipped, n->id); return 1; }
    }
    if (n->kind == NodeKind::Script) {
        if (is(k, "Enabled"))  { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "Disabled")) { lua_pushboolean(L, !n->enabled); return 1; }
        if (is(k, "Source"))   { lua_pushstring(L, n->source.c_str()); return 1; }
    }
    if (n->isSound()) {
        if (is(k, "SoundId"))       { lua_pushstring(L, n->soundId.c_str()); return 1; }
        if (is(k, "Volume"))        { lua_pushnumber(L, n->volume); return 1; }
        if (is(k, "PlaybackSpeed") || is(k, "Pitch")) { lua_pushnumber(L, n->pitch); return 1; }
        if (is(k, "Looped"))        { lua_pushboolean(L, n->looped); return 1; }
        if (is(k, "Playing") || is(k, "IsPlaying")) { lua_pushboolean(L, Audio::isPlaying(n->audioHandle)); return 1; }
    }
    if (n->isLight()) {
        if (is(k, "Enabled"))    { lua_pushboolean(L, n->enabled); return 1; }
        if (is(k, "Brightness")) { lua_pushnumber(L, n->brightness); return 1; }
        if (is(k, "Range"))      { lua_pushnumber(L, n->range); return 1; }
        if (is(k, "Angle"))      { lua_pushnumber(L, n->spotAngle); return 1; }
        if (is(k, "Color"))      { LuaApi::pushColor3(L, n->color); return 1; }
    }
    if (n == E(L)->scene()->root()) {
        const WorldSettings& w = E(L)->scene()->world();
        if (is(k, "Gravity"))    { lua_pushnumber(L, w.gravity); return 1; }
        if (is(k, "DeathStyle")) { lua_pushstring(L, w.deathStyle == DeathStyle::Ragdoll ? "Ragdoll" : "Classic"); return 1; }
        if (is(k, "Gore"))       { lua_pushstring(L, w.gore == GoreLevel::Blood ? "Blood" : w.gore == GoreLevel::OilAndBolts ? "Oil" : "Off"); return 1; }
        if (is(k, "FallDamage")) { lua_pushboolean(L, w.fallDamage); return 1; }
    }
    if (is(k, "Humanoid") && isCharacterRoot(L, n)) { LuaApi::pushHumanoid(L, n->id); return 1; }

    // Like Roblox, `workspace.Door` finds a child called "Door".
    if (SceneNode* c = n->findChild(k)) { LuaApi::pushInstance(L, c->id); return 1; }
    return luaL_error(L, "'%s' is not a valid member of %s \"%s\"", k, className(L, n), n->name.c_str());
}

int inst_newindex(lua_State* L) {
    auto* ref = static_cast<InstRef*>(luaL_checkudata(L, 1, kInst));
    const char* k = luaL_checkstring(L, 2);
    SceneNode* n = E(L)->resolve(ref->id);
    if (!n) return luaL_error(L, "Tried to set '%s' of an object that has been destroyed", k);
    bool part = n->kind == NodeKind::Part;
    Scene* scene = E(L)->scene();

    if (n->isAttachment()) {
        if (is(k, "Position"))      { n->transform.position = LuaApi::checkVector3(L, 3); return 0; }
        if (is(k, "WorldPosition")) { setWorldPosition(L, n, LuaApi::checkVector3(L, 3)); return 0; }
    }
    if (n->isConstraint()) {
        bool weld = n->constraintType == ConstraintType::Weld;
        auto ref = [&](uint64_t& r) {
            SceneNode* t = lua_isnil(L, 3) ? nullptr : LuaApi::checkNode(L, 3);
            r = t ? t->id : 0;
            n->jointReady = false;
        };
        if ((!weld && is(k, "Attachment0")) || (weld && is(k, "Part0"))) { ref(n->ref0); return 0; }
        if ((!weld && is(k, "Attachment1")) || (weld && is(k, "Part1"))) { ref(n->ref1); return 0; }
        if (is(k, "Length") || is(k, "FreeLength")) { n->length = (float)luaL_checknumber(L, 3); return 0; }
        if (is(k, "Stiffness"))      { n->stiffness = (float)luaL_checknumber(L, 3); return 0; }
        if (is(k, "Damping"))        { n->damping = (float)luaL_checknumber(L, 3); return 0; }
        if (is(k, "AngularVelocity")){ n->motorSpeed = (float)luaL_checknumber(L, 3); return 0; }
        if (is(k, "MotorMaxTorque")) { n->motorTorque = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "ActuatorType")) {
            bool motor = std::string(luaL_checkstring(L, 3)) == "Motor";
            n->motorTorque = motor ? (n->motorTorque > 0 ? n->motorTorque : 2000.0f) : 0.0f;
            return 0;
        }
        if (is(k, "Thickness"))      { n->thickness = (float)luaL_checknumber(L, 3); return 0; }
        if (is(k, "Color"))          { n->color = LuaApi::checkColor3(L, 3); return 0; }
        if (is(k, "Enabled"))        { n->enabled = lua_toboolean(L, 3); return 0; }
    }
    if (is(k, "Name")) {
        if (scene->isProtected(n)) return luaL_error(L, "You can't rename %s", n->name.c_str());
        n->name = luaL_checkstring(L, 3);
        return 0;
    }
    if (is(k, "Parent")) {
        SceneNode* p = lua_isnil(L, 3) ? nullptr : LuaApi::checkNode(L, 3);
        std::string err;
        if (!E(L)->setParent(n, p, err)) return luaL_error(L, "%s", err.c_str());
        return 0;
    }
    if (is(k, "Position"))  { setWorldPosition(L, n, LuaApi::checkVector3(L, 3)); return 0; }
    if (is(k, "Orientation") || is(k, "Rotation")) { n->transform.rotation = LuaApi::checkVector3(L, 3); return 0; }
    if (is(k, "Size"))      { n->transform.scale = glm::max(LuaApi::checkVector3(L, 3), glm::vec3(0.001f)); return 0; }
    if (is(k, "CFrame"))    { setCFrame(L, n, LuaApi::checkCFrame(L, 3)); return 0; }
    if (is(k, "Visible"))   { n->visible = lua_toboolean(L, 3); return 0; }

    // Setting the HumanoidRootPart's velocity launches the character.
    bool hrp = n->parent && isCharacterRoot(L, n->parent) && n->name == "HumanoidRootPart";
    if (hrp && (is(k, "Velocity") || is(k, "AssemblyLinearVelocity"))) {
        Player* me = scene->player();
        if (me && n->parent->id == me->rootId()) me->launch(LuaApi::checkVector3(L, 3));
        else if (RemoteCharacter* rc = scene->findRemote(n->parent->id))
            rc->kills.push_back({-1.0f, LuaApi::checkVector3(L, 3)});   // force < 0 = just a push
        return 0;
    }

    if (part) {
        if (is(k, "Color"))        { n->color = LuaApi::checkColor3(L, 3); return 0; }
        if (is(k, "Transparency")) { n->transparency = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, 1.0f); return 0; }
        if (is(k, "Anchored"))     { n->anchored = lua_toboolean(L, 3); if (n->anchored) n->velocity = glm::vec3(0.0f); return 0; }
        if (is(k, "CanCollide"))   { n->canCollide = lua_toboolean(L, 3); return 0; }
        if (is(k, "CastShadow"))   { n->castShadow = lua_toboolean(L, 3); return 0; }
        if (is(k, "Material")) {
            if (!parseMaterial(luaL_checkstring(L, 3), n->material))
                return luaL_error(L, "Unknown material '%s' (try Plastic, Metal, Neon, Wood, Glass, Concrete or Ice)", lua_tostring(L, 3));
            return 0;
        }
        if (is(k, "Shape")) {
            PrimitiveType t;
            if (!parseShape(luaL_checkstring(L, 3), t))
                return luaL_error(L, "Unknown shape '%s' (try Block, Ball, Cylinder or Plane)", lua_tostring(L, 3));
            n->primitiveType = t;
            n->mesh = MeshLibrary::get(t);
            return 0;
        }
        if (is(k, "Velocity") || is(k, "AssemblyLinearVelocity")) { n->velocity = LuaApi::checkVector3(L, 3); n->sleepTime = 0; return 0; }
        if (is(k, "RotVelocity") || is(k, "AssemblyAngularVelocity")) { n->angularVelocity = LuaApi::checkVector3(L, 3); n->sleepTime = 0; return 0; }
        if (is(k, "Density"))    { n->density = std::max(0.01f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "Friction"))   { n->friction = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "Elasticity")) { n->elasticity = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, 1.0f); return 0; }
    }
    if (n->kind == NodeKind::Script) {
        if (is(k, "Enabled"))  { LuaApi::engine(L)->setScriptEnabled(n, lua_toboolean(L, 3)); return 0; }
        if (is(k, "Disabled")) { LuaApi::engine(L)->setScriptEnabled(n, !lua_toboolean(L, 3)); return 0; }
    }
    if (n->isSound()) {
        if (is(k, "SoundId"))  { n->soundId = luaL_checkstring(L, 3); return 0; }
        if (is(k, "Volume"))   { n->volume = std::max(0.0f, (float)luaL_checknumber(L, 3)); Audio::setVolume(n->audioHandle, n->volume); return 0; }
        if (is(k, "PlaybackSpeed") || is(k, "Pitch")) { n->pitch = std::max(0.05f, (float)luaL_checknumber(L, 3)); Audio::setPitch(n->audioHandle, n->pitch); return 0; }
        if (is(k, "Looped"))   { n->looped = lua_toboolean(L, 3); return 0; }
        if (is(k, "Playing"))  {
            if (lua_toboolean(L, 3)) startSound(L, n);
            else { Audio::stop(n->audioHandle); n->audioHandle = 0; }
            return 0;
        }
    }
    if (n->isValue() && is(k, "Value")) {
        Attribute& v = n->value;
        switch (v.type) {
            case Attribute::Bool:   v.b = lua_toboolean(L, 3); break;
            case Attribute::String: v.s = luaL_tolstring(L, 3, nullptr); lua_pop(L, 1); break;
            case Attribute::Vector3: v.v = LuaApi::checkVector3(L, 3); break;
            case Attribute::Color3: v.v = LuaApi::checkColor3(L, 3); break;
            default: {
                double d = luaL_checknumber(L, 3);
                v.n = n->intValue ? (double)(long long)std::llround(d) : d;
            }
        }
        LuaApi::engine(L)->fireValueChanged(n->id);
        return 0;
    }
    if (n->isTool()) {
        if (is(k, "Enabled"))      { n->enabled = lua_toboolean(L, 3); return 0; }
        if (is(k, "ToolTip"))      { n->toolTip = luaL_checkstring(L, 3); return 0; }
        if (is(k, "CanBeDropped")) { n->canBeDropped = lua_toboolean(L, 3); return 0; }
        if (is(k, "GripPos"))      { n->gripPos = LuaApi::checkVector3(L, 3); return 0; }
    }
    if (n->isLight()) {
        if (is(k, "Enabled"))    { n->enabled = lua_toboolean(L, 3); return 0; }
        if (is(k, "Brightness")) { n->brightness = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "Range"))      { n->range = std::max(0.1f, (float)luaL_checknumber(L, 3)); return 0; }
        if (is(k, "Angle"))      { n->spotAngle = glm::clamp((float)luaL_checknumber(L, 3), 1.0f, 179.0f); return 0; }
        if (is(k, "Color"))      { n->color = LuaApi::checkColor3(L, 3); return 0; }
    }
    if (n == scene->root()) {
        WorldSettings& w = scene->world();
        if (is(k, "Gravity"))    { w.gravity = (float)luaL_checknumber(L, 3); return 0; }
        if (is(k, "DeathStyle")) { w.deathStyle = std::string(luaL_checkstring(L, 3)) == "Classic" ? DeathStyle::Classic : DeathStyle::Ragdoll; return 0; }
        if (is(k, "Gore")) {
            std::string g = luaL_checkstring(L, 3);
            w.gore = g == "Blood" ? GoreLevel::Blood : g == "Off" ? GoreLevel::Off : GoreLevel::OilAndBolts;
            return 0;
        }
        if (is(k, "FallDamage")) { w.fallDamage = lua_toboolean(L, 3); return 0; }
    }
    return luaL_error(L, "'%s' can't be set on %s \"%s\"", k, className(L, n), n->name.c_str());
}

int inst_eq(lua_State* L) {
    auto* a = static_cast<InstRef*>(luaL_testudata(L, 1, kInst));
    auto* b = static_cast<InstRef*>(luaL_testudata(L, 2, kInst));
    lua_pushboolean(L, a && b && a->id == b->id);
    return 1;
}

int inst_tostring(lua_State* L) {
    auto* ref = static_cast<InstRef*>(luaL_checkudata(L, 1, kInst));
    SceneNode* n = E(L)->resolve(ref->id);
    lua_pushstring(L, n ? n->name.c_str() : "(destroyed)");
    return 1;
}

// Instance.new("Part", parent)
int inst_new(lua_State* L) {
    std::string cls = luaL_checkstring(L, 1);
    std::unique_ptr<SceneNode> n;
    PrimitiveType shape;
    if (cls == "Model" || cls == "Folder") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Model);
    } else if (cls == "Script") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Script);
    } else if (cls == "Attachment") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Attachment);
    } else if (cls == "RopeConstraint" || cls == "RodConstraint" || cls == "SpringConstraint" ||
               cls == "WeldConstraint" || cls == "HingeConstraint") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Constraint);
        n->constraintType = cls == "RodConstraint"    ? ConstraintType::Rod
                          : cls == "SpringConstraint" ? ConstraintType::Spring
                          : cls == "WeldConstraint"   ? ConstraintType::Weld
                          : cls == "HingeConstraint"  ? ConstraintType::Hinge : ConstraintType::Rope;
        n->color = n->constraintType == ConstraintType::Rope ? glm::vec3(0.45f, 0.32f, 0.2f) : glm::vec3(0.6f);
    } else if (cls == "Sound") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Sound);
    } else if (cls == "ForceField") {
        n = std::make_unique<SceneNode>(cls, NodeKind::ForceField);
    } else if (cls == "Tool") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Tool);
    } else if (cls == "IntValue" || cls == "NumberValue" || cls == "StringValue" || cls == "BoolValue" ||
               cls == "Vector3Value" || cls == "Color3Value") {
        n = std::make_unique<SceneNode>("Value", NodeKind::Value);
        n->intValue = cls == "IntValue";
        n->value.type = cls == "StringValue" ? Attribute::String : cls == "BoolValue" ? Attribute::Bool
                      : cls == "Vector3Value" ? Attribute::Vector3 : cls == "Color3Value" ? Attribute::Color3 : Attribute::Number;
    } else if (cls == "PointLight" || cls == "SpotLight") {
        n = std::make_unique<SceneNode>(cls, NodeKind::Light);
        n->lightType = cls == "SpotLight" ? LightType::Spot : LightType::Point;
        n->color = {1.0f, 1.0f, 1.0f};
    } else if (parseShape(cls, shape) || cls == "SpawnLocation" || cls == "WedgePart" || cls == "TrussPart") {
        if (cls == "SpawnLocation" || cls == "WedgePart" || cls == "TrussPart") shape = PrimitiveType::Cube;
        n = std::make_unique<SceneNode>(cls == "Block" || cls == "Cube" ? "Part" : cls);
        n->primitiveType = shape;
        n->mesh     = MeshLibrary::get(shape);
        n->anchored = false;                    // new parts fall, like in Roblox
        n->color    = {0.64f, 0.64f, 0.66f};
        n->transform.position = {0.0f, 0.5f, 0.0f};
    } else {
        return luaL_error(L, "Instance.new: unknown class '%s' (try \"Part\", \"Ball\", "
                             "\"Cylinder\", \"Model\", \"Folder\" or \"PointLight\")", cls.c_str());
    }
    SceneNode* raw = E(L)->adopt(std::move(n));
    if (!lua_isnoneornil(L, 2)) {
        std::string err;
        if (!E(L)->setParent(raw, LuaApi::checkNode(L, 2), err)) return luaL_error(L, "%s", err.c_str());
    }
    LuaApi::pushInstance(L, raw->id);
    return 1;
}

// ===========================================================================
// Signals & connections
// ===========================================================================

int sig_connect_impl(lua_State* L, bool once) {
    auto* s = static_cast<SignalRef*>(luaL_checkudata(L, 1, kSig));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int fnRef = luaL_ref(L, LUA_REGISTRYINDEX);
    int idx = E(L)->connect(s->kind, s->id, fnRef, once);
    auto* c = static_cast<ConnRef*>(lua_newuserdatauv(L, sizeof(ConnRef), 0));
    c->index = idx;
    luaL_setmetatable(L, kConn);
    return 1;
}
int sig_connect(lua_State* L) { return sig_connect_impl(L, false); }
int sig_once   (lua_State* L) { return sig_connect_impl(L, true); }

int sig_wait(lua_State* L) {
    auto* s = static_cast<SignalRef*>(luaL_checkudata(L, 1, kSig));
    if (!lua_isyieldable(L)) return luaL_error(L, ":Wait() can't be used here");
    E(L)->connectWaiter(s->kind, s->id, L);
    lua_settop(L, 0);
    return lua_yield(L, 0);
}

int sig_index(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    if (is(k, "Connect") || is(k, "connect")) { lua_pushcfunction(L, sig_connect); return 1; }
    if (is(k, "Once"))                        { lua_pushcfunction(L, sig_once);    return 1; }
    if (is(k, "Wait"))                        { lua_pushcfunction(L, sig_wait);    return 1; }
    return luaL_error(L, "'%s' is not a valid member of an event (try :Connect)", k);
}

int conn_disconnect(lua_State* L) {
    auto* c = static_cast<ConnRef*>(luaL_checkudata(L, 1, kConn));
    E(L)->disconnect(c->index);
    return 0;
}
int conn_index(lua_State* L) {
    auto* c = static_cast<ConnRef*>(luaL_checkudata(L, 1, kConn));
    const char* k = luaL_checkstring(L, 2);
    if (is(k, "Disconnect") || is(k, "disconnect")) { lua_pushcfunction(L, conn_disconnect); return 1; }
    if (is(k, "Connected")) { lua_pushboolean(L, E(L)->connected(c->index)); return 1; }
    return luaL_error(L, "'%s' is not a valid member of a connection", k);
}

// ===========================================================================
// Humanoid (the character's health / speed)
// ===========================================================================

struct HumRef { uint64_t rootId; };

uint64_t humRoot(lua_State* L) {
    return static_cast<HumRef*>(luaL_checkudata(L, 1, kHum))->rootId;
}

Humanoid& hum(lua_State* L) {
    Humanoid* h = E(L)->scene()->humanoidOf(humRoot(L));
    if (!h) luaL_error(L, "This character has left the game");
    return *h;
}

void touched(lua_State* L) { E(L)->scene()->markHumanoidEdited(humRoot(L), E(L)->time()); }

int hum_takeDamage(lua_State* L) {
    Humanoid& h = hum(L);
    // Like Roblox: TakeDamage does nothing while the character has a ForceField.
    if (SceneNode* r = E(L)->scene()->findById(humRoot(L)); r && r->hasForceField()) return 0;
    h.health = glm::clamp(h.health - (float)luaL_checknumber(L, 2), 0.0f, h.maxHealth);
    touched(L);
    return 0;
}

int hum_breakJoints(lua_State* L) {
    E(L)->scene()->killCharacter(humRoot(L), 1.0f, glm::vec3(0, 4, 0));
    return 0;
}

// humanoid:EquipTool(tool) / humanoid:UnequipTools() (your own character only)
Player* toolPlayer(lua_State* L) {
    Player* p = E(L)->scene()->player();
    return p && p->rootId() == humRoot(L) ? p : nullptr;
}
int hum_equipTool(lua_State* L) {
    SceneNode* tool = LuaApi::checkNode(L, 2);
    if (Player* p = toolPlayer(L); p && tool->isTool()) p->equip(tool->id);
    return 0;
}
int hum_unequipTools(lua_State* L) {
    if (Player* p = toolPlayer(L)) p->equip(0);
    return 0;
}

int hum_isA(lua_State* L) {
    const char* c = luaL_checkstring(L, 2);
    lua_pushboolean(L, is(c, "Humanoid") || is(c, "Instance"));
    return 1;
}

int hum_index(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    Humanoid& h = hum(L);
    if (is(k, "Health"))     { lua_pushnumber(L, h.health);     return 1; }
    if (is(k, "MaxHealth"))  { lua_pushnumber(L, h.maxHealth);  return 1; }
    if (is(k, "WalkSpeed"))  { lua_pushnumber(L, h.walkSpeed);  return 1; }
    if (is(k, "JumpPower"))  { lua_pushnumber(L, h.jumpPower);  return 1; }
    if (is(k, "AutoRotate")) { lua_pushboolean(L, h.autoRotate); return 1; }
    if (is(k, "Name") || is(k, "ClassName")) { lua_pushstring(L, "Humanoid"); return 1; }
    if (is(k, "Parent"))     { LuaApi::pushInstance(L, humRoot(L)); return 1; }
    if (is(k, "Died"))       { LuaApi::pushSignal(L, SignalKind::Died, humRoot(L)); return 1; }
    if (is(k, "TakeDamage")) { lua_pushcfunction(L, hum_takeDamage); return 1; }
    if (is(k, "BreakJoints")) { lua_pushcfunction(L, hum_breakJoints); return 1; }
    if (is(k, "EquipTool"))   { lua_pushcfunction(L, hum_equipTool); return 1; }
    if (is(k, "UnequipTools")) { lua_pushcfunction(L, hum_unequipTools); return 1; }
    if (is(k, "IsA"))        { lua_pushcfunction(L, hum_isA); return 1; }
    return luaL_error(L, "'%s' is not a valid member of Humanoid", k);
}

int hum_newindex(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    Humanoid& h = hum(L);
    if (is(k, "Health"))     h.health = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, h.maxHealth);
    else if (is(k, "MaxHealth")) { h.maxHealth = std::max(1.0f, (float)luaL_checknumber(L, 3));
                                   h.health = std::min(h.health, h.maxHealth); }
    else if (is(k, "WalkSpeed"))  h.walkSpeed = std::max(0.0f, (float)luaL_checknumber(L, 3));
    else if (is(k, "JumpPower"))  h.jumpPower = std::max(0.0f, (float)luaL_checknumber(L, 3));
    else if (is(k, "AutoRotate")) h.autoRotate = lua_toboolean(L, 3);
    else return luaL_error(L, "'%s' can't be set on Humanoid", k);
    touched(L);
    return 0;
}

int hum_eq(lua_State* L) {
    auto* a = static_cast<HumRef*>(luaL_testudata(L, 1, kHum));
    auto* b = static_cast<HumRef*>(luaL_testudata(L, 2, kHum));
    lua_pushboolean(L, a && b && a->rootId == b->rootId);
    return 1;
}

// ===========================================================================
// Lighting
// ===========================================================================

int light_index(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    Environment& e = E(L)->scene()->environment();
    if (is(k, "ClockTime"))     { lua_pushnumber(L, E(L)->clockTime()); return 1; }
    if (is(k, "Brightness"))    { lua_pushnumber(L, e.sunIntensity);    return 1; }
    if (is(k, "GlobalShadows")) { lua_pushboolean(L, e.shadows);        return 1; }
    if (is(k, "FogEnabled"))    { lua_pushboolean(L, e.fogEnabled);     return 1; }
    if (is(k, "FogDensity"))    { lua_pushnumber(L, e.fogDensity);      return 1; }
    if (is(k, "FogColor"))      { LuaApi::pushColor3(L, e.fogColor);    return 1; }
    if (is(k, "Ambient"))       { LuaApi::pushColor3(L, e.ambientColor); return 1; }
    if (is(k, "SunColor"))      { LuaApi::pushColor3(L, e.sunColor);    return 1; }
    if (is(k, "Name") || is(k, "ClassName")) { lua_pushstring(L, "Lighting"); return 1; }
    return luaL_error(L, "'%s' is not a valid member of Lighting", k);
}

int light_newindex(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    Environment& e = E(L)->scene()->environment();
    if (is(k, "ClockTime"))     { E(L)->clockTime() = (float)luaL_checknumber(L, 3); E(L)->applyClockTime(); return 0; }
    if (is(k, "Brightness"))    { e.sunIntensity = (float)luaL_checknumber(L, 3); return 0; }
    if (is(k, "GlobalShadows")) { e.shadows = lua_toboolean(L, 3);    return 0; }
    if (is(k, "FogEnabled"))    { e.fogEnabled = lua_toboolean(L, 3); return 0; }
    if (is(k, "FogDensity"))    { e.fogDensity = (float)luaL_checknumber(L, 3); return 0; }
    if (is(k, "FogColor"))      { e.fogColor = LuaApi::checkColor3(L, 3);     return 0; }
    if (is(k, "Ambient"))       { e.ambientColor = LuaApi::checkColor3(L, 3); return 0; }
    if (is(k, "SunColor"))      { e.sunColor = LuaApi::checkColor3(L, 3);     return 0; }
    return luaL_error(L, "'%s' can't be set on Lighting", k);
}

void makeMeta(lua_State* L, const char* name, const luaL_Reg* fns) {
    luaL_newmetatable(L, name);
    luaL_setfuncs(L, fns, 0);
    lua_pushstring(L, name);
    lua_setfield(L, -2, "__name");
    lua_pop(L, 1);
}

} // namespace

namespace LuaApi {

void pushInstance(lua_State* L, uint64_t id) {
    if (!id) { lua_pushnil(L); return; }
    // Reuse the same Lua object for the same Instance so it works as a table key.
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceCache");
    if (lua_rawgeti(L, -1, (lua_Integer)id) == LUA_TUSERDATA) {
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);
    auto* ref = static_cast<InstRef*>(lua_newuserdatauv(L, sizeof(InstRef), 0));
    ref->id = id;
    luaL_setmetatable(L, kInst);
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, (lua_Integer)id);
    lua_remove(L, -2);
}

SceneNode* checkNode(lua_State* L, int idx) {
    // A player (a Lua table) stands for its object: `folder.Parent = player`.
    if (lua_type(L, idx) == LUA_TTABLE) {
        idx = lua_absindex(L, idx);
        lua_pushstring(L, "__node");
        lua_rawget(L, idx);
        auto* r = static_cast<InstRef*>(luaL_testudata(L, -1, kInst));
        uint64_t id = r ? r->id : 0;
        lua_pop(L, 1);
        if (!id) luaL_argerror(L, idx, "expected an object");
        SceneNode* n = engine(L)->resolve(id);
        if (!n) luaL_error(L, "This object has been destroyed");
        return n;
    }
    auto* ref = static_cast<InstRef*>(luaL_checkudata(L, idx, kInst));
    SceneNode* n = engine(L)->resolve(ref->id);
    if (!n) luaL_error(L, "This object has been destroyed");
    return n;
}

void pushSignal(lua_State* L, SignalKind kind, uint64_t id) {
    auto* s = static_cast<SignalRef*>(lua_newuserdatauv(L, sizeof(SignalRef), 0));
    s->kind = kind;
    s->id   = id;
    luaL_setmetatable(L, kSig);
}

void pushValue(lua_State* L, const SceneNode& n) {
    const Attribute& v = n.value;
    switch (v.type) {
        case Attribute::Bool:    lua_pushboolean(L, v.b); break;
        case Attribute::String:  lua_pushstring(L, v.s.c_str()); break;
        case Attribute::Vector3: pushVector3(L, v.v); break;
        case Attribute::Color3:  pushColor3(L, v.v); break;
        default:
            if (n.intValue) lua_pushinteger(L, (lua_Integer)v.n);
            else lua_pushnumber(L, v.n);
    }
}

void pushHumanoid(lua_State* L, uint64_t rootId) {
    auto* h = static_cast<HumRef*>(lua_newuserdatauv(L, sizeof(HumRef), 0));
    h->rootId = rootId;
    luaL_setmetatable(L, kHum);
}

void pushLighting(lua_State* L) {
    lua_newuserdatauv(L, 1, 0);
    luaL_setmetatable(L, kLight);
}

void registerInstance(lua_State* L) {
    static const luaL_Reg instMeta[] = {
        {"__index", inst_index}, {"__newindex", inst_newindex}, {"__eq", inst_eq},
        {"__tostring", inst_tostring}, {nullptr, nullptr}};
    static const luaL_Reg sigMeta[]   = {{"__index", sig_index},   {nullptr, nullptr}};
    static const luaL_Reg connMeta[]  = {{"__index", conn_index},  {nullptr, nullptr}};
    static const luaL_Reg humMeta[]   = {{"__index", hum_index}, {"__newindex", hum_newindex}, {"__eq", hum_eq}, {nullptr, nullptr}};
    static const luaL_Reg lightMeta[] = {{"__index", light_index}, {"__newindex", light_newindex}, {nullptr, nullptr}};
    makeMeta(L, kInst, instMeta);
    makeMeta(L, kSig, sigMeta);
    makeMeta(L, kConn, connMeta);
    makeMeta(L, kHum, humMeta);
    makeMeta(L, kLight, lightMeta);

    luaL_newlib(L, kMethods);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");

    // Weak-valued cache: id -> Instance userdata.
    lua_newtable(L);
    lua_newtable(L);
    lua_pushstring(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.InstanceCache");

    lua_newtable(L);
    lua_pushcfunction(L, inst_new);
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Instance");
}

} // namespace LuaApi
