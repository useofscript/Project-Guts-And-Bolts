#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "../scene/Physics.h"
#include "../scripting/ScriptEngine.h"
#include "GameGui.h"

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
    std::function<void(uint64_t button)>                        onGuiClick;   // a game UI button

    void start();
    void stop();
    bool running() const { return m_running; }

    // One frame. `cameraYaw` makes WASD move relative to the camera;
    // `acceptInput` = false ignores the keyboard (e.g. while typing).
    // swimLook: the camera's up/down for swimming (see Player::setSwimInput).
    void update(float dt, float cameraYaw, bool acceptInput, float swimLook = 0.0f);
    void click(uint64_t partId);                 // left-click in the 3D view
    // Game UI: pointer events from GameGui (clicks go to the host in multiplayer).
    void guiEvents(const std::vector<GameGui::Event>& events);
    // On-screen joystick (x = right, y = forward) and jump button, for touch screens.
    void setTouchInput(glm::vec2 move, bool jump) { m_touchMove = move; m_touchJump = jump; }

    // Tools: the held tool's number keys, Backspace to drop, a hotbar click.
    void selectToolSlot(int slot);
    // Your gear from the catalog: given now and again each time you respawn, like a
    // StarterPack tool. (Only where tools run: playing alone or hosting.)
    void addGear(std::unique_ptr<SceneNode> tool);
    void dropTool();

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

    // Tools
    void setupTools();                         // take StarterPack tools out, hand them out
    void giveStarterTools();
    void pickUpTools(const std::vector<TouchEvent>& touches);
    void reachCheckpoints(const std::vector<TouchEvent>& touches);   // parts called "Checkpoint"
    std::vector<std::unique_ptr<SceneNode>> m_starterPack;   // templates (like Roblox's StarterPack)
    std::unordered_map<uint64_t, double> m_noPickupUntil;    // just dropped: don't grab it straight back
    double       m_time = 0.0;
    bool         m_toolDown = false;          // mouse held after activating the tool
};
