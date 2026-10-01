#include "ScriptEngine.h"
#include "../core/Account.h"
#include <cctype>
#include <sstream>
#include <fstream>
#include "LuaApi.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../core/Log.h"
#include "../scene/Effects.h"
#include "../scene/Physics.h"
#include "../core/Audio.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Lua code that builds the friendly Roblox-style globals on top of the C API.
const char* kPrelude = R"LUA(
-- Enum.KeyCode.E -> "E", Enum.Material.Neon -> "Neon", Enum.UserInputType.Keyboard -> "Keyboard"
Enum = setmetatable({}, { __index = function(t, category)
    local e = setmetatable({}, { __index = function(_, name) return name end })
    rawset(t, category, e)
    return e
end })

task = { wait = __gb_wait, spawn = __gb_spawn, delay = __gb_delay, defer = __gb_spawn }
wait, spawn, delay = task.wait, task.spawn, task.delay

-- Players are tables with an object behind them (__node), so `player.leaderstats`
-- and `folder.Parent = player` work like Roblox.
local playerNode, setRespawn = __gb_playerNode, __gb_setRespawn
-- Game UI lives in a folder called StarterGui (game.StarterGui, player.PlayerGui).
local function uiFolder()
    local f = workspace:FindFirstChild("StarterGui")
    if not f then
        f = Instance.new("Folder")
        f.Name = "StarterGui"
        f.Parent = workspace
    end
    return f
end
__gb_uiFolder = uiFolder
local playerMeta = {
    __index = function(t, k)
        if k == "PlayerGui" then return uiFolder() end
        return rawget(t, "__node")[k]
    end,
    __newindex = function(t, k, v)
        if k == "RespawnLocation" and rawget(t, "__local") then setRespawn(v) end
        rawset(t, k, v)
    end,
    __tostring = function(t) return rawget(t, "Name") end,
}
local function makePlayer(name, character, id)
    return setmetatable({ Name = name, DisplayName = name, UserId = id, Character = character,
                          __node = playerNode(name) }, playerMeta)
end
local LocalPlayer = makePlayer(__gb_playerName, __gb_character, 1)
rawset(LocalPlayer, "Backpack", __gb_backpack)
rawset(LocalPlayer, "__local", true)
local playerList = { LocalPlayer }
Players = { LocalPlayer = LocalPlayer, PlayerAdded = __gb_playerAdded,
            PlayerRemoving = __gb_playerRemoving }
function Players:GetPlayers()
    local t = {}
    for i, p in ipairs(playerList) do t[i] = p end
    return t
end
function Players:GetPlayerFromCharacter(model)
    if model == nil then return nil end
    for _, p in ipairs(playerList) do if p.Character == model then return p end end
    return nil
end
function Players:FindFirstChild(name)
    for _, p in ipairs(playerList) do if p.Name == name then return p end end
    return nil
end
-- Used by the engine when people join / leave a multiplayer game.
function __gb_addPlayer(name, character, id)
    local p = makePlayer(name, character, id)
    table.insert(playerList, p)
    return p
end
function __gb_removePlayer(name)
    for i, p in ipairs(playerList) do
        if p.Name == name then table.remove(playerList, i) return p end
    end
    return nil
end

RunService = { Heartbeat = __gb_heartbeat, RenderStepped = __gb_heartbeat,
               Stepped = __gb_heartbeat }
UserInputService = { InputBegan = __gb_inputBegan, InputEnded = __gb_inputEnded }
local isKeyDown = __gb_isKeyDown
function UserInputService:IsKeyDown(key) return isKeyDown(key) end

-- An event that only passes on some firings: `test` picks them, `out` shapes the arguments.
local function filtered(sig, test, out)
    local s = {}
    function s:Connect(fn)
        return sig:Connect(function(...) if test(...) then fn(out(...)) end end)
    end
    s.connect = s.Connect
    function s:Once(fn)
        local c
        c = sig:Connect(function(...) if test(...) then c:Disconnect() fn(out(...)) end end)
        return c
    end
    function s:Wait()
        while true do
            local r = table.pack(sig:Wait())
            if test(table.unpack(r, 1, r.n)) then return out(table.unpack(r, 1, r.n)) end
        end
    end
    return s
end
function __gb_attrSignal(sig, name)
    return filtered(sig, function(n) return n == name end, function() end)
end

-- CollectionService: tags on objects (obj:AddTag("Enemy"), CollectionService:GetTagged("Enemy")).
local tagAdded, tagRemoved = __gb_tagAdded, __gb_tagRemoved
CollectionService = {}
function CollectionService:GetTagged(tag)
    local t = {}
    for _, d in ipairs(workspace:GetDescendants()) do
        if d:HasTag(tag) then table.insert(t, d) end
    end
    return t
end
function CollectionService:GetAllTags()
    local seen, t = {}, {}
    for _, d in ipairs(workspace:GetDescendants()) do
        for _, tag in ipairs(d:GetTags()) do
            if not seen[tag] then seen[tag] = true table.insert(t, tag) end
        end
    end
    return t
end
function CollectionService:HasTag(obj, tag) return obj:HasTag(tag) end
function CollectionService:AddTag(obj, tag) obj:AddTag(tag) end
function CollectionService:RemoveTag(obj, tag) obj:RemoveTag(tag) end
function CollectionService:GetTags(obj) return obj:GetTags() end
function CollectionService:GetInstanceAddedSignal(tag)
    return filtered(tagAdded, function(_, t) return t == tag end, function(o) return o end)
end
function CollectionService:GetInstanceRemovedSignal(tag)
    return filtered(tagRemoved, function(_, t) return t == tag end, function(o) return o end)
end

-- DataStoreService: save things between visits (coins, levels...). Kept per
-- game in this player's account folder.
local dsGet, dsSet = __gb_dsGet, __gb_dsSet
DataStoreService = {}
function DataStoreService:GetDataStore(name, scope)
    local store = tostring(name) .. (scope and ("/" .. tostring(scope)) or "")
    local ds = {}
    function ds:GetAsync(key) return dsGet(store, tostring(key)) end
    function ds:SetAsync(key, value) dsSet(store, tostring(key), value) end
    function ds:RemoveAsync(key)
        local old = dsGet(store, tostring(key))
        dsSet(store, tostring(key), nil)
        return old
    end
    function ds:UpdateAsync(key, fn)
        local new = fn(dsGet(store, tostring(key)))
        if new ~= nil then dsSet(store, tostring(key), new) end
        return new
    end
    function ds:IncrementAsync(key, delta)
        local v = (tonumber(dsGet(store, tostring(key))) or 0) + (delta or 1)
        dsSet(store, tostring(key), v)
        return v
    end
    return ds
end
DataStoreService.GetOrderedDataStore = DataStoreService.GetDataStore

