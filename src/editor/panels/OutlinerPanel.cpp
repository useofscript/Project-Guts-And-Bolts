#include "OutlinerPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

OutlinerPanel::OutlinerPanel(Scene* scene, NodeFn openScript, NodeFn addScriptTo)
    : m_scene(scene), m_openScript(std::move(openScript)), m_addScriptTo(std::move(addScriptTo)) {}

bool OutlinerPanel::isOpen(const SceneNode* node) const {
    auto it = m_open.find(node->id);
    if (it != m_open.end()) return it->second;
    // Keep the character folded by default — it has lots of parts.
    return !m_scene->isCharacterPart(node);
}

void OutlinerPanel::expand(const std::vector<SceneNode*>& nodes) {
    std::vector<SceneNode*> stack(nodes.begin(), nodes.end());
    while (!stack.empty()) {
        SceneNode* n = stack.back(); stack.pop_back();
        m_open[n->id] = true;
        for (auto& c : n->children) stack.push_back(c.get());
    }
}

void OutlinerPanel::collapse(const std::vector<SceneNode*>& nodes) {
    std::vector<SceneNode*> stack(nodes.begin(), nodes.end());
    while (!stack.empty()) {
        SceneNode* n = stack.back(); stack.pop_back();
        m_open[n->id] = false;
        for (auto& c : n->children) stack.push_back(c.get());
    }
}

void OutlinerPanel::collapseAll() {
    if (SceneNode* root = m_scene->root()) {
        collapse({root});
        m_open[root->id] = true;   // keep the Workspace itself open
    }
}

void OutlinerPanel::reveal(SceneNode* node) {
    if (!node) return;
    for (SceneNode* p = node->parent; p; p = p->parent) m_open[p->id] = true;
    m_scrollTo = node->id;
}

void OutlinerPanel::beginRename(SceneNode* node) {
    if (!node || m_scene->isProtected(node) || m_scene->isCharacterPart(node)) return;
    reveal(node);
    m_renaming = node->id;
    m_renameText = node->name;
    m_renameFocus = true;
}

void OutlinerPanel::drawNode(SceneNode* node) {
    if (node->internal) return;   // hidden helper geometry (e.g. the face)

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth;

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

    bool renaming = m_renaming == node->id;
    if (hasVisibleKids) ImGui::SetNextItemOpen(isOpen(node), ImGuiCond_Always);
    bool open = renaming ? ImGui::TreeNodeEx("##node", flags, "%s", tag)
                         : ImGui::TreeNodeEx("##node", flags, "%s%s", tag, node->name.c_str());
    ImGui::PopStyleColor();
    if (hasVisibleKids) m_open[node->id] = open;
    if (m_scrollTo == node->id) { ImGui::SetScrollHereY(0.4f); m_scrollTo = 0; }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl)       m_scene->toggleSelection(node);
        else if (io.KeyShift) m_scene->addToSelection(node);
        else                  m_scene->select(node);
        m_lastSelected = m_scene->selected();
    }
    if (node->isScript() && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        m_openScript(node);

    // Drag an object (or everything selected) onto another one to move it inside.
    bool locked = m_scene->isProtected(node) || m_scene->isCharacterPart(node);
    if (!locked && !renaming && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("GB_NODE", &node, sizeof(SceneNode*));
        size_t n = node->selected ? m_scene->selectionRoots().size() : 1;
        if (n > 1) ImGui::Text("Move %zu objects", n);
        else       ImGui::Text("Move %s", node->name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GB_NODE")) {
            SceneNode* dragged = *static_cast<SceneNode* const*>(p->Data);
            m_dragNodes.clear();
            if (dragged->selected) m_dragNodes = m_scene->selectionRoots();
            else                   m_dragNodes.push_back(dragged);
            m_dropTarget = node;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (!node->selected) m_scene->select(node);
        if (node->isScript() && ImGui::MenuItem("Edit Script")) m_openScript(node);
        if (node->isPart() && ImGui::MenuItem("Add Script inside")) m_addScriptTo(node);
        if (ImGui::MenuItem("Rename", "F2", false, !locked)) beginRename(node);
        if (ImGui::MenuItem(node->visible ? "Hide" : "Show", "H"))
            node->visible = !node->visible;
        if (node->parent && node->parent != m_scene->root() && !locked &&
            ImGui::MenuItem("Move to Workspace")) {
            m_dragNodes = {node};
            m_dropTarget = m_scene->root();
        }
        if (contextMenuExtras) contextMenuExtras();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del", false, !m_scene->isProtected(node)))
            m_pendingDelete = node;
        ImGui::EndPopup();
    }

    if (renaming) {
        ImGui::SameLine();
        if (m_renameFocus) { ImGui::SetKeyboardFocusHere(); m_renameFocus = false; }
        ImGui::SetNextItemWidth(-1);
        bool done = ImGui::InputText("##rename", &m_renameText,
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_renaming = 0;
        else if (done || ImGui::IsItemDeactivated()) {
            if (!m_renameText.empty()) node->name = m_renameText;
            m_renaming = 0;
        }
    }

    if (open) {
        for (auto& child : node->children)
            drawNode(child.get());
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void OutlinerPanel::render() {
    // Something picked in the Viewport: open the folders so it's visible here.
    SceneNode* sel = m_scene->selected();
    if (sel != m_lastSelected) {
        if (sel && !m_scene->isCharacterPart(sel)) reveal(sel);
        m_lastSelected = sel;
    }

    ImGui::Begin("Explorer");

    if (SceneNode* root = m_scene->root())
        drawNode(root);

    // Click on empty space to clear the selection.
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        m_scene->deselect();

    ImGui::Spacing();
    ImGui::TextDisabled("Tip: Ctrl+click picks several things. F1 lists every shortcut.");

    ImGui::End();

    // Deferred so we never change the tree while drawing it.
    if (m_pendingDelete) {
        if (m_pendingDelete->selected) {
            for (SceneNode* n : m_scene->selectionRoots())
                if (!m_scene->isProtected(n)) m_scene->removeNode(n);
        } else {
            m_scene->removeNode(m_pendingDelete);
        }
        m_pendingDelete = nullptr;
    }
    if (!m_dragNodes.empty() && m_dropTarget) {
        if (!m_scene->isCharacterPart(m_dropTarget) || m_dropTarget == m_scene->root())
            for (SceneNode* n : m_dragNodes)
                if (!m_scene->isProtected(n) && !m_scene->isCharacterPart(n)) m_scene->reparent(n, m_dropTarget);
        m_open[m_dropTarget->id] = true;
        m_dragNodes.clear();
        m_dropTarget = nullptr;
    }
}
