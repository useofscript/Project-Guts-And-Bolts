#include "EnvironmentPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/Environment.h"

#include <imgui.h>

EnvironmentPanel::EnvironmentPanel(Scene* scene) : m_scene(scene) {}

void EnvironmentPanel::render() {
    ImGui::Begin("Environment");

    Environment& e = m_scene->environment();

    ImGui::TextDisabled("Presets");
    if (ImGui::Button("Day"))      e = EnvironmentPresets::day();
    ImGui::SameLine();
    if (ImGui::Button("Sunset"))   e = EnvironmentPresets::sunset();
    ImGui::SameLine();
    if (ImGui::Button("Night"))    e = EnvironmentPresets::night();
    ImGui::SameLine();
    if (ImGui::Button("Overcast")) e = EnvironmentPresets::overcast();
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Azimuth",   &e.sunAzimuth,   0.0f, 360.0f, "%.0f\xc2\xb0");
        ImGui::SliderFloat("Elevation", &e.sunElevation, -90.0f, 90.0f, "%.0f\xc2\xb0");
        ImGui::ColorEdit3 ("Color##sun",     &e.sunColor.x);
        ImGui::SliderFloat("Intensity##sun", &e.sunIntensity, 0.0f, 4.0f);
        ImGui::Checkbox   ("Cast Shadows",   &e.shadows);
    }

    if (ImGui::CollapsingHeader("Ambient", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3 ("Color##amb",     &e.ambientColor.x);
        ImGui::SliderFloat("Intensity##amb", &e.ambientIntensity, 0.0f, 2.0f);
    }

    if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox  ("Show Sky", &e.showSky);
        ImGui::ColorEdit3("Zenith",   &e.skyZenith.x);
        ImGui::ColorEdit3("Horizon",  &e.skyHorizon.x);
        ImGui::ColorEdit3("Ground",   &e.skyGround.x);
    }

    if (ImGui::CollapsingHeader("Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox   ("Fog",       &e.fogEnabled);
        ImGui::ColorEdit3 ("Fog Color", &e.fogColor.x);
        ImGui::SliderFloat("Density",   &e.fogDensity, 0.0f, 0.08f, "%.4f");
    }

    ImGui::End();
}