-- PathfindingService: walking routes on the navigation mesh (every floor a character
-- can stand on, baked from the parts; it re-bakes itself when anchored parts change).
-- Works like Roblox's:
--   local path = PathfindingService:CreatePath({ AgentRadius = 0.6, AgentCanJump = true,
--                                                Costs = { Water = 10, Lava = math.huge } })
--   path:ComputeAsync(npc.HumanoidRootPart.Position, target)
--   if path.Status == Enum.PathStatus.Success then
--       for _, wp in ipairs(path:GetWaypoints()) do
--           if wp.Action == Enum.PathWaypointAction.Jump then humanoid.Jump = true end
--           humanoid:MoveTo(wp.Position)
--           humanoid.MoveToFinished:Wait()
--       end
--   end
-- ...or let the engine do all of that: humanoid:PathfindTo(target) walks there by itself
-- (jumping, finding a new way when blocked or stuck, keeping up with a moving target).
-- Extras: PathfindingService:IsWalkable(pos), :FindClosestPoint(pos), :GetRandomPoint(near, radius),
-- :CanWalkStraight(a, b), :Bake(), :SetBakeSettings({...}). Part attributes
-- PathfindingLabel (a name for Costs) and PathfindingPassThrough (ignored) work like
-- Roblox's PathfindingModifier.
local navPath, navQuery = __gb_navPath, __gb_navQuery
local function luaSignal()
    local sig, list = {}, {}
    function sig:Connect(fn)
        local c = { Connected = true }
        function c:Disconnect() self.Connected = false end
        list[#list + 1] = { c, fn }
        return c
    end
    sig.connect = sig.Connect
    function sig:Wait()
        local done, args = false, nil
        local c
        c = sig:Connect(function(...) args = { ... }; done = true; c:Disconnect() end)
        while not done do task.wait() end
        return table.unpack(args)
    end
    function sig:Fire(...)
        for _, e in ipairs(list) do if e[1].Connected then task.spawn(e[2], ...) end end
    end
    return sig
end
local watched = setmetatable({}, { __mode = "k" })   -- paths that can be Blocked
local watching = false
local function startWatching()
    if watching then return end
    watching = true
    task.spawn(function()
        while true do
            task.wait(0.25)
            for path in pairs(watched) do
                if path._ver ~= navQuery("version") then
                    path._ver = navQuery("version")
                    local idx = path:CheckOcclusionAsync(1)
                    if idx > 0 and not path._blocked then path._blocked = true; path.Blocked:Fire(idx)
                    elseif idx < 0 and path._blocked then path._blocked = false; path.Unblocked:Fire(1) end
                end
            end
        end
    end)
end
PathfindingService = { ClassName = "PathfindingService", Name = "PathfindingService" }
function PathfindingService:CreatePath(params)
    local path = { ClassName = "Path", Status = "NoPath", _wps = {}, _params = params or {} }
    path.Blocked, path.Unblocked = luaSignal(), luaSignal()
    function path:ComputeAsync(from, to)
        local status, wps = navPath(from, to, self._params)
        self.Status = status
        self._wps = wps
        self._ver = navQuery("version")
        self._blocked = false
        watched[self] = true
        startWatching()
    end
    function path:GetWaypoints()
        local t = {}
        for i, w in ipairs(self._wps) do
            t[i] = { ClassName = "PathWaypoint", Position = w[1], Action = w[2], Label = w[3] }
        end
        return t
    end
    -- The first waypoint (from `start` on) whose way there is blocked now, or -1.
    function path:CheckOcclusionAsync(start)
        local w = self._wps
        for i = math.max(2, start or 1), #w do
            if w[i][2] == "Jump" then
                if not navQuery("walkable", w[i][1], self._params) then return i end
            elseif not navQuery("straight", w[i - 1][1], w[i][1], self._params) then
                return i
            end
        end
        return -1
    end
    function path:Destroy() watched[self] = nil; self._wps = {} end
    return path
end
function PathfindingService:FindPathAsync(from, to)
    local p = self:CreatePath()
    p:ComputeAsync(from, to)
    return p
end
PathfindingService.ComputeRawPathAsync = PathfindingService.FindPathAsync
PathfindingService.ComputeSmoothPathAsync = PathfindingService.FindPathAsync
function PathfindingService:IsWalkable(pos, params) return navQuery("walkable", pos, params) end
function PathfindingService:FindClosestPoint(pos, range, params) return navQuery("closest", pos, range or 10, params) end
function PathfindingService:GetRandomPoint(near, radius, params) return navQuery("random", near, radius, params) end
function PathfindingService:CanWalkStraight(a, b, params) return navQuery("straight", a, b, params) end
function PathfindingService:Bake() return navQuery("bake") end
function PathfindingService:SetBakeSettings(t) navQuery("settings", t) end
function PathfindingService:GetService() return self end

-- humanoid:PathfindTo(target [, params]): walk there by itself; waits until it gets
-- there (true) or gives up (false). target: a Vector3, a part, or a model/character.
function __gb_pathfindTo(hum, target, params)
    if not hum:PathfindStart(target, params) then return false end
    while true do
        local st = hum.PathfindStatus
        if st == "Arrived" then return true end
        if st ~= "Walking" then return false end
        task.wait(0.1)
    end
end

-- BadgeService: give players the badges you made for your game on its page
-- (Create > your game > Badges). Only works in a published game's online server.
--   BadgeService:AwardBadge(player, "gb-badge-1a2b3c")
local awardBadge, hasBadge = __gb_awardBadge, __gb_hasBadge
BadgeService = {}
local function nameOf(p)
    if type(p) == "table" then return p.Name end
    if type(p) == "number" then
        for _, pl in ipairs(Players:GetPlayers()) do if pl.UserId == p then return pl.Name end end
    end
    return tostring(p)
end
function BadgeService:AwardBadge(player, badgeId)
    awardBadge(nameOf(player), tostring(badgeId))
    return true
end
function BadgeService:UserHasBadgeAsync(player, badgeId)
    return hasBadge(nameOf(player), tostring(badgeId))
end
BadgeService.UserHasBadge = BadgeService.UserHasBadgeAsync

-- Debris: throw something away later, like Roblox's. Debris:AddItem(part, 5)
Debris = { ClassName = "Debris", Name = "Debris", MaxItems = 1000 }
function Debris:AddItem(obj, lifetime)
    task.delay(tonumber(lifetime) or 10, function()
        if obj and obj.Parent then pcall(function() obj:Destroy() end) end
    end)
end
Debris.addItem = Debris.AddItem

local services = { Workspace = workspace, PathfindingService = PathfindingService, BadgeService = BadgeService, Debris = Debris, Players = Players, Lighting = Lighting,
                   RunService = RunService, UserInputService = UserInputService, Gui = Gui,
                   CollectionService = CollectionService, DataStoreService = DataStoreService }
game = setmetatable({}, { __index = function(_, name)
    if name == "StarterGui" then return __gb_uiFolder() end
    return services[name]
end })
function game:GetService(name)
    if name == "StarterGui" then return __gb_uiFolder() end
    local s = services[name]
    if s == nil then
        error("'" .. tostring(name) .. "' is not a service Guts and Bolts knows about", 2)
    end
    return s
end
Workspace = workspace

-- Handy extras that Roblox's Luau also has.
math.atan2 = math.atan2 or function(y, x) return math.atan(y, x) end
math.pow = math.pow or function(x, y) return x ^ y end
math.log10 = math.log10 or function(x) return math.log(x, 10) end
function math.clamp(x, lo, hi) if x < lo then return lo elseif x > hi then return hi end return x end
function math.sign(x) if x > 0 then return 1 elseif x < 0 then return -1 end return 0 end
function math.round(x) return math.floor(x + 0.5) end
function math.lerp(a, b, t) return a + (b - a) * t end
function string.split(s, sep)
    sep = sep or ","
    local out, start = {}, 1
    while true do
        local i, j = string.find(s, sep, start, true)
        if not i then table.insert(out, string.sub(s, start)) break end
        table.insert(out, string.sub(s, start, i - 1))
        start = j + 1
    end
    return out
end
function table.find(t, value)
    for i, v in ipairs(t) do if v == value then return i end end
    return nil
end

__gb_wait, __gb_spawn, __gb_delay, __gb_character, __gb_playerName, __gb_backpack = nil, nil, nil, nil, nil, nil
__gb_heartbeat, __gb_inputBegan, __gb_inputEnded, __gb_isKeyDown = nil, nil, nil, nil
__gb_playerAdded, __gb_playerRemoving, __gb_tagAdded, __gb_tagRemoved = nil, nil, nil, nil
__gb_playerNode, __gb_setRespawn, __gb_dsGet, __gb_dsSet, __gb_navPath, __gb_navQuery = nil, nil, nil, nil, nil, nil
__gb_awardBadge, __gb_hasBadge = nil, nil
)LUA";

constexpr double kTimeoutSeconds = 5.0;

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct KeyName { const char* name; ImGuiKey key; };

