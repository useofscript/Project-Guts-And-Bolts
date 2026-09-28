#pragma once
#include <imgui.h>

class Scene;
struct GuiState;

// In-game overlay: health bar, script labels / messages and the death screen.
namespace Hud {
void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const GuiState& gui);
}
