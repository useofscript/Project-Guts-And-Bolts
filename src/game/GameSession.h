#pragma once
#include <cstdint>
#include <memory>
#include "../scene/Physics.h"
#include "../scripting/ScriptEngine.h"

class Scene;

// Everything that happens while a game is running: scripts, physics, the
// character and Touched / Died events. The editor's Play button and the
// Guts&BoltsPlayer app both drive the game through this class.
class GameSession {
public:
    explicit GameSession(Scene* scene);

    void start();
    void stop();
    bool running() const { return m_running; }

    // One frame. `cameraYaw` makes WASD move relative to the camera;
    // `acceptInput` = false ignores the keyboard (e.g. while typing).
    void update(float dt, float cameraYaw, bool acceptInput);
    void click(uint64_t partId);                 // left-click in the 3D view

    ScriptEngine& scripts() { return m_scripts; }
    GuiState&     gui()     { return m_scripts.gui(); }

private:
    Scene*       m_scene;
    Physics      m_physics;
    ScriptEngine m_scripts;
    bool         m_running = false;
};