const std::vector<KeyName>& keyTable() {
    static std::vector<KeyName> keys = [] {
        std::vector<KeyName> k;
        static const char* letters[] = {"A","B","C","D","E","F","G","H","I","J","K","L","M",
                                        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"};
        for (int i = 0; i < 26; ++i) k.push_back({letters[i], (ImGuiKey)(ImGuiKey_A + i)});
        static const char* digits[] = {"Zero","One","Two","Three","Four","Five","Six","Seven",
                                       "Eight","Nine"};
        for (int i = 0; i < 10; ++i) k.push_back({digits[i], (ImGuiKey)(ImGuiKey_0 + i)});
        k.push_back({"Space",        ImGuiKey_Space});
        k.push_back({"Return",       ImGuiKey_Enter});
        k.push_back({"Tab",          ImGuiKey_Tab});
        k.push_back({"Backspace",    ImGuiKey_Backspace});
        k.push_back({"LeftShift",    ImGuiKey_LeftShift});
        k.push_back({"RightShift",   ImGuiKey_RightShift});
        k.push_back({"LeftControl",  ImGuiKey_LeftCtrl});
        k.push_back({"RightControl", ImGuiKey_RightCtrl});
        k.push_back({"LeftAlt",      ImGuiKey_LeftAlt});
        k.push_back({"Up",           ImGuiKey_UpArrow});
        k.push_back({"Down",         ImGuiKey_DownArrow});
        k.push_back({"Left",         ImGuiKey_LeftArrow});
        k.push_back({"Right",        ImGuiKey_RightArrow});
        return k;
    }();
    return keys;
}

// ---------------------------------------------------------------------------
// Global functions
// ---------------------------------------------------------------------------

std::string joinArgs(lua_State* L) {
    std::string out;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        size_t len;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) out += ' ';
        out.append(s, len);
        lua_pop(L, 1);
    }
    return out;
}

int l_print(lua_State* L) { Log::info(joinArgs(L)); return 0; }
int l_warn (lua_State* L) { Log::warn(joinArgs(L)); return 0; }

// wait(seconds) — pause this script; returns how long it actually waited.
int l_wait(lua_State* L) {
    double t = luaL_optnumber(L, 1, 0.0);
    lua_settop(L, 0);
    lua_pushnumber(L, t);
    return lua_yield(L, 1);
}

// spawn(fn, ...) — run fn right away in its own thread.
int l_spawn(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    int n = lua_gettop(L);
    lua_State* co = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    for (int i = 1; i <= n; ++i) lua_pushvalue(L, i);
    lua_xmove(L, co, n);
    LuaApi::engine(L)->resume(co, ref, n - 1, L);
    return 0;
}

// delay(seconds, fn, ...) — run fn later.
int l_delay(lua_State* L) {
    double t = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int n = lua_gettop(L);
    lua_State* co = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    for (int i = 2; i <= n; ++i) lua_pushvalue(L, i);
    lua_xmove(L, co, n - 1);
    LuaApi::engine(L)->schedule(co, ref, t, n - 2);
    return 0;
}

// require(moduleScript) — runs a ModuleScript once and returns what it returns.
int l_require(lua_State* L) {
    SceneNode* m = LuaApi::checkNode(L, 1);
    if (!m->isScript()) return luaL_error(L, "require() needs a ModuleScript, got %s", m->name.c_str());
    lua_getfield(L, LUA_REGISTRYINDEX, "__gb_modules");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "__gb_modules");
    }
    lua_rawgeti(L, -1, (lua_Integer)m->id);
    if (!lua_isnil(L, -1)) return 1;                      // already loaded
    lua_pop(L, 1);
    std::string chunk = "=" + m->fullName();
    if (luaL_loadbuffer(L, m->source.data(), m->source.size(), chunk.c_str()) != LUA_OK) return lua_error(L);
    // Its own globals (falling back to the shared ones), with `script` = the module.
    lua_newtable(L);
    lua_newtable(L);
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    LuaApi::pushInstance(L, m->id);
    lua_setfield(L, -2, "script");
    lua_setupvalue(L, -2, 1);
    lua_call(L, 0, 1);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, (lua_Integer)m->id);              // cache it
    return 1;
}

int l_time(lua_State* L) { lua_pushnumber(L, LuaApi::engine(L)->time()); return 1; }
int l_tick(lua_State* L) {
    using namespace std::chrono;
    lua_pushnumber(L, duration<double>(system_clock::now().time_since_epoch()).count());
    return 1;
}

int l_isKeyDown(lua_State* L) {
    lua_pushboolean(L, LuaApi::engine(L)->isKeyDown(luaL_checkstring(L, 1)));
    return 1;
}

// --- DataStoreService: Lua values <-> JSON, kept in a file per game ----------

nlohmann::json toJson(lua_State* L, int idx, int depth = 0) {
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
        case LUA_TBOOLEAN: return (bool)lua_toboolean(L, idx);
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) return (long long)lua_tointeger(L, idx);
            return lua_tonumber(L, idx);
        case LUA_TSTRING: return std::string(lua_tostring(L, idx));
        case LUA_TTABLE: {
            if (depth > 20) luaL_error(L, "That table is nested too deep to save");
            // A list (1..n) saves as an array; anything else as an object with string keys.
            lua_Integer n = (lua_Integer)lua_rawlen(L, idx);
            nlohmann::json out = n > 0 ? nlohmann::json::array() : nlohmann::json::object();
            if (n > 0) {
                for (lua_Integer i = 1; i <= n; ++i) {
                    lua_rawgeti(L, idx, i);
                    out.push_back(toJson(L, -1, depth + 1));
                    lua_pop(L, 1);
                }
                return out;
            }
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                lua_pushvalue(L, -2);
                std::string key = luaL_tolstring(L, -1, nullptr);
                lua_pop(L, 2);   // the tostring'd key and its copy
                out[key] = toJson(L, -1, depth + 1);
                lua_pop(L, 1);
            }
            return out;
        }
        case LUA_TNIL: return nullptr;
        default: luaL_error(L, "DataStores can only save numbers, text, true/false and tables of those");
    }
    return nullptr;
}

void pushJson(lua_State* L, const nlohmann::json& j) {
    if (j.is_boolean()) lua_pushboolean(L, j.get<bool>());
    else if (j.is_number_integer()) lua_pushinteger(L, j.get<lua_Integer>());
    else if (j.is_number()) lua_pushnumber(L, j.get<double>());
    else if (j.is_string()) lua_pushstring(L, j.get<std::string>().c_str());
    else if (j.is_array()) {
        lua_createtable(L, (int)j.size(), 0);
        for (size_t i = 0; i < j.size(); ++i) { pushJson(L, j[i]); lua_rawseti(L, -2, (lua_Integer)i + 1); }
    } else if (j.is_object()) {
        lua_createtable(L, 0, (int)j.size());
        for (auto& [k, v] : j.items()) { pushJson(L, v); lua_setfield(L, -2, k.c_str()); }
    } else lua_pushnil(L);
}

int l_dsGet(lua_State* L) {
    const nlohmann::json& all = LuaApi::engine(L)->saveData();
    std::string store = luaL_checkstring(L, 1), key = luaL_checkstring(L, 2);
    if (all.contains(store) && all[store].contains(key)) pushJson(L, all[store][key]);
    else lua_pushnil(L);
    return 1;
}

int l_dsSet(lua_State* L) {
    std::string store = luaL_checkstring(L, 1), key = luaL_checkstring(L, 2);
    nlohmann::json v = toJson(L, 3);
    if (v.dump().size() > 256 * 1024) return luaL_error(L, "That's too much to save in one key (256 KB at most)");
    LuaApi::engine(L)->setSaveData(store, key, v);
    return 0;
}

// __gb_awardBadge(playerName, badgeId): the app sends it to the server.
int l_awardBadge(lua_State* L) {
    std::string name = luaL_checkstring(L, 1), badge = luaL_checkstring(L, 2);
    ScriptEngine* e = LuaApi::engine(L);
    if (!e->knowsBadge(name, badge)) { e->queueBadge(name, badge); e->markBadge(name, badge); }
    return 0;
}

