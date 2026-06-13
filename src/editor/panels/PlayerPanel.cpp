#include "PlayerPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/Player.h"

#include <imgui.h>

PlayerPanel::PlayerPanel(Scene* scene) : m_scene(scene) {}

void PlayerPanel::render() {
    ImGui::Begin("Player");

    Player* player = m_scene->player();
    if (!player) {
        ImGui::TextDisabled("No player in the scene.");
        ImGui::End();
        return;
    }

    Humanoid& h = player->humanoid();

    if (ImGui::CollapsingHeader("Humanoid", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Walk Speed", &h.walkSpeed, 0.0f, 16.0f);
        ImGui::SliderFloat("Jump Power", &h.jumpPower, 0.0f, 16.0f);
        ImGui::Checkbox   ("Auto Rotate", &h.autoRotate);
    }

    if (ImGui::CollapsingHeader("Health", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Max Health", &h.maxHealth, 1.0f, 500.0f);
        if (h.health > h.maxHealth) h.health = h.maxHealth;
        ImGui::SliderFloat("Health", &h.health, 0.0f, h.maxHealth);

        float frac = (h.maxHealth > 0.0f) ? h.health / h.maxHealth : 0.0f;
        ImVec4 bar = frac > 0.5f ? ImVec4(0.30f, 0.80f, 0.30f, 1.0f)
                   : frac > 0.2f ? ImVec4(0.90f, 0.75f, 0.20f, 1.0f)
                                 : ImVec4(0.85f, 0.25f, 0.25f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar);
        ImGui::ProgressBar(frac, ImVec2(-1, 0));
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Press Play, then move with WASD + Space.");

    ImGui::End();
}
