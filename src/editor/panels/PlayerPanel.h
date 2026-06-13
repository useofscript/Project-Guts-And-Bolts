#pragma once

class Scene;

// Inspector for the character's Humanoid properties (walk speed, jump power,
// health, …) — the Roblox-style "player properties".
class PlayerPanel {
public:
    explicit PlayerPanel(Scene* scene);
    void render();

private:
    Scene* m_scene;
};
