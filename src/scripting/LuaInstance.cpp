// Scene objects for scripts: Instance (parts, models, scripts), Humanoid,
// Lighting, and the Signal / Connection event objects.
#include "LuaApi.h"
#include "ScriptEngine.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Serializer.h"
#include "../renderer/MeshLibrary.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
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

const char* materialNames[] = {"Plastic", "Metal", "Neon", "Wood"};

bool parseMaterial(const std::string& s, Material& out) {
    for (int i = 0; i < 4; ++i)
        if (s == materialNames[i]) { out = (Material)i; return true; }
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
    Player* p = E(L)->scene()->player();
    return p && n->id == p->rootId();
}

const char* className(lua_State* L, const SceneNode* n) {
    if (n == E(L)->scene()->root()) return "Workspace";
    switch (n->kind) {
        case NodeKind::Model:  return "Model";
        case NodeKind::Script: return "Script";
        default:               return "Part";
    }
}

bool isA(lua_State* L, const SceneNode* n, const std::string& cls) {
    if (cls == "Instance") return true;
    std::string mine = className(L, n);
    if (cls == mine) return true;
    if (n->kind == NodeKind::Part && (cls == "BasePart" || cls == "Part" || cls == "PVInstance")) return true;
    if (n->kind == NodeKind::Model && (cls == "Model" || cls == "Folder" || cls == "PVInstance")) return true;
    if (n->kind == NodeKind::Script && (cls == "BaseScript" || cls == "LuaSourceContainer")) return true;
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
    if (name == "Humanoid" && isCharacterRoot(L, n)) { LuaApi::pushHumanoid(L); return 1; }
    SceneNode* c = n->findChild(name, recursive);
    LuaApi::pushInstance(L, c ? c->id : 0);
    return 1;
}

int m_FindFirstChildOfClass(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string cls = luaL_checkstring(L, 2);
    if (cls == "Humanoid" && isCharacterRoot(L, n)) { LuaApi::pushHumanoid(L); return 1; }
    for (auto& c : n->children)
        if (!c->internal && isA(L, c.get(), cls)) { LuaApi::pushInstance(L, c->id); return 1; }
    lua_pushnil(L);
    return 1;
}

int m_WaitForChild(lua_State* L) {
    SceneNode* n = LuaApi::checkNode(L, 1);
    std::string name = luaL_checkstring(L, 2);
    if (name == "Humanoid" && isCharacterRoot(L, n)) { LuaApi::pushHumanoid(L); return 1; }
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

const luaL_Reg kMethods[] = {
    {"FindFirstChild", m_FindFirstChild}, {"FindFirstChildOfClass", m_FindFirstChildOfClass},
    {"WaitForChild", m_WaitForChild}, {"GetChildren", m_GetChildren},
    {"GetDescendants", m_GetDescendants}, {"Destroy", m_Destroy}, {"Remove", m_Destroy},
    {"ClearAllChildren", m_ClearAllChildren}, {"Clone", m_Clone}, {"IsA", m_IsA},
    {"IsDescendantOf", m_IsDescendantOf}, {"GetFullName", m_GetFullName},
    {"GetPivot", m_GetPivot}, {"PivotTo", m_PivotTo}, {nullptr, nullptr}};

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
    if (is(k, "Parent"))    { LuaApi::pushInstance(L, n->parent ? n->parent->id : 0); return 1; }
    if (is(k, "Position"))  { LuaApi::pushVector3(L, worldPosition(n)); return 1; }
    if (is(k, "Orientation") || is(k, "Rotation")) { LuaApi::pushVector3(L, n->transform.rotation); return 1; }
    if (is(k, "Size"))      { LuaApi::pushVector3(L, n->transform.scale); return 1; }
    if (is(k, "CFrame"))    { LuaApi::pushCFrame(L, getCFrame(n)); return 1; }
    if (is(k, "Visible"))   { lua_pushboolean(L, n->visible); return 1; }

    bool hrp = n->parent && isCharacterRoot(L, n->parent) && n->name == "HumanoidRootPart";
    if (hrp && (is(k, "Velocity") || is(k, "AssemblyLinearVelocity"))) {
        LuaApi::pushVector3(L, E(L)->scene()->player()->velocity());
        return 1;
    }

    if (part) {
        if (is(k, "Color"))        { LuaApi::pushColor3(L, n->color); return 1; }
        if (is(k, "Transparency")) { lua_pushnumber(L, n->transparency); return 1; }
        if (is(k, "Anchored"))     { lua_pushboolean(L, n->anchored); return 1; }
        if (is(k, "CanCollide"))   { lua_pushboolean(L, n->canCollide); return 1; }
        if (is(k, "CastShadow"))   { lua_pushboolean(L, n->castShadow); return 1; }
        if (is(k, "Material"))     { lua_pushstring(L, materialNames[(int)n->material]); return 1; }
        if (is(k, "Shape"))        { lua_pushstring(L, shapeName(n->primitiveType)); return 1; }
        if (is(k, "Velocity") || is(k, "AssemblyLinearVelocity")) { LuaApi::pushVector3(L, n->velocity); return 1; }
        if (is(k, "Touched"))      { LuaApi::pushSignal(L, SignalKind::Touched, n->id); return 1; }
        if (is(k, "Clicked"))      { LuaApi::pushSignal(L, SignalKind::Clicked, n->id); return 1; }
    }
    if (n->kind == NodeKind::Script) {
        if (is(k, "Enabled"))  { lua_pushboolean(L, n->scriptEnabled); return 1; }
        if (is(k, "Disabled")) { lua_pushboolean(L, !n->scriptEnabled); return 1; }
        if (is(k, "Source"))   { lua_pushstring(L, n->source.c_str()); return 1; }
    }
    if (n == E(L)->scene()->root() && is(k, "Gravity")) {
        lua_pushnumber(L, E(L)->scene()->world().gravity);
        return 1;
    }
    if (is(k, "Humanoid") && isCharacterRoot(L, n)) { LuaApi::pushHumanoid(L); return 1; }

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
        scene->player()->launch(LuaApi::checkVector3(L, 3));
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
                return luaL_error(L, "Unknown material '%s' (try Plastic, Metal, Neon or Wood)", lua_tostring(L, 3));
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
        if (is(k, "Velocity") || is(k, "AssemblyLinearVelocity")) { n->velocity = LuaApi::checkVector3(L, 3); return 0; }
    }
    if (n->kind == NodeKind::Script) {
        if (is(k, "Enabled"))  { n->scriptEnabled = lua_toboolean(L, 3); return 0; }
        if (is(k, "Disabled")) { n->scriptEnabled = !lua_toboolean(L, 3); return 0; }
    }
    if (n == scene->root() && is(k, "Gravity")) {
        scene->world().gravity = (float)luaL_checknumber(L, 3);
        return 0;
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
                             "\"Cylinder\", \"Model\" or \"Folder\")", cls.c_str());
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