int l_hasBadge(lua_State* L) {
    lua_pushboolean(L, LuaApi::engine(L)->knowsBadge(luaL_checkstring(L, 1), luaL_checkstring(L, 2)));
    return 1;
}

int l_playerNode(lua_State* L) {
    LuaApi::pushInstance(L, LuaApi::engine(L)->playerNode(luaL_checkstring(L, 1)));
    return 1;
}

// LocalPlayer.RespawnLocation = part (nil = back to the normal spawn)
int l_setRespawn(lua_State* L) {
    Player* p = LuaApi::engine(L)->scene()->player();
    if (p) p->setCheckpoint(lua_isnil(L, 1) ? 0 : LuaApi::checkNode(L, 1)->id);
    return 0;
}

// Gui.Message(text, seconds), Gui.Label(key, text), Gui.Clear()
int gui_message(lua_State* L) {
    GuiState& g = LuaApi::engine(L)->gui();
    g.message     = luaL_optstring(L, 1, "");
    g.messageTime = (float)luaL_optnumber(L, 2, 3.0);
    return 0;
}
int gui_label(lua_State* L) {
    GuiState& g = LuaApi::engine(L)->gui();
    std::string key = luaL_checkstring(L, 1);
    if (lua_isnoneornil(L, 2)) g.labels.erase(key);
    else g.labels[key] = luaL_tolstring(L, 2, nullptr);
    return 0;
}
int gui_clear(lua_State* L) {
    GuiState& g = LuaApi::engine(L)->gui();
    g.labels.clear();
    g.message.clear();
    return 0;
}

// Explode(position, radius, power)
// __gb_navPath(from, to, params) -> status, { {position, action, label}, ... } (PathfindingService).
int l_navPath(lua_State* L) {
    glm::vec3 from = LuaApi::checkVector3(L, 1), to = LuaApi::checkVector3(L, 2);
    NavMesh::Agent agent = LuaApi::checkAgent(L, 3);
    const Physics* ph = LuaApi::engine(L)->physics();
    if (!ph) { lua_pushstring(L, "NoPath"); lua_newtable(L); return 2; }
    std::vector<NavMesh::Waypoint> wps;
    NavMesh::Status st = ph->navMesh().findPath(from, to, agent, wps);
    lua_pushstring(L, NavMesh::statusName(st));
    lua_createtable(L, (int)wps.size(), 0);
    for (size_t i = 0; i < wps.size(); ++i) {
        lua_createtable(L, 3, 0);
        LuaApi::pushVector3(L, wps[i].pos);  lua_rawseti(L, -2, 1);
        lua_pushstring(L, wps[i].action == NavMesh::Action::Jump ? "Jump" : "Walk"); lua_rawseti(L, -2, 2);
        lua_pushstring(L, wps[i].label.c_str()); lua_rawseti(L, -2, 3);
        lua_rawseti(L, -2, (int)i + 1);
    }
    return 2;
}

// __gb_navQuery(what, ...): the navmesh's other questions (see PathfindingService below).
int l_navQuery(lua_State* L) {
    const std::string what = luaL_checkstring(L, 1);
    const Physics* ph = LuaApi::engine(L)->physics();
    if (!ph) { lua_pushnil(L); return 1; }
    if (what == "bake") {
        ph->rebakeNavMesh();
        lua_pushnumber(L, ph->navMesh().bakeMs());
        lua_pushinteger(L, (lua_Integer)ph->navMesh().spanCount());
        return 2;
    }
    if (what == "settings") {   // SetBakeSettings({CellSize=, MaxSlope=, JumpHeight=, JumpGap=, MaxDrop=, StepHeight=})
        NavMesh::Settings st = ph->navMesh().settings();
        luaL_checktype(L, 2, LUA_TTABLE);
        auto num = [&](const char* k, float& out) {
            lua_getfield(L, 2, k);
            if (lua_isnumber(L, -1)) out = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        };
        num("CellSize", st.cell); num("MaxSlope", st.maxSlope); num("JumpHeight", st.jumpHeight);
        num("JumpGap", st.jumpGap); num("MaxDrop", st.maxDrop); num("StepHeight", st.maxClimb);
        st.cell = std::clamp(st.cell, 0.2f, 4.0f);
        ph->setNavSettings(st);
        return 0;
    }
    const NavMesh& nav = ph->navMesh();
    if (what == "version") { lua_pushinteger(L, nav.version()); return 1; }
    if (what == "walkable") {
        lua_pushboolean(L, nav.walkable(LuaApi::checkVector3(L, 2), LuaApi::checkAgent(L, 3)));
        return 1;
    }
    if (what == "closest") {
        glm::vec3 out;
        if (nav.closestPoint(LuaApi::checkVector3(L, 2), LuaApi::checkAgent(L, 4), (float)luaL_optnumber(L, 3, 10.0), out))
            LuaApi::pushVector3(L, out);
        else
            lua_pushnil(L);
        return 1;
    }
    if (what == "straight") {
        glm::vec3 hit;
        bool ok = nav.straightWalk(LuaApi::checkVector3(L, 2), LuaApi::checkVector3(L, 3), LuaApi::checkAgent(L, 4), &hit);
        lua_pushboolean(L, ok);
        if (ok) LuaApi::pushVector3(L, LuaApi::checkVector3(L, 3)); else LuaApi::pushVector3(L, hit);
        return 2;
    }
    if (what == "random") {
        glm::vec3 around = lua_isnoneornil(L, 2) ? glm::vec3(0.0f) : LuaApi::checkVector3(L, 2);
        float radius = lua_isnoneornil(L, 2) ? 0.0f : (float)luaL_optnumber(L, 3, 20.0);
        static uint32_t seed = 1;
        glm::vec3 out;
        if (nav.randomPoint(around, radius, LuaApi::checkAgent(L, 4), seed++, out)) LuaApi::pushVector3(L, out);
        else lua_pushnil(L);
        return 1;
    }
    return luaL_error(L, "unknown navmesh question '%s'", what.c_str());
}

int l_explode(lua_State* L) {
    glm::vec3 pos = LuaApi::checkVector3(L, 1);
    Effects::explode(*LuaApi::engine(L)->scene(), pos, (float)luaL_optnumber(L, 2, 6.0),
                     (float)luaL_optnumber(L, 3, 1.0));
    return 0;
}

// Effects.Blood(position, amount), Effects.Oil(...), Effects.Sparks(...), Effects.Gibs(...)
int fx_spray(lua_State* L, GoreKind kind) {
    Scene* s = LuaApi::engine(L)->scene();
    if (!s->goreEnabled()) return 0;
    int n = (int)luaL_optinteger(L, 2, 20);
    glm::vec3 pos = LuaApi::checkVector3(L, 1);
    // Optional third argument: which way (and how hard) it sprays, e.g. Vector3.new(0, 2, -8).
    glm::vec3 dir(0, 1, 0);
    float speed = 3.0f;
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
        glm::vec3 d = LuaApi::checkVector3(L, 3);
        if (glm::length(d) > 1e-3f) { speed = std::min(glm::length(d), 40.0f); dir = d / glm::length(d); }
    }
    s->particles().spray(kind, pos, dir, std::clamp(n, 1, 300), speed);
    s->pushFx(kind == GoreKind::Blood ? FxEvent::Blood : FxEvent::Oil, pos, (float)n);
    return 0;
}
int fx_blood(lua_State* L) { return fx_spray(L, GoreKind::Blood); }
int fx_oil(lua_State* L)   { return fx_spray(L, GoreKind::Oil); }
int fx_gibs(lua_State* L) {
    Scene* s = LuaApi::engine(L)->scene();
    if (!s->goreEnabled()) return 0;
    glm::vec3 pos = LuaApi::checkVector3(L, 1);
    int n = std::clamp((int)luaL_optinteger(L, 2, 8), 1, 60);
    s->particles().gibs(s->goreKind(), pos, glm::vec3(0, 2, 0), n);
    s->pushFx(FxEvent::Gibs, pos, (float)n);
    return 0;
}
int fx_sparks(lua_State* L) {
    Scene* s = LuaApi::engine(L)->scene();
    glm::vec3 pos = LuaApi::checkVector3(L, 1);
    int n = std::clamp((int)luaL_optinteger(L, 2, 20), 1, 300);
    s->particles().sparks(pos, n);
    s->pushFx(FxEvent::Sparks, pos, (float)n);
    return 0;
}

