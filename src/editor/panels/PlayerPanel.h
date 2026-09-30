#pragma once

class Scene;
struct WorldSettings;

// The game's damage, death and blood rules (shown in the Player panel and in
// Game Settings): death style, gore, blood colour and amount, fall damage...
void drawGameRules(WorldSettings& w);

// Inspector for the character's Humanoid properties (walk speed, jump power,
// health, …) — the Roblox-style "player properties".
class PlayerPanel {
public:
    explicit PlayerPanel(Scene* scene);
    void render();

private:
    Scene* m_scene;
};