Humanoid& hum(lua_State* L) {
    Player* p = E(L)->scene()->player();
    if (!p) luaL_error(L, "There is no character in this game");
    return p->humanoid();
}

int hum_takeDamage(lua_State* L) {
    Humanoid& h = hum(L);
    h.health = glm::clamp(h.health - (float)luaL_checknumber(L, 2), 0.0f, h.maxHealth);
    return 0;
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
    if (is(k, "Parent"))     { LuaApi::pushInstance(L, E(L)->scene()->player()->rootId()); return 1; }
    if (is(k, "Died"))       { LuaApi::pushSignal(L, SignalKind::Died, 0); return 1; }
    if (is(k, "TakeDamage")) { lua_pushcfunction(L, hum_takeDamage); return 1; }
    if (is(k, "IsA")) {
        lua_pushcfunction(L, [](lua_State* L2) {
            const char* c = luaL_checkstring(L2, 2);
            lua_pushboolean(L2, is(c, "Humanoid") || is(c, "Instance"));
            return 1;
        });
        return 1;
    }
    return luaL_error(L, "'%s' is not a valid member of Humanoid", k);
}

int hum_newindex(lua_State* L) {
    const char* k = luaL_checkstring(L, 2);
    Humanoid& h = hum(L);
    if (is(k, "Health"))     { h.health = glm::clamp((float)luaL_checknumber(L, 3), 0.0f, h.maxHealth); return 0; }
    if (is(k, "MaxHealth"))  { h.maxHealth = std::max(1.0f, (float)luaL_checknumber(L, 3));
                               h.health = std::min(h.health, h.maxHealth); return 0; }
    if (is(k, "WalkSpeed"))  { h.walkSpeed = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
    if (is(k, "JumpPower"))  { h.jumpPower = std::max(0.0f, (float)luaL_checknumber(L, 3)); return 0; }
    if (is(k, "AutoRotate")) { h.autoRotate = lua_toboolean(L, 3); return 0; }
    return luaL_error(L, "'%s' can't be set on Humanoid", k);
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

void pushHumanoid(lua_State* L) {
    lua_newuserdatauv(L, 1, 0);
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
    static const luaL_Reg humMeta[]   = {{"__index", hum_index}, {"__newindex", hum_newindex}, {nullptr, nullptr}};
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