// Sounds.Play(name, position, volume) — quick one-off sound effects.
int snd_play(lua_State* L) {
    std::string name = luaL_checkstring(L, 1);
    glm::vec3* pos = LuaApi::toVector3(L, 2);
    float volume = (float)luaL_optnumber(L, 3, 0.7);
    Audio::play(name, volume, 1.0f, false, pos);
    LuaApi::engine(L)->scene()->pushFx(FxEvent::Sound, pos ? *pos : glm::vec3(0.0f), volume, name);
    return 0;
}
int snd_list(lua_State* L) {
    lua_newtable(L);
    int i = 1;
    for (const auto& n : Audio::builtinNames()) { lua_pushstring(L, n.c_str()); lua_rawseti(L, -2, i++); }
    return 1;
}

void timeoutHook(lua_State* L, lua_Debug*) { LuaApi::engine(L)->checkTimeout(L); }

} // namespace

// ===========================================================================

namespace LuaApi {
ScriptEngine* engine(lua_State* L) { return *static_cast<ScriptEngine**>(lua_getextraspace(L)); }

NavMesh::Agent checkAgent(lua_State* L, int idx) {
    NavMesh::Agent a;
    if (lua_isnoneornil(L, idx) || !lua_istable(L, idx)) return a;
    idx = lua_absindex(L, idx);
    auto num = [&](const char* k, float& out) {
        lua_getfield(L, idx, k);
        if (lua_isnumber(L, -1)) out = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    };
    num("AgentRadius", a.radius);
    num("AgentHeight", a.height);
    num("WaypointSpacing", a.spacing);
    // Copied from a Roblox game (AgentHeight 5, AgentRadius 2...)? Those are Roblox
    // studs: characters here are half that size.
    if (a.height >= 4.0f) { a.radius *= 0.5f; a.height *= 0.5f; a.spacing *= 0.5f; }
    a.radius = std::clamp(a.radius, 0.1f, 20.0f);
    a.height = std::clamp(a.height, 0.5f, 50.0f);
    if (!std::isfinite(a.spacing) || a.spacing <= 0.0f) a.spacing = 0.0f;   // math.huge: corners only
    lua_getfield(L, idx, "AgentCanJump");
    if (lua_isboolean(L, -1)) a.canJump = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, idx, "Costs");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_isnumber(L, -1)) {
                double v = lua_tonumber(L, -1);
                a.costs[lua_tostring(L, -2)] = !std::isfinite(v) || v > 1e8 ? 1e9f : (float)std::max(0.01, v);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return a;
}
}

ScriptEngine::ScriptEngine(Scene* scene) : m_scene(scene) {}
ScriptEngine::~ScriptEngine() { stop(); }

