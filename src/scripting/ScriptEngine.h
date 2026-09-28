#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct lua_State;
class Scene;
class SceneNode;
enum class SignalKind : int;

// Things scripts can show on screen during Play (see the Gui table in Lua).
struct GuiState {
    std::map<std::string, std::string> labels;   // Gui.Label(key, text)
    std::string message;                         // Gui.Message(text, seconds)
    float       messageTime = 0.0f;
};

// Runs the Lua code in every Script object while the game is in Play mode.
//
// Each script body — and every event callback — runs in its own coroutine, so
// wait() can pause it without freezing the rest of the game. A per-resume
// time limit catches accidental infinite loops (e.g. `while true do end`).
class ScriptEngine {
public:
    explicit ScriptEngine(Scene* scene);
    ~ScriptEngine();

    void start();                 // create a fresh Lua world and run all Scripts
    void stop();                  // tear everything down
    bool running() const { return m_L != nullptr; }

    // Called once per frame in Play mode.
    void update(float dt);
    void fireTouched(uint64_t partId, uint64_t otherId);
    void fireClicked(uint64_t partId);
    void fireDied();

    GuiState& gui() { return m_gui; }

    // Compile without running — used by the script editor for live error checks.
    static bool checkSyntax(const std::string& source, std::string& error, int& line);

    // ---- used by the Lua bindings --------------------------------------------
    Scene*     scene() { return m_scene; }
    double     time() const { return m_time; }
    SceneNode* resolve(uint64_t id);                 // scene or detached (Parent = nil)
    SceneNode* adopt(std::unique_ptr<SceneNode> n);  // new object with Parent = nil
    bool       setParent(SceneNode* node, SceneNode* newParent, std::string& err);
    bool       destroy(SceneNode* node, std::string& err);
    int        connect(SignalKind kind, uint64_t id, int fnRef, bool once);
    int        connectWaiter(SignalKind kind, uint64_t id, lua_State* thread);
    void       disconnect(int index);
    bool       connected(int index) const;
    void       resume(lua_State* co, int ref, int nargs, lua_State* from = nullptr);
    void       schedule(lua_State* co, int ref, double delay, int startArgs);
    void       checkTimeout(lua_State* L);
    float&     clockTime();
    void       applyClockTime();
    bool       isKeyDown(const std::string& key) const;

private:
    struct Waiting {
        int        ref;
        lua_State* co;
        double     wakeAt;
        double     since;
        int        startArgs;   // >= 0: not started yet (task.delay), else a wait()
    };
    struct Connection {
        SignalKind kind;
        uint64_t   id;
        int        fnRef     = -2;       // LUA_NOREF
        int        threadRef = -2;       // for :Wait()
        lua_State* waiter    = nullptr;
        bool       once      = false;
        bool       alive     = true;
    };

    void openLibraries();
    void runScript(SceneNode* script);
    void fire(SignalKind kind, uint64_t id, const std::function<int(lua_State*)>& pushArgs);
    void reportError(lua_State* co);

    Scene*     m_scene;
    lua_State* m_L = nullptr;
    double     m_time = 0.0;
    double     m_resumeStart = 0.0;
    int        m_depth = 0;

    std::vector<Waiting>                    m_waiting;
    std::vector<Connection>                 m_conns;
    std::vector<std::unique_ptr<SceneNode>> m_detached;
    GuiState                                m_gui;
};
