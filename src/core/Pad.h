#pragma once
#include <glm/glm.hpp>

// Game controllers (Xbox, PlayStation, Switch Pro and other pads). Dear ImGui's
// window backends read the first pad into its gamepad keys
// (ImGuiKey_GamepadFaceDown = A / Cross...); this adds the sticks and triggers as
// numbers, and remembers whether the pad or the keyboard and mouse was used last
// (so on-screen hints can show the right buttons).
namespace Pad {

// Once a frame, after ImGui::NewFrame (AppWindow::beginFrame does it).
void update();

bool connected();
// The sticks, -1..1 (y up = pushed forward), with a small dead zone in the middle.
glm::vec2 leftStick();
glm::vec2 rightStick();
float leftTrigger();    // 0..1
float rightTrigger();
// The pad was used more recently than the keyboard and mouse.
bool inUse();

// Tests: pretend a pad is plugged in, with this stick / trigger / button
// (ImGuiKey_Gamepad*) held this far (0..1). injectApply() hands them to ImGui (call
// it before ImGui::NewFrame); injectClear() lets go of everything.
void inject(int key, float value);
void injectApply();
void injectClear();

} // namespace Pad
