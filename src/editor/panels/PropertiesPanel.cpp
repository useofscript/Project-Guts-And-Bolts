#include "PropertiesPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../renderer/MeshLibrary.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

PropertiesPanel::PropertiesPanel(Scene* scene, std::function<void(SceneNode*)> openScript)
    : m_scene(scene), m_openScript(std::move(openScript)) {}

void PropertiesPanel::render() {
    ImGui::Begin("Properties");

    SceneNode* node = m_scene->selected();
    if (!node) {
        ImGui::TextDisabled("No object selected.");
        ImGui::TextDisabled("Pick one in the Explorer or Viewport.");
        ImGui::End();
        return;
    }

    const char* cls = node == m_scene->root()          ? "Workspace"
                    : node->kind == NodeKind::Script   ? "Script"
                    : node->kind == NodeKind::Model    ? "Model" : "Part";
    ImGui::TextDisabled("%s", cls);

    // --- Name ---
    ImGui::BeginDisabled(m_scene->isProtected(node));
    ImGui::InputText("Name", &node->name);
    ImGui::EndDisabled();

    // --- Scripts: just the code ---
    if (node->isScript()) {
        ImGui::Checkbox("Enabled", &node->scriptEnabled);
        ImGui::Spacing();
        if (ImGui::Button("Edit Script", ImVec2(-1, 36)) && m_openScript) m_openScript(node);
        ImGui::Spacing();
        ImGui::TextDisabled("Runs when you press Play.");
        ImGui::TextDisabled("Inside the code, 'script.Parent' is the");
        ImGui::TextDisabled("object this script is inside of.");
        ImGui::End();
        return;
    }

    if (node == m_scene->root()) {
        ImGui::Separator();
        ImGui::TextDisabled("The Workspace holds everything in your game.");
        ImGui::DragFloat("Gravity", &m_scene->world().gravity, 0.1f, 0.0f, 200.0f);
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Visible", &node->visible);
    ImGui::Separator();

    // --- Transform ---
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &node->transform.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &node->transform.rotation.x, 0.5f);
        ImGui::DragFloat3("Size",     &node->transform.scale.x,    0.05f, 0.001f, 1000.0f);

        if (ImGui::Button("Reset Transform")) {
            node->transform.position = {0, 0, 0};
            node->transform.rotation = {0, 0, 0};
            node->transform.scale    = {1, 1, 1};
        }
    }

    if (node->kind != NodeKind::Part) {
        ImGui::End();
        return;
    }

    // --- Appearance ---
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", &node->color.x);

        const char* shapes[] = { "Cube", "Sphere", "Plane", "Cylinder" };
        int shape = (int)node->primitiveType - 1;
        if (ImGui::Combo("Shape", &shape, shapes, IM_ARRAYSIZE(shapes))) {
            node->primitiveType = (PrimitiveType)(shape + 1);
            node->mesh = MeshLibrary::get(node->primitiveType);
        }

        const char* materials[] = { "Plastic", "Metal", "Neon", "Wood" };
        int mat = (int)node->material;
        if (ImGui::Combo("Material", &mat, materials, IM_ARRAYSIZE(materials)))
            node->material = (Material)mat;

        ImGui::SliderFloat("Transparency", &node->transparency, 0.0f, 1.0f);
    }

    // --- Behavior ---
    if (ImGui::CollapsingHeader("Behavior", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Anchored",    &node->anchored);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unanchored parts fall with gravity in Play mode");
        ImGui::Checkbox("Can Collide", &node->canCollide);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn off to let things pass through");
        ImGui::Checkbox("Cast Shadow", &node->castShadow);
    }

    if (!m_scene->isCharacterPart(node)) {
        ImGui::Spacing();
        if (ImGui::Button("Add Script inside", ImVec2(-1, 0))) {
            auto s = std::make_unique<SceneNode>("Script", NodeKind::Script);
            s->source = "local part = script.Parent\n\nprint(\"Hello from \" .. part.Name)\n";
            SceneNode* raw = m_scene->insert(std::move(s), node);
            m_scene->select(raw);
            if (m_openScript) m_openScript(raw);
        }
    }

    ImGui::End();
}
