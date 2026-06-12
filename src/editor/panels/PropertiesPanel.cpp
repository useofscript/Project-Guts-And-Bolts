#include "PropertiesPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"

#include <imgui.h>
#include <cstring>

PropertiesPanel::PropertiesPanel(Scene* scene) : m_scene(scene) {}

void PropertiesPanel::render() {
    ImGui::Begin("Properties");

    SceneNode* node = m_scene->selected();
    if (!node) {
        ImGui::TextDisabled("No object selected.");
        ImGui::TextDisabled("Pick one in the Outliner or Viewport.");
        ImGui::End();
        return;
    }

    // --- Name ---
    char buf[128];
    std::strncpy(buf, node->name.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Name", buf, sizeof(buf)))
        node->name = buf;

    ImGui::Checkbox("Visible", &node->visible);
    ImGui::Separator();

    // --- Transform ---
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &node->transform.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &node->transform.rotation.x, 0.5f);
        ImGui::DragFloat3("Scale",    &node->transform.scale.x,    0.05f, 0.001f, 1000.0f);

        if (ImGui::Button("Reset Transform")) {
            node->transform.position = {0, 0, 0};
            node->transform.rotation = {0, 0, 0};
            node->transform.scale    = {1, 1, 1};
        }
    }

    // --- Material ---
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Base Color", &node->color.x);
    }

    ImGui::End();
}
