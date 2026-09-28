#include "SettingsWindow.h"
#include "../core/Settings.h"

#include <imgui.h>
#include <cstring>

namespace SettingsWindow {

namespace {
void help(const char* text) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}
} // namespace

void draw(bool* open) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Settings", open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    GraphicsSettings& s = GraphicsSettings::get();
    GraphicsSettings before = s;

    ImGui::SeparatorText("Frame rate");
    ImGui::Checkbox("VSync", &s.vsync);
    help("Locks the frame rate to your monitor (smooth, no tearing).\nTurn off to unlock the FPS.");
    ImGui::BeginDisabled(s.vsync);
    const char* caps[] = {"Unlimited", "30", "60", "120", "144", "165", "240"};
    const int   capValues[] = {0, 30, 60, 120, 144, 165, 240};
    int capIdx = 0;
    for (int i = 0; i < IM_ARRAYSIZE(capValues); ++i) if (capValues[i] == s.fpsCap) capIdx = i;
    if (ImGui::Combo("FPS Limit", &capIdx, caps, IM_ARRAYSIZE(caps))) s.fpsCap = capValues[capIdx];
    ImGui::EndDisabled();
    ImGui::Checkbox("Show FPS", &s.showFps);
    ImGui::TextDisabled("Current: %.0f FPS", ImGui::GetIO().Framerate);

    ImGui::SeparatorText("Graphics");
    const char* presets[] = {"Low", "Medium", "High", "Ultra", "Custom"};
    int q = s.quality;
    if (ImGui::Combo("Quality", &q, presets, IM_ARRAYSIZE(presets)) && q != GraphicsSettings::Custom)
        s.applyPreset(q);
    help("Low = fastest, Ultra = best looking");

    bool custom = false;
    const char* resNames[] = {"1024 (fast)", "2048", "4096 (sharpest)"};
    const int   resValues[] = {1024, 2048, 4096};
    int resIdx = s.shadowRes >= 4096 ? 2 : s.shadowRes >= 2048 ? 1 : 0;
    if (ImGui::Combo("Shadow Detail", &resIdx, resNames, 3)) { s.shadowRes = resValues[resIdx]; custom = true; }
    const char* sq[] = {"Hard", "Soft", "Realistic (contact-hardening)"};
    custom |= ImGui::Combo("Shadow Style", &s.shadowQuality, sq, 3);
    custom |= ImGui::Checkbox("Ambient Occlusion", &s.ssao);
    help("Darkens corners and contact points");
    custom |= ImGui::Checkbox("Bloom", &s.bloom);
    help("Glow around bright lights and Neon");
    custom |= ImGui::Checkbox("Anti-aliasing (FXAA)", &s.fxaa);
    help("Smooths jagged edges");
    custom |= ImGui::Checkbox("Tone mapping & colour grading", &s.postFx);
    custom |= ImGui::SliderFloat("Render Scale", &s.renderScale, 0.5f, 2.0f, "%.2fx");
    help("Below 1 = faster but blurrier. Above 1 = super-sampling (very sharp, slow).");
    custom |= ImGui::SliderInt("Max Lights", &s.maxLights, 0, 32);
    if (custom) s.quality = GraphicsSettings::Custom;

    ImGui::Spacing();
    if (ImGui::Button("Reset to High")) s.applyPreset(GraphicsSettings::High);

    if (std::memcmp(&before, &s, sizeof(GraphicsSettings)) != 0) s.save();
    ImGui::End();
}

} // namespace SettingsWindow
