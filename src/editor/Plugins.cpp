#include "Plugins.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scripting/LuaApi.h"
#include "../scripting/ScriptEngine.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

Plugins::Plugin* self(lua_State* L) {
    return static_cast<Plugins::Plugin*>(lua_touserdata(L, lua_upvalueindex(1)));
}
Scene* sceneOf(lua_State* L) { return LuaApi::engine(L)->scene(); }

// plugin:Button(label, tooltip, function)
int l_button(lua_State* L) {
    Plugins::Plugin* p = self(L);
    int base = lua_istable(L, 1) ? 2 : 1;   // works as plugin:Button(...) and plugin.Button(...)
    const char* label = luaL_checkstring(L, base);
    const char* tip = lua_isstring(L, base + 1) ? lua_tostring(L, base + 1) : "";
    luaL_checktype(L, base + 2, LUA_TFUNCTION);
    lua_pushvalue(L, base + 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (p->buttons.size() < 12) p->buttons.push_back({label, tip, ref});
    return 0;
}

// Selection:Get() -> { selected objects }
int l_selGet(lua_State* L) {
    lua_newtable(L);
    int i = 1;
    for (SceneNode* n : sceneOf(L)->selection()) {
        LuaApi::pushInstance(L, n->id);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// Selection:Set({ objects })
int l_selSet(lua_State* L) {
    int arg = lua_gettop(L) >= 2 ? 2 : 1;
    Scene* scene = sceneOf(L);
    scene->deselect();
    if (!lua_istable(L, arg)) return 0;
    lua_Integer n = luaL_len(L, arg);
    for (lua_Integer k = 1; k <= n; ++k) {
        lua_rawgeti(L, arg, k);
        if (SceneNode* node = LuaApi::checkNode(L, -1)) scene->addToSelection(node);
        lua_pop(L, 1);
    }
    return 0;
}

} // namespace

Plugins::Plugins(Scene* scene) : m_scene(scene) {}
Plugins::~Plugins() = default;

std::filesystem::path Plugins::folder() {
    std::error_code ec;
    auto p = Paths::appFolder() / "plugins";
    std::filesystem::create_directories(p, ec);
    return p;
}

void Plugins::reload() {
    for (auto& p : m_plugins) if (p->engine) p->engine->stop();
    m_plugins.clear();
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(folder(), ec))
        if (e.is_regular_file() && e.path().extension() == ".lua") files.push_back(e.path());
    std::sort(files.begin(), files.end());

    for (const auto& f : files) {
        auto p = std::make_unique<Plugin>();
        p->file = f;
        p->name = f.stem().string();
        std::ifstream in(f, std::ios::binary);
        std::stringstream src;
        src << in.rdbuf();

        p->engine = std::make_unique<ScriptEngine>(m_scene);
        p->engine->start(false);
        lua_State* L = p->engine->lua();
        // plugin = { Name = "...", Button = function }
        lua_newtable(L);
        lua_pushstring(L, p->name.c_str());
        lua_setfield(L, -2, "Name");
        lua_pushlightuserdata(L, p.get());
        lua_pushcclosure(L, l_button, 1);
        lua_setfield(L, -2, "Button");
        lua_setglobal(L, "plugin");
        // Selection = { Get = ..., Set = ... }
        lua_newtable(L);
        lua_pushcfunction(L, l_selGet);
        lua_setfield(L, -2, "Get");
        lua_pushcfunction(L, l_selSet);
        lua_setfield(L, -2, "Set");
        lua_setglobal(L, "Selection");

        if (!p->engine->runChunk(src.str(), p->name, p->error))
            Log::error("Plugin " + p->name + ": " + p->error);
        else if (p->buttons.empty())
            Log::warn("Plugin " + p->name + " didn't add any buttons (use plugin:Button).");
        m_plugins.push_back(std::move(p));
    }
    if (!m_plugins.empty()) Log::system("Loaded " + std::to_string(m_plugins.size()) + " plugin(s).");
}

void Plugins::click(size_t plugin, size_t button) {
    if (plugin >= m_plugins.size()) return;
    Plugin& p = *m_plugins[plugin];
    if (button >= p.buttons.size() || !p.engine) return;
    std::string err;
    if (!p.engine->callRef(p.buttons[button].ref, err)) Log::error("Plugin " + p.name + ": " + err);
}
