#include "Pad.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace Pad {
namespace {
double g_padAt = -1.0, g_kbAt = 0.0;
std::map<int, float> g_injected;

float key(ImGuiKey k) {
    const ImGuiKeyData* d = ImGui::GetKeyData(k);
    return d ? std::max(d->AnalogValue, d->Down ? 1.0f : 0.0f) : 0.0f;
}

glm::vec2 stick(ImGuiKey left, ImGuiKey right, ImGuiKey up, ImGuiKey down) {
    glm::vec2 v(key(right) - key(left), key(up) - key(down));
    const float len = glm::length(v);
    const float dead = 0.15f;
    if (len <= dead) return glm::vec2(0.0f);
    // Rescale past the dead zone so a small push is still a slow walk.
    return v / len * std::min(1.0f, (len - dead) / (1.0f - dead));
}
} // namespace

void update() {
    const double now = ImGui::GetTime();
    // Anything on the pad?
    bool pad = false;
    for (int k = ImGuiKey_GamepadStart; k <= ImGuiKey_GamepadRStickDown && !pad; ++k) pad = key((ImGuiKey)k) > 0.4f;
    if (pad) g_padAt = now;
    // Anything on the keyboard or mouse?
    const ImGuiIO& io = ImGui::GetIO();
    bool kb = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f;
    for (int b = 0; b < 3 && !kb; ++b) kb = io.MouseDown[b];
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart && !kb; ++k) kb = ImGui::IsKeyDown((ImGuiKey)k);
    if (kb) g_kbAt = now;
}

bool connected() {
    return !g_injected.empty() || (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
}

glm::vec2 leftStick() {
    return stick(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown);
}
glm::vec2 rightStick() {
    return stick(ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight, ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown);
}
float leftTrigger()  { return key(ImGuiKey_GamepadL2); }
float rightTrigger() { return key(ImGuiKey_GamepadR2); }
bool inUse() { return connected() && g_padAt >= g_kbAt; }

void inject(int k, float value) { g_injected[k] = value; }
void injectClear() {
    for (auto& [k, v] : g_injected) v = 0.0f;
    injectApply();
    g_injected.clear();
}
void injectApply() {
    ImGuiIO& io = ImGui::GetIO();
    if (!g_injected.empty()) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;   // (else ImGui ignores the pad keys)
    for (auto& [k, v] : g_injected) io.AddKeyAnalogEvent((ImGuiKey)k, v > 0.1f, v);
}

} // namespace Pad