void ScriptEngine::openLibraries() {
    lua_State* L = m_L;
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base},        {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_TABLIBNAME, luaopen_table},  {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},  {LUA_UTF8LIBNAME, luaopen_utf8},
        {LUA_OSLIBNAME, luaopen_os},      {nullptr, nullptr}};
    for (const luaL_Reg* lib = libs; lib->func; ++lib) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    // Sandbox: scripts can't touch files or run programs.
    for (const char* name : {"dofile", "loadfile"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, "os");
    for (const char* name : {"execute", "exit", "remove", "rename", "tmpname", "getenv", "setlocale"}) {
        lua_pushnil(L);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
}

void ScriptEngine::start(bool runScripts) {
    stop();
    m_L = luaL_newstate();
    *static_cast<ScriptEngine**>(lua_getextraspace(m_L)) = this;
    m_time = 0.0;
    m_depth = 0;
    m_gui = GuiState{};
    // Every coroutine inherits this hook, which stops runaway loops.
    lua_sethook(m_L, timeoutHook, LUA_MASKCOUNT, 20000);

    openLibraries();
    LuaApi::registerTypes(m_L);
    LuaApi::registerInstance(m_L);

    lua_State* L = m_L;
    lua_register(L, "print", l_print);
    lua_register(L, "warn",  l_warn);
    lua_register(L, "time",  l_time);
    lua_register(L, "require", l_require);
    lua_register(L, "tick",  l_tick);
    lua_register(L, "__gb_wait",  l_wait);
    lua_register(L, "__gb_spawn", l_spawn);
    lua_register(L, "__gb_delay", l_delay);
    lua_register(L, "__gb_isKeyDown", l_isKeyDown);
    lua_register(L, "__gb_playerNode", l_playerNode);
    lua_register(L, "__gb_setRespawn", l_setRespawn);
    lua_register(L, "__gb_dsGet", l_dsGet);
    lua_register(L, "__gb_dsSet", l_dsSet);
    lua_register(L, "__gb_navPath", l_navPath);
    lua_register(L, "__gb_navQuery", l_navQuery);
    lua_register(L, "__gb_awardBadge", l_awardBadge);
    lua_register(L, "__gb_hasBadge", l_hasBadge);

    LuaApi::pushInstance(L, m_scene->root()->id);
    lua_setglobal(L, "workspace");
    LuaApi::pushInstance(L, m_scene->player() ? m_scene->player()->rootId() : 0);
    lua_setglobal(L, "__gb_character");
    LuaApi::pushInstance(L, m_scene->player() ? m_scene->player()->backpackId() : 0);
    lua_setglobal(L, "__gb_backpack");
    lua_pushstring(L, m_playerName.c_str());
    lua_setglobal(L, "__gb_playerName");
    LuaApi::pushLighting(L);
    lua_setglobal(L, "Lighting");
    LuaApi::pushSignal(L, SignalKind::Heartbeat, 0);  lua_setglobal(L, "__gb_heartbeat");
    LuaApi::pushSignal(L, SignalKind::InputBegan, 0); lua_setglobal(L, "__gb_inputBegan");
    LuaApi::pushSignal(L, SignalKind::InputEnded, 0); lua_setglobal(L, "__gb_inputEnded");
    LuaApi::pushSignal(L, SignalKind::PlayerAdded, 0);    lua_setglobal(L, "__gb_playerAdded");
    LuaApi::pushSignal(L, SignalKind::PlayerRemoving, 0); lua_setglobal(L, "__gb_playerRemoving");
    LuaApi::pushSignal(L, SignalKind::TagAdded, 0);       lua_setglobal(L, "__gb_tagAdded");
    LuaApi::pushSignal(L, SignalKind::TagRemoved, 0);     lua_setglobal(L, "__gb_tagRemoved");

    lua_register(L, "Explode", l_explode);
    lua_newtable(L);
    lua_pushcfunction(L, snd_play); lua_setfield(L, -2, "Play");
    lua_pushcfunction(L, snd_list); lua_setfield(L, -2, "List");
    lua_setglobal(L, "Sounds");
    lua_newtable(L);
    lua_pushcfunction(L, l_explode);  lua_setfield(L, -2, "Explosion");
    lua_pushcfunction(L, fx_blood);   lua_setfield(L, -2, "Blood");
    lua_pushcfunction(L, fx_oil);     lua_setfield(L, -2, "Oil");
    lua_pushcfunction(L, fx_gibs);    lua_setfield(L, -2, "Gibs");
    lua_pushcfunction(L, fx_sparks);  lua_setfield(L, -2, "Sparks");
    lua_setglobal(L, "Effects");

    lua_newtable(L);
    lua_pushcfunction(L, gui_message); lua_setfield(L, -2, "Message");
    lua_pushcfunction(L, gui_label);   lua_setfield(L, -2, "Label");
    lua_pushcfunction(L, gui_clear);   lua_setfield(L, -2, "Clear");
    lua_setglobal(L, "Gui");

    m_resumeStart = nowSeconds();
    ++m_depth;
    if (luaL_loadbuffer(L, kPrelude, std::strlen(kPrelude), "=prelude") != LUA_OK ||
        lua_pcall(L, 0, 0, 0) != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    --m_depth;
    // For Studio's AI tools: an object by its id number (they refer to objects that way).
    lua_register(L, "__gb_byId", [](lua_State* L) -> int {
        uint64_t id = (uint64_t)luaL_checkinteger(L, 1);
        if (LuaApi::engine(L)->scene()->findById(id)) LuaApi::pushInstance(L, id);
        else lua_pushnil(L);
        return 1;
    });
    // obj:GetAttributeChangedSignal(name) is built in Lua (see the prelude).
    lua_getglobal(L, "__gb_attrSignal");
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.attrSignal");
    lua_pushnil(L);
    lua_setglobal(L, "__gb_attrSignal");

    if (!runScripts) return;
    // Collect first, then run: scripts may add or remove objects as they start.
    std::vector<uint64_t> scripts;
    m_scene->forEach([&](SceneNode* n) {
        if (n->isScript() && n->enabled && !n->isModule) scripts.push_back(n->id);   // modules wait for require()
    });
    for (uint64_t id : scripts)
        if (SceneNode* s = resolve(id)) runScript(s);
}

bool ScriptEngine::runCommand(const std::string& code, std::string& error) {
    if (!m_L) { error = "not running"; return false; }
    lua_State* L = m_L;
    // "= 5 + 5" style: try it as an expression first, so results get printed.
    std::string asExpr = "return " + code;
    bool expr = luaL_loadbuffer(L, asExpr.data(), asExpr.size(), "=CommandBar") == LUA_OK;
    if (!expr) {
        lua_pop(L, 1);
        if (luaL_loadbuffer(L, code.data(), code.size(), "=CommandBar") != LUA_OK) {
            error = lua_tostring(L, -1);
            lua_pop(L, 1);
            return false;
        }
    }
    if (!expr) {
        // Statements run like a script's body, so wait() works in them (errors go to Output).
        lua_State* co = lua_newthread(L);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_xmove(L, co, 1);
        resume(co, ref, 0, L);
        return true;
    }
    m_resumeStart = nowSeconds();
    ++m_depth;
    int top = lua_gettop(L) - 1;
    int status = lua_pcall(L, 0, LUA_MULTRET, 0);
    --m_depth;
    if (status != LUA_OK) {
        error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
        lua_settop(L, top);
        return false;
    }
    int n = lua_gettop(L) - top;
    if (expr && n > 0) {
        std::string out;
        for (int i = 1; i <= n; ++i) {
            size_t len;
            const char* t = luaL_tolstring(L, top + i, &len);
            if (i > 1) out += "  ";
            out.append(t, len);
            lua_pop(L, 1);
        }
        Log::info(out);
    }
    lua_settop(L, top);
    return true;
}

bool ScriptEngine::runChunk(const std::string& code, const std::string& chunkName, std::string& error) {
    if (!m_L) { error = "not running"; return false; }
    lua_State* L = m_L;
    int top = lua_gettop(L);
    if (luaL_loadbuffer(L, code.data(), code.size(), ("=" + chunkName).c_str()) != LUA_OK) {
        error = lua_tostring(L, -1);
        lua_settop(L, top);
        return false;
    }
    m_resumeStart = nowSeconds();
    ++m_depth;
    int status = lua_pcall(L, 0, 0, 0);
    --m_depth;
    if (status != LUA_OK) error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
    lua_settop(L, top);
    return status == LUA_OK;
}

bool ScriptEngine::callRef(int ref, std::string& error) {
    if (!m_L) { error = "not running"; return false; }
    lua_State* L = m_L;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    m_resumeStart = nowSeconds();
    ++m_depth;
    int status = lua_pcall(L, 0, 0, 0);
    --m_depth;
    if (status != LUA_OK) error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
    lua_settop(L, top);
    return status == LUA_OK;
}

void ScriptEngine::runScript(SceneNode* script) {
    lua_State* L = m_L;
    std::string chunk = "=" + script->fullName();
    if (luaL_loadbuffer(L, script->source.data(), script->source.size(), chunk.c_str()) != LUA_OK) {
        Log::error(lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    // Each script gets its own globals table (falling back to the shared one),
    // with `script` pointing at itself.
    lua_newtable(L);
    lua_newtable(L);
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    LuaApi::pushInstance(L, script->id);
    lua_setfield(L, -2, "script");
    lua_setupvalue(L, -2, 1);          // becomes the chunk's _ENV

    lua_State* co = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, co, 1);
    m_started.insert(script->id);
    m_stopped.erase(script->id);
    uint64_t prev = m_current;
    m_current = script->id;
    resume(co, ref, 0);
    m_current = prev;
}

void ScriptEngine::stopScripts(SceneNode* root) {
    if (!root) return;
    std::vector<SceneNode*> stack{root};
    std::unordered_set<uint64_t> ids;
    while (!stack.empty()) {
        SceneNode* n = stack.back(); stack.pop_back();
        if (n->isScript()) ids.insert(n->id);
        for (auto& c : n->children) stack.push_back(c.get());
    }
    if (ids.empty()) return;
    m_stopped.insert(ids.begin(), ids.end());
    for (size_t i = 0; i < m_conns.size(); ++i)
        if (m_conns[i].alive && ids.count(m_conns[i].owner)) disconnect((int)i);
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        if (ids.count(it->owner)) {
            if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, it->ref);
            it = m_waiting.erase(it);
        } else ++it;
    }
}

void ScriptEngine::runScriptsIn(SceneNode* root) {
    if (!m_L || !root) return;
    std::vector<uint64_t> ids;
    std::vector<SceneNode*> stack{root};
    while (!stack.empty()) {
        SceneNode* n = stack.back(); stack.pop_back();
        if (n->isScript() && n->enabled && !n->isModule && !m_started.count(n->id)) ids.push_back(n->id);
        for (auto& c : n->children) stack.push_back(c.get());
    }
    for (uint64_t id : ids)
        if (SceneNode* s = resolve(id)) runScript(s);
}

void ScriptEngine::fireTool(SignalKind kind, uint64_t toolId) { fire(kind, toolId, nullptr); }
void ScriptEngine::fireGui(SignalKind kind, uint64_t id) { fire(kind, id, nullptr); }

void ScriptEngine::fireAnimationEvents() {
    std::vector<Anim::Animator::Event> events;
    events.swap(m_scene->animator().events);
    for (const auto& e : events) {
        if (!m_L) return;
        SignalKind k = e.kind == Anim::Animator::Event::Stopped ? SignalKind::AnimStopped
                     : e.kind == Anim::Animator::Event::Ended   ? SignalKind::AnimEnded
                     : e.kind == Anim::Animator::Event::DidLoop ? SignalKind::AnimDidLoop : SignalKind::KeyframeReached;
        std::string name = e.name;
        fire(k, (uint64_t)e.track, [k, name](lua_State* co) {
            if (k != SignalKind::KeyframeReached) return 0;
            lua_pushstring(co, name.c_str());
            return 1;
        });
    }
}

void ScriptEngine::fireValueChanged(uint64_t id) {
    fire(SignalKind::Changed, id, [this, id](lua_State* co) {
        SceneNode* n = resolve(id);
        if (!n) { lua_pushnil(co); return 1; }
        LuaApi::pushValue(co, *n);
        return 1;
    });
}

void ScriptEngine::setScriptEnabled(SceneNode* script, bool on) {
    if (!script || !script->isScript() || script->enabled == on) return;
    script->enabled = on;
    if (!on) stopScripts(script);
    else if (m_L) runScript(script);
}

void ScriptEngine::stop() {
    if (m_L) lua_close(m_L);
    m_L = nullptr;
    m_waiting.clear();
    m_conns.clear();
    m_stopped.clear();
    m_started.clear();
    m_playersRoot = 0;
    m_saveData = nlohmann::json::object();   // the next game reads its own file
    m_saveLoaded = false;
    m_current = 0;
    m_detached.clear();
    m_gui = GuiState{};
}

void ScriptEngine::reportError(lua_State* co) {
    const char* msg = lua_tostring(co, -1);
    Log::error(msg ? msg : "(script error with no message)");
    lua_pop(co, 1);
}

void ScriptEngine::checkTimeout(lua_State* L) {
    if (m_depth > 0 && nowSeconds() - m_resumeStart > kTimeoutSeconds)
        luaL_error(L, "Script ran for more than %d seconds without waiting - "
                      "did you forget a wait() inside a loop?", (int)kTimeoutSeconds);
}

void ScriptEngine::resume(lua_State* co, int ref, int nargs, lua_State* from) {
    if (!m_L) return;
    if (m_depth == 0) m_resumeStart = nowSeconds();
    ++m_depth;
    int nres = 0;
    int status = lua_resume(co, from ? from : m_L, nargs, &nres);
    --m_depth;

    if (status == LUA_YIELD) {
        // Parked by Signal:Wait()? Then the connection owns the thread now.
        for (auto& c : m_conns) {
            if (c.alive && c.waiter == co && c.threadRef == LUA_NOREF) {
                c.threadRef = ref;
                lua_pop(co, nres);
                return;
            }
        }
        double t = (nres > 0 && lua_isnumber(co, -1)) ? lua_tonumber(co, -1) : 0.0;
        lua_pop(co, nres);
        schedule(co, ref, std::max(t, 0.0), -1);
        return;
    }
    if (status != LUA_OK) reportError(co);
    luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
}

void ScriptEngine::schedule(lua_State* co, int ref, double delay, int startArgs) {
    if (m_stopped.count(m_current)) {   // its script was stopped while it ran
        if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
        return;
    }
    m_waiting.push_back({ref, co, m_time + delay, m_time, startArgs, m_current});
}

void ScriptEngine::update(float dt) {
    if (!m_L) return;
    m_time += dt;

    // Keyboard events.
    if (!ImGui::GetIO().WantTextInput) {
        for (const auto& k : keyTable()) {
            bool began = ImGui::IsKeyPressed(k.key, false);
            bool ended = ImGui::IsKeyReleased(k.key);
            if (!began && !ended) continue;
            const char* name = k.name;
            fire(began ? SignalKind::InputBegan : SignalKind::InputEnded, 0, [name](lua_State* co) {
                lua_newtable(co);
                lua_pushstring(co, name);       lua_setfield(co, -2, "KeyCode");
                lua_pushstring(co, "Keyboard"); lua_setfield(co, -2, "UserInputType");
                lua_pushboolean(co, 0);
                return 2;
            });
        }
    }

    // Wake threads whose wait() is over.
    std::vector<Waiting> ready;
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        if (it->wakeAt <= m_time) { ready.push_back(*it); it = m_waiting.erase(it); }
        else ++it;
    }
    for (auto& w : ready) {
        if (!m_L) return;
        if (m_stopped.count(w.owner)) { luaL_unref(m_L, LUA_REGISTRYINDEX, w.ref); continue; }
        m_current = w.owner;
        if (w.startArgs >= 0) {
            resume(w.co, w.ref, w.startArgs);
        } else {
            lua_pushnumber(w.co, m_time - w.since);
            resume(w.co, w.ref, 1);
        }
    }
    m_current = 0;

    fire(SignalKind::Heartbeat, 0, [dt](lua_State* co) { lua_pushnumber(co, dt); return 1; });

    if (m_gui.messageTime > 0.0f) {
        m_gui.messageTime -= dt;
        if (m_gui.messageTime <= 0.0f) m_gui.message.clear();
    }
}

