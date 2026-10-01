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
// `character`: false hides the health bar (Studio's Run mode has no character).
void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const GuiState& gui, float topOffset = 0.0f,
          bool character = true);

// Speech bubbles over characters' heads. `bubbles` maps a character's name to its text.
void drawBubbles(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const glm::mat4& viewProj,
                 const std::unordered_map<std::string, std::pair<std::string, float>>& bubbles);
// Names over characters' heads, like Roblox: every player (you too, until the
// camera gets close) and NPCs. Others show a small health bar when they're hurt.
void drawNameTags(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const glm::mat4& viewProj,
                  const glm::vec3& cameraPos);
// Everyone in the game (top-right, under the health bar). Administrators get
// a little floating badge next to their name.
// The player list (leaderboard), top right. Like old Roblox it folds away: click
// the little arrow on its title bar (or press Tab - the caller handles the key).
// Returns the name of a player whose row was clicked ("" if none), and where.
std::string drawPlayerList(ImDrawList* dl, ImVec2 min, ImVec2 max, const std::vector<PlayerEntry>& players,
                           bool& open, ImVec2* clickedAt = nullptr, const ImVec2* tap = nullptr);
// The tool hotbar along the bottom (slots 1-9, the held one lit up). Returns the
// slot that was clicked or tapped (`tap`: a finger's tap, if any), or -1.
int  drawHotbar(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const ImVec2* tap = nullptr);
// Is `p` on the hotbar? (so clicking a slot doesn't also swing the tool)
bool overHotbar(ImVec2 min, ImVec2 max, Scene& scene, ImVec2 p);
}
