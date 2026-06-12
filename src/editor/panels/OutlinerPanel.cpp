#include "OutlinerPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"

#include <imgui.h>

OutlinerPanel::OutlinerPanel(Scene* scene) : m_scene(scene) {}

void OutlinerPanel::drawNode(SceneNode* node) {
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_DefaultOpen;

    if (node->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (node->selected)         flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(node);

    // Dim the label for hidden objects.
    bool dim = !node->visible;
    if (dim) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));

    bool open = ImGui::TreeNodeEx(node->name.c_str(), flags);

    if (dim) ImGui::PopStyleColor();

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        m_scene->select(node);

    if (ImGui::BeginPopupContextItem()) {
        m_scene->select(node);
        if (ImGui::MenuItem(node->visible ? "Hide" : "Show"))
            node->visible = !node->visible;
        ImGui::Separator();
        bool isRoot = (node == m_scene->root());
        if (ImGui::MenuItem("Delete", nullptr, false, !isRoot))
            m_pendingDelete = node;
        ImGui::EndPopup();
    }

    if (open) {
        for (auto& child : node->children)
            drawNode(child.get());
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void OutlinerPanel::render() {
    ImGui::Begin("Outliner");

    if (SceneNode* root = m_scene->root())
        drawNode(root);

    // Click on empty space to clear the selection.
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        m_scene->deselect();

    ImGui::End();

    // Deferred so we never mutate the tree mid-traversal.
    if (m_pendingDelete) {
        m_scene->removeNode(m_pendingDelete);
        m_pendingDelete = nullptr;
    }
}
