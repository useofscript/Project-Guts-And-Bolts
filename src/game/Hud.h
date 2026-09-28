#pragma once
#include <imgui.h>

class Scene;
struct GuiState;

// In-game overlay: health bar, script labels / messages and the death screen.
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include "PlayerEntry.h"

namespace Hud {
// `topOffset` pushes the top-left labels down (under the touch buttons).
void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const GuiState& gui, float topOffset = 0.0f);

// Speech bubbles over characters' heads. `bubbles` maps a character's name to its text.
void drawBubbles(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const glm::mat4& viewProj,
                 const std::unordered_map<std::string, std::pair<std::string, float>>& bubbles);
// Everyone in the game (top-right, under the health bar). Administrators get
// a little floating badge next to their name.
void drawPlayerList(ImDrawList* dl, ImVec2 min, ImVec2 max, const std::vector<PlayerEntry>& players);
}
