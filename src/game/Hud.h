#pragma once
#include <imgui.h>

class Scene;
struct GuiState;

// In-game overlay: health bar, script labels / messages and the death screen.
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hud {
void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const GuiState& gui);

// Speech bubbles over characters' heads. `bubbles` maps a character's name to its text.
void drawBubbles(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const glm::mat4& viewProj,
                 const std::unordered_map<std::string, std::pair<std::string, float>>& bubbles);
// Names of everyone in the game (top-right, under the health bar).
void drawPlayerList(ImDrawList* dl, ImVec2 min, ImVec2 max, const std::vector<std::string>& names);
}
