#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "../scene/Physics.h"
#include "../scripting/ScriptEngine.h"

class Scene;

// Everything that happens while a game is running: scripts, physics, the
// character and Touched / Died events. The editor's Play button and the
// Guts&BoltsPlayer app both drive the game through this class.
class GameSession {
public:
    // Solo = normal. Host = like Solo, but others can join. Client = joined
    // someone else's game: no scripts or world physics here (the host runs
    // them); we only move our own character and report touches / clicks.
    enum class Role { Solo, Host, Client };

    explicit GameSession(Scene* scene);

    void setRole(Role r) { m_role = r; }
    // Studio's "Run": the world simulates and scripts run, but there's no player.
    void setRunOnly(bool on) { m_runOnly = on; }
    bool runOnly() const { return m_runOnly; }
    Role role() const { return m_role; }
    // Client mode: where touches and clicks get sent (to the host).
    std::function<void(uint64_t part, const std::string& limb)> onTouch;
    std::function<void(uint64_t part)>                          onClick;

    void start();
    void stop();
    bool running() const { return m_running; }

    // One frame. `cameraYaw` makes WASD move relative to the camera;
    // `acceptInput` = false ignores the keyboard (e.g. while typing).
    void update(float dt, float cameraYaw, bool acceptInput);
    void click(uint64_t partId);                 // left-click in the 3D view
    // On-screen joystick (x = right, y = forward) and jump button, for touch screens.
    void setTouchInput(glm::vec2 move, bool jump) { m_touchMove = move; m_touchJump = jump; }

    ScriptEngine& scripts() { return m_scripts; }
    GuiState&     gui()     { return m_scripts.gui(); }

private:
    Scene*       m_scene;
    Physics      m_physics;
    ScriptEngine m_scripts;
    bool         m_running = false;
    Role         m_role = Role::Solo;
    glm::vec2    m_touchMove{0.0f};
    bool         m_touchJump = false;
    bool         m_runOnly = false;
};