void ScriptEngine::fire(SignalKind kind, uint64_t id, const std::function<int(lua_State*)>& pushArgs) {
    if (!m_L) return;
    size_t n = m_conns.size();   // connections made while firing wait for next time
    for (size_t i = 0; i < n; ++i) {
        if (!m_L) return;
        Connection& c = m_conns[i];
        if (!c.alive || c.kind != kind || c.id != id) continue;
        uint64_t prev = m_current;
        m_current = c.owner;
        struct Restore { uint64_t& cur; uint64_t val; ~Restore() { cur = val; } } restore{m_current, prev};

        if (c.waiter) {
            // A thread parked in :Wait() — wake it with the event's arguments.
            if (c.threadRef == LUA_NOREF) continue;
            lua_State* co = c.waiter;
            int ref = c.threadRef;
            c.alive = false;
            int nargs = pushArgs ? pushArgs(co) : 0;
            resume(co, ref, nargs);
            continue;
        }

        lua_State* co = lua_newthread(m_L);
        int ref = luaL_ref(m_L, LUA_REGISTRYINDEX);
        lua_rawgeti(co, LUA_REGISTRYINDEX, c.fnRef);
        if (c.once) disconnect((int)i);
        int nargs = pushArgs ? pushArgs(co) : 0;
        resume(co, ref, nargs);
    }
}

void ScriptEngine::fireTouched(uint64_t partId, uint64_t otherId) {
    fire(SignalKind::Touched, partId, [otherId](lua_State* co) {
        LuaApi::pushInstance(co, otherId);
        return 1;
    });
}

void ScriptEngine::fireClicked(uint64_t partId) {
    fire(SignalKind::InputBegan, 0, [](lua_State* co) {
        lua_newtable(co);
        lua_pushstring(co, "Unknown");      lua_setfield(co, -2, "KeyCode");
        lua_pushstring(co, "MouseButton1"); lua_setfield(co, -2, "UserInputType");
        lua_pushboolean(co, 0);
        return 2;
    });
    if (partId) fire(SignalKind::Clicked, partId, nullptr);
}

void ScriptEngine::fireDied(uint64_t rootId) { fire(SignalKind::Died, rootId, nullptr); }

void ScriptEngine::fireMoveToFinished(uint64_t rootId, bool reached) {
    fire(SignalKind::MoveToFinished, rootId, [reached](lua_State* co) { lua_pushboolean(co, reached); return 1; });
}

void ScriptEngine::fireAttributeChanged(uint64_t id, const std::string& name) {
    fire(SignalKind::AttributeChanged, id, [name](lua_State* co) { lua_pushstring(co, name.c_str()); return 1; });
}

void ScriptEngine::fireTag(bool added, uint64_t id, const std::string& tag) {
    fire(added ? SignalKind::TagAdded : SignalKind::TagRemoved, 0, [id, tag](lua_State* co) {
        LuaApi::pushInstance(co, id);
        lua_pushstring(co, tag.c_str());
        return 2;
    });
}

// ---------------------------------------------------------------------------
// Players (for leaderstats): each one has an object outside the world.
// ---------------------------------------------------------------------------

std::filesystem::path ScriptEngine::saveFile() const {
    // One file per game: its published ID if it has one, else its title.
    const GameInfo& info = m_scene->info();
    std::string key = !info.publishedId.empty() ? info.publishedId : info.title.empty() ? "Untitled" : info.title;
    for (char& c : key)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
    return Account::folder() / "savedata" / (key + ".json");
}

const nlohmann::json& ScriptEngine::saveData() {
    if (!m_saveLoaded) {
        m_saveLoaded = true;
        std::ifstream f(saveFile());
        std::stringstream ss;
        ss << f.rdbuf();
        m_saveData = nlohmann::json::parse(ss.str(), nullptr, false);
        if (!m_saveData.is_object()) m_saveData = nlohmann::json::object();
    }
    return m_saveData;
}

void ScriptEngine::setSaveData(const std::string& store, const std::string& key, const nlohmann::json& value) {
    saveData();
    if (value.is_null()) { if (m_saveData.contains(store)) m_saveData[store].erase(key); }
    else m_saveData[store][key] = value;
    std::error_code ec;
    std::filesystem::create_directories(saveFile().parent_path(), ec);
    std::string tmp = saveFile().string() + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); f << m_saveData.dump(1); }
    std::filesystem::rename(tmp, saveFile(), ec);
}

