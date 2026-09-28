#include "OutlinerPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"

#include <imgui.h>

OutlinerPanel::OutlinerPanel(Scene* scene, NodeFn openScript, NodeFn addScriptTo)
    : m_scene(scene), m_openScript(std::move(openScript)), m_addScriptTo(std::move(addScriptTo)) {}

void OutlinerPanel::drawNode(SceneNode* node) {
    if (node->internal) return;   // hidden helper geometry (e.g. the face)

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    // Keep the character folded by default — it has lots of parts.
    if (!m_scene->isCharacterPart(node)) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    bool hasVisibleKids = false;
    for (auto& c : node->children) if (!c->internal) { hasVisibleKids = true; break; }
    if (!hasVisibleKids) flags |= ImGuiTreeNodeFlags_Leaf;
    if (node->selected)  flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(node);

    // Colour-code by kind: scripts blue, models yellow, hidden objects grey.
    ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const char* tag = "";
    if (node->isScript())                { col = {0.55f, 0.75f, 1.00f, 1}; tag = "[S] "; }
    else if (node->kind == NodeKind::Model) { col = {0.95f, 0.82f, 0.45f, 1}; tag = "[M] "; }
    else if (node->isLight())            { col = {1.00f, 0.95f, 0.60f, 1}; tag = "[L] "; }
    else if (node->kind == NodeKind::ForceField) { col = {0.60f, 0.90f, 1.00f, 1}; tag = "[F] "; }
    else if (node->isSound())            { col = {0.80f, 0.65f, 1.00f, 1}; tag = "[A] "; }
    else if (node->isAttachment())       { col = {0.40f, 1.00f, 0.50f, 1}; tag = "[+] "; }
    else if (node->isConstraint())       { col = {0.95f, 0.70f, 0.45f, 1}; tag = "[C] "; }
    if (!node->visible || (node->isScript() && !node->enabled)) col.w = 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Text, col);

    bool open = ImGui::TreeNodeEx("##node", flags, "%s%s", tag, node->name.c_str());
    ImGui::PopStyleColor();

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        m_scene->select(node);
    if (node->isScript() && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        m_openScript(node);

    // Drag an object onto another one to move it inside.
    bool locked = m_scene->isProtected(node) || m_scene->isCharacterPart(node);
    if (!locked && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("GB_NODE", &node, sizeof(SceneNode*));
        ImGui::Text("Move %s", node->name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GB_NODE")) {
            m_dragNode   = *static_cast<SceneNode* const*>(p->Data);
            m_dropTarget = node;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        m_scene->select(node);
        if (node->isScript() && ImGui::MenuItem("Edit Script")) m_openScript(node);
        if (node->isPart() && ImGui::MenuItem("Add Script inside")) m_addScriptTo(node);
        if (ImGui::MenuItem(node->visible ? "Hide" : "Show"))
            node->visible = !node->visible;
        if (node->parent && node->parent != m_scene->root() && !locked &&
            ImGui::MenuItem("Move to Workspace")) {
            m_dragNode = node;
            m_dropTarget = m_scene->root();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del", false, !m_scene->isProtected(node)))
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
    ImGui::Begin("Explorer");

    if (SceneNode* root = m_scene->root())
        drawNode(root);

    // Click on empty space to clear the selection.
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        m_scene->deselect();

    ImGui::Spacing();
    ImGui::TextDisabled("Tip: drag objects onto each other to group them.");

    ImGui::End();

    // Deferred so we never change the tree while drawing it.
    if (m_pendingDelete) {
        m_scene->removeNode(m_pendingDelete);
        m_pendingDelete = nullptr;
    }
    if (m_dragNode && m_dropTarget) {
        if (!m_scene->isCharacterPart(m_dropTarget) || m_dropTarget == m_scene->root())
            m_scene->reparent(m_dragNode, m_dropTarget);
        m_dragNode = m_dropTarget = nullptr;
    }
}
