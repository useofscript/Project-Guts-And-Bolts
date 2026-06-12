#pragma once

class Scene;

// Lighting & atmosphere inspector — edits the scene's Environment (sun,
// ambient, sky and fog) live, with one-click presets.
class EnvironmentPanel {
public:
    explicit EnvironmentPanel(Scene* scene);
    void render();

private:
    Scene* m_scene;
};