uint64_t ScriptEngine::playerNode(const std::string& name) {
    if (!m_playersRoot || !resolve(m_playersRoot)) {
        auto root = std::make_unique<SceneNode>("Players", NodeKind::Model);
        m_playersRoot = adopt(std::move(root))->id;
    }
    SceneNode* root = resolve(m_playersRoot);
    if (SceneNode* p = root->findChild(name)) return p->id;
    return root->addChild(std::make_unique<SceneNode>(name, NodeKind::Model))->id;
}

std::vector<std::pair<std::string, std::string>> ScriptEngine::leaderstats(const std::string& name) {
    std::vector<std::pair<std::string, std::string>> out;
    SceneNode* root = m_playersRoot ? resolve(m_playersRoot) : nullptr;
    SceneNode* p = root ? root->findChild(name) : nullptr;
    SceneNode* ls = p ? p->findChild("leaderstats") : nullptr;
    if (!ls) return out;
    for (auto& c : ls->children)
        if (c->isValue() && out.size() < 4) out.push_back({c->name, c->valueText()});   // Roblox shows up to 4
    return out;
}

void ScriptEngine::addPlayer(const std::string& name, uint64_t rootId, int userId) {
    if (!m_L) return;
    lua_getglobal(m_L, "__gb_addPlayer");
    lua_pushstring(m_L, name.c_str());
    LuaApi::pushInstance(m_L, rootId);
    lua_pushinteger(m_L, userId);
    if (lua_pcall(m_L, 3, 1, 0) != LUA_OK) { lua_pop(m_L, 1); return; }
    int ref = luaL_ref(m_L, LUA_REGISTRYINDEX);
    fire(SignalKind::PlayerAdded, 0, [ref](lua_State* co) { lua_rawgeti(co, LUA_REGISTRYINDEX, ref); return 1; });
    if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
}

void ScriptEngine::removePlayer(const std::string& name) {
    if (!m_L) return;
    lua_getglobal(m_L, "__gb_removePlayer");
    lua_pushstring(m_L, name.c_str());
    if (lua_pcall(m_L, 1, 1, 0) != LUA_OK || lua_isnil(m_L, -1)) { lua_pop(m_L, 1); return; }
    int ref = luaL_ref(m_L, LUA_REGISTRYINDEX);
    fire(SignalKind::PlayerRemoving, 0, [ref](lua_State* co) { lua_rawgeti(co, LUA_REGISTRYINDEX, ref); return 1; });
    if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
}

int ScriptEngine::connect(SignalKind kind, uint64_t id, int fnRef, bool once) {
    Connection c;
    c.kind = kind; c.id = id; c.fnRef = fnRef; c.once = once; c.owner = m_current;
    m_conns.push_back(c);
    return (int)m_conns.size() - 1;
}

int ScriptEngine::connectWaiter(SignalKind kind, uint64_t id, lua_State* thread) {
    Connection c;
    c.kind = kind; c.id = id; c.waiter = thread; c.threadRef = LUA_NOREF; c.owner = m_current;
    m_conns.push_back(c);
    return (int)m_conns.size() - 1;
}

void ScriptEngine::disconnect(int index) {
    if (index < 0 || index >= (int)m_conns.size()) return;
    Connection& c = m_conns[index];
    if (!c.alive) return;
    c.alive = false;
    if (m_L && c.fnRef != LUA_NOREF) luaL_unref(m_L, LUA_REGISTRYINDEX, c.fnRef);
    c.fnRef = LUA_NOREF;
}

bool ScriptEngine::connected(int index) const {
    return index >= 0 && index < (int)m_conns.size() && m_conns[index].alive;
}

bool ScriptEngine::isKeyDown(const std::string& key) const {
    if (ImGui::GetIO().WantTextInput) return false;
    for (const auto& k : keyTable())
        if (key == k.name) return ImGui::IsKeyDown(k.key);
    return false;
}

// ---------------------------------------------------------------------------
// Object ownership (for Parent = nil / Instance.new / Clone / Destroy)
// ---------------------------------------------------------------------------

namespace {
SceneNode* findIn(SceneNode* n, uint64_t id) {
    if (n->id == id) return n;
    for (auto& c : n->children)
        if (SceneNode* f = findIn(c.get(), id)) return f;
    return nullptr;
}
SceneNode* topOf(SceneNode* n) {
    while (n->parent) n = n->parent;
    return n;
}
} // namespace

SceneNode* ScriptEngine::resolve(uint64_t id) {
    if (!id) return nullptr;
    if (SceneNode* n = m_scene->findById(id)) return n;
    for (auto& d : m_detached)
        if (SceneNode* f = findIn(d.get(), id)) return f;
    return nullptr;
}

SceneNode* ScriptEngine::adopt(std::unique_ptr<SceneNode> n) {
    m_detached.push_back(std::move(n));
    return m_detached.back().get();
}

bool ScriptEngine::setParent(SceneNode* node, SceneNode* newParent, std::string& err) {
    if (m_scene->isProtected(node)) { err = "You can't change the Parent of " + node->name; return false; }
    if (newParent && (newParent == node || node->isAncestorOf(newParent))) {
        err = "Can't put an object inside itself";
        return false;
    }
    if (node->parent == newParent && newParent) return true;

    // Take ownership of the node, wherever it currently lives.
    std::unique_ptr<SceneNode> owned;
    bool inScene = topOf(node) == m_scene->root();
    if (inScene) {
        owned = m_scene->detach(node);
    } else if (node->parent) {
        owned = node->parent->detachChild(node);
    } else {
        for (auto it = m_detached.begin(); it != m_detached.end(); ++it)
            if (it->get() == node) { owned = std::move(*it); m_detached.erase(it); break; }
    }
    if (!owned) { err = "Couldn't move " + node->name; return false; }

    if (newParent) {
        SceneNode* moved = newParent->addChild(std::move(owned));
        m_scene->markDirty();
        // Like Roblox: scripts start when they arrive in the world (a clone parented in, say).
        if (!inScene && topOf(moved) == m_scene->root()) runScriptsIn(moved);
    } else {
        m_detached.push_back(std::move(owned));
    }
    return true;
}

bool ScriptEngine::destroy(SceneNode* node, std::string& err) {
    if (m_scene->isProtected(node)) { err = "You can't Destroy " + node->name; return false; }
    stopScripts(node);
    if (topOf(node) == m_scene->root()) {
        m_scene->removeNode(node);
    } else if (node->parent) {
        node->parent->removeChild(node);
    } else {
        m_detached.erase(std::remove_if(m_detached.begin(), m_detached.end(),
            [node](const auto& p) { return p.get() == node; }), m_detached.end());
    }
    return true;
}

// ---------------------------------------------------------------------------
// Lighting.ClockTime — moves the sun and blends day / sunset / night colours.
// ---------------------------------------------------------------------------

float& ScriptEngine::clockTime() { return m_scene->environment().clockTime; }

void ScriptEngine::applyClockTime() {
    EnvironmentPresets::applyTimeOfDay(m_scene->environment(), clockTime());
}

// ---------------------------------------------------------------------------

bool ScriptEngine::checkSyntax(const std::string& source, std::string& error, int& line) {
    lua_State* L = luaL_newstate();
    bool ok = luaL_loadbuffer(L, source.data(), source.size(), "=script") == LUA_OK;
    line = 0;
    if (!ok) {
        error = lua_tostring(L, -1);
        // Messages look like "script:12: 'end' expected near <eof>".
        if (error.rfind("script:", 0) == 0) {
            line = std::atoi(error.c_str() + 7);
            size_t colon = error.find(':', 7);
            if (colon != std::string::npos) error = error.substr(colon + 2);
        }
    }
    lua_close(L);
    return ok;
}
