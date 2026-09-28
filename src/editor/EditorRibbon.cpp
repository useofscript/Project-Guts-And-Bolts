// Studio's ribbon (HOME / MODEL / TEST / VIEW), laid out like Roblox Studio's.
#include "Editor.h"
#include "Icons.h"
#include "Theme.h"
#include "panels/ViewportPanel.h"
#include "../scene/Scene.h"
#include "../scene/Physics.h"
#include "../core/Log.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>

namespace {

const ImU32 kRibbonBg  = IM_COL32(46, 46, 46, 255);
const ImU32 kTabsBg    = IM_COL32(37, 37, 37, 255);
const ImU32 kHover     = IM_COL32(70, 70, 70, 255);
const ImU32 kActive    = IM_COL32(11, 90, 175, 255);
const ImU32 kGroupText = IM_COL32(150, 150, 150, 255);

// A big ribbon button: icon on top, label underneath.
bool bigButton(const char* label, Icons::Id icon, bool active = false, bool enabled = true, const char* tip = nullptr) {
    ImGui::PushID(label);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float w = std::max(52.0f, textSize.x + 14.0f), h = 62.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    bool clicked = ImGui::InvisibleButton("##b", ImVec2(w, h));
    ImGui::EndDisabled();
    bool hover = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kActive, 3.0f);
    else if (hover && enabled) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kHover, 3.0f);
    Icons::draw(dl, ImVec2(p.x + w * 0.5f, p.y + 20), 28.0f, icon,
                enabled ? IM_COL32(225, 225, 225, 255) : IM_COL32(110, 110, 110, 255));
    dl->AddText(ImVec2(p.x + (w - textSize.x) * 0.5f, p.y + h - textSize.y - 5),
                enabled ? IM_COL32(225, 225, 225, 255) : IM_COL32(110, 110, 110, 255), label);
    if (hover && tip) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
    ImGui::SameLine(0, 2);
    return clicked && enabled;
}

// A small ribbon button (icon + text), stacked three high.
bool smallButton(const char* label, Icons::Id icon, bool active = false, bool enabled = true, const char* tip = nullptr) {
    ImGui::PushID(label);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float w = textSize.x + 30.0f, h = 19.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    bool clicked = ImGui::InvisibleButton("##s", ImVec2(w, h));
    ImGui::EndDisabled();
    bool hover = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kActive, 2.0f);
    else if (hover && enabled) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kHover, 2.0f);
    ImU32 col = enabled ? IM_COL32(225, 225, 225, 255) : IM_COL32(110, 110, 110, 255);
    Icons::draw(dl, ImVec2(p.x + 11, p.y + h * 0.5f), 15.0f, icon, col);
    dl->AddText(ImVec2(p.x + 24, p.y + (h - textSize.y) * 0.5f), col, label);
    if (hover && tip) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
    return clicked && enabled;
}

// Groups: a column of buttons with a caption under it and a divider after.
struct Group {
    ImVec2 start;
    float  top;
    const char* name;
    Group(const char* n) : name(n) {
        start = ImGui::GetCursorScreenPos();
        top = start.y;
        ImGui::BeginGroup();
    }
    ~Group() {
        ImGui::EndGroup();
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 ts = ImGui::CalcTextSize(name);
        float capY = top + 66;
        float width = std::max(mx.x - mn.x, ts.x);
        dl->AddText(ImVec2(mn.x + (width - ts.x) * 0.5f, capY), kGroupText, name);
        ImGui::SameLine(0, 0);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float x = std::max(p.x, mn.x + width) + 8;
        dl->AddLine(ImVec2(x, top + 4), ImVec2(x, top + 80), IM_COL32(70, 70, 70, 255));
        ImGui::SetCursorScreenPos(ImVec2(x + 9, top));
    }
};

// Three small buttons stacked in a column.
struct Stack {
    Stack() { ImGui::BeginGroup(); ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2)); }
    ~Stack() { ImGui::PopStyleVar(); ImGui::EndGroup(); ImGui::SameLine(0, 4); }
};

} // namespace

// Line the selection up along one axis (0 = X, 1 = Y, 2 = Z): where 0 = low
// side, 1 = centre, 2 = high side of the whole selection's box.
void Editor::alignSelected(int axis, int where) {
    auto items = m_scene->selectionRoots();
    if (items.size() < 2) { Log::warn("Align: select two or more things first."); return; }
    auto bounds = [&](SceneNode* n, AABB& out) {
        bool any = false;
        std::vector<SceneNode*> stack{n};
        while (!stack.empty()) {
            SceneNode* k = stack.back(); stack.pop_back();
            if (k->isPart()) {
                AABB b = Physics::worldBounds(k);
                if (!any) out = b; else { out.min = glm::min(out.min, b.min); out.max = glm::max(out.max, b.max); }
                any = true;
            }
            for (auto& c : k->children) stack.push_back(c.get());
        }
        return any;
    };
    AABB all{};
    bool first = true;
    for (SceneNode* n : items) {
        AABB b;
        if (!bounds(n, b)) continue;
        if (first) { all = b; first = false; }
        else { all.min = glm::min(all.min, b.min); all.max = glm::max(all.max, b.max); }
    }
    if (first) return;
    float target = where == 0 ? all.min[axis] : where == 2 ? all.max[axis] : (all.min[axis] + all.max[axis]) * 0.5f;
    for (SceneNode* n : items) {
        AABB b;
        if (!canEdit(n) || !bounds(n, b)) continue;
        float cur = where == 0 ? b.min[axis] : where == 2 ? b.max[axis] : (b.min[axis] + b.max[axis]) * 0.5f;
        glm::vec3 d(0.0f);
        d[axis] = target - cur;
        if (n->parent) d = glm::vec3(glm::inverse(n->parent->worldMatrix()) * glm::vec4(d, 0.0f));
        n->transform.position += d;
    }
}

void Editor::renderToolbar() {
    ImDrawList* bg = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;

    // --- Tab row ---
    const float tabsH = 26.0f, ribbonH = 88.0f;
    bg->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + tabsH), kTabsBg);
    bg->AddRectFilled(ImVec2(origin.x, origin.y + tabsH), ImVec2(origin.x + width, origin.y + tabsH + ribbonH), kRibbonBg);
    bg->AddLine(ImVec2(origin.x, origin.y + tabsH + ribbonH - 1), ImVec2(origin.x + width, origin.y + tabsH + ribbonH - 1),
                IM_COL32(26, 26, 26, 255));
    const char* tabs[] = {"HOME", "MODEL", "TEST", "VIEW"};
    float x = origin.x + 10;
    for (int i = 0; i < 4; ++i) {
        ImVec2 ts = ImGui::CalcTextSize(tabs[i]);
        ImVec2 a(x, origin.y), b(x + ts.x + 24, origin.y + tabsH);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", ImVec2(b.x - a.x, tabsH))) m_ribbonTab = i;
        ImGui::PopID();
        bool on = m_ribbonTab == i;
        if (on) {
            bg->AddRectFilled(a, b, kRibbonBg);
            bg->AddLine(ImVec2(a.x, a.y + 1), ImVec2(b.x, a.y + 1), EditorTheme::kRobloxBlue, 2.0f);
        } else if (ImGui::IsItemHovered()) {
            bg->AddRectFilled(a, b, IM_COL32(55, 55, 55, 255));
        }
        bg->AddText(ImVec2(a.x + 12, a.y + (tabsH - ts.y) * 0.5f),
                    on ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 255), tabs[i]);
        x = b.x + 2;
    }

    // Quick buttons on the right of the tab row (like Roblox's undo / redo).
    {
        float qx = origin.x + width - 70;
        ImGui::SetCursorScreenPos(ImVec2(qx, origin.y + 3));
        ImGui::PushID("quick");
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(m_undo.empty() || m_playing);
        if (ImGui::InvisibleButton("##undo", ImVec2(28, 20))) undo();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Undo (Ctrl+Z)");
        ImGui::EndDisabled();
        Icons::draw(bg, ImVec2(p.x + 14, p.y + 10), 16, Icons::Id::Undo);
        ImGui::SameLine(0, 4);
        p = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(m_redo.empty() || m_playing);
        if (ImGui::InvisibleButton("##redo", ImVec2(28, 20))) redo();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Redo (Ctrl+Y)");
        ImGui::EndDisabled();
        Icons::draw(bg, ImVec2(p.x + 14, p.y + 10), 16, Icons::Id::Redo);
        ImGui::PopID();
    }

    // --- Ribbon contents ---
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10, origin.y + tabsH + 3));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
    SceneNode* sel = m_scene->selected();
    bool editable = canEdit(sel) && !m_playing;
    auto tool = [&](const char* label, Icons::Id icon, GizmoTool t, const char* tip) {
        if (bigButton(label, icon, m_state.tool == t, !m_playing, tip)) m_state.tool = t;
    };
    auto tools = [&] {
        Group g("Tools");
        tool("Select", Icons::Id::Select, GizmoTool::Select, "Select (Ctrl+1)");
        tool("Move", Icons::Id::Move, GizmoTool::Translate, "Move (Ctrl+2)");
        tool("Scale", Icons::Id::Scale, GizmoTool::Scale, "Scale (Ctrl+3)");
        tool("Rotate", Icons::Id::Rotate, GizmoTool::Rotate, "Rotate (Ctrl+4)");
    };
    auto test = [&] {
        Group g("Test");
        if (bigButton("Play", Icons::Id::Play, m_playing && m_playMode == 0, !m_playing, "Play (F5)")) startPlay(0);
        if (bigButton("Play Here", Icons::Id::PlayHere, m_playing && m_playMode == 1, !m_playing,
                      "Play, starting where the camera is looking")) startPlay(1);
        if (bigButton("Run", Icons::Id::Run, m_playing && m_playMode == 2, !m_playing,
                      "Run the world and scripts without a player (F8)")) startPlay(2);
        if (bigButton("Stop", Icons::Id::Stop, false, m_playing, "Stop (Shift+F5)")) togglePlay();
    };

    switch (m_ribbonTab) {
    case 0: {   // HOME
        {
            Group g("Clipboard");
            if (bigButton("Paste", Icons::Id::Paste, false, !m_clipboard.empty() && !m_playing, "Paste (Ctrl+V)")) paste();
            Stack st;
            if (smallButton("Copy", Icons::Id::Copy, false, editable, "Copy (Ctrl+C)")) copySelected();
            if (smallButton("Cut", Icons::Id::Cut, false, editable, "Cut (Ctrl+X)")) cutSelected();
            if (smallButton("Duplicate", Icons::Id::Duplicate, false, editable, "Duplicate (Ctrl+D)")) duplicateSelected();
        }
        tools();
        {
            Group g("Insert");
            if (bigButton("Part", Icons::Id::Part, false, !m_playing, "Insert a block (Ctrl+Shift+P for more shapes in the MODEL tab)"))
                spawnPrimitive(PrimitiveType::Cube);
            if (bigButton("Object", Icons::Id::Insert, false, !m_playing, "Insert Object... (Ctrl+I)")) m_openInsert = true;
            if (bigButton("Import", Icons::Id::Import, false, !m_playing, "Open or insert a Roblox file (.rbxl / .rbxm)")) {
                m_pending = Pending::Open; m_openOpen = true;
            }
        }
        {
            Group g("Edit");
            {
                Stack st;
                if (smallButton("Group", Icons::Id::Group, false, editable, "Group as Model (Ctrl+G)")) groupSelected();
                if (smallButton("Ungroup", Icons::Id::Ungroup, false, editable && sel->kind == NodeKind::Model, "Ungroup (Ctrl+U)"))
                    ungroupSelected();
                if (smallButton("Lock", Icons::Id::Lock, sel && sel->locked, editable, "Lock: can't be clicked in the Viewport (Alt+L)"))
                    toggleLocked();
            }
            {
                Stack st;
                if (smallButton("Anchor", Icons::Id::Anchor, sel && sel->anchored && sel->isPart(), editable, "Anchor: stays put (Alt+A)"))
                    toggleAnchored();
                if (smallButton("Snap", Icons::Id::Snap, m_state.snapEnabled, true, "Snap to grid")) m_state.snapEnabled = !m_state.snapEnabled;
                if (smallButton(m_state.gizmoLocal ? "Local" : "World", Icons::Id::Transform, false, true, "Local / world axes (Ctrl+L)"))
                    m_state.gizmoLocal = !m_state.gizmoLocal;
            }
        }
        test();
        {
            Group g("Settings");
            if (bigButton("Game Settings", Icons::Id::Settings, false, !m_playing, "Title, description and more")) m_openInfo = true;
            if (bigButton("Team Create", Icons::Id::Team, false, true, "Edit together with friends")) m_openTeam = true;
        }
        break;
    }
    case 1: {   // MODEL
        tools();
        {
            Group g("Snap to Grid");
            ImGui::BeginGroup();
            ImGui::Checkbox("Move", &m_state.snapEnabled);
            ImGui::SetNextItemWidth(70);
            ImGui::DragFloat("studs##mv", &m_state.snapTranslate, 0.05f, 0.05f, 50.0f, "%.2f");
            ImGui::SetNextItemWidth(70);
            ImGui::DragFloat("deg##rot", &m_state.snapRotate, 1.0f, 1.0f, 180.0f, "%.0f");
            ImGui::EndGroup();
            ImGui::SameLine(0, 4);
        }
        {
            Group g("Parts");
            if (bigButton("Block", Icons::Id::Part, false, !m_playing)) spawnPrimitive(PrimitiveType::Cube);
            if (bigButton("Sphere", Icons::Id::Sphere, false, !m_playing)) spawnPrimitive(PrimitiveType::Sphere);
            if (bigButton("Cylinder", Icons::Id::Cylinder, false, !m_playing)) spawnPrimitive(PrimitiveType::Cylinder);
            if (bigButton("Plane", Icons::Id::Plane, false, !m_playing)) spawnPrimitive(PrimitiveType::Plane);
        }
        {
            Group g("Constraints");
            const char* names[] = {"Rope", "Rod", "Spring", "Weld", "Hinge", "Motor"};
            for (int col = 0; col < 2; ++col) {
                Stack st;
                for (int r = 0; r < 3; ++r) {
                    int t = col * 3 + r;
                    if (smallButton(names[t], Icons::Id::Constraint, m_state.connectTool == t, !m_playing,
                                    "Click this, then click two parts to join them")) {
                        m_state.connectTool = t;
                        m_state.connectFirst = 0;
                    }
                }
            }
        }
        {
            Group g("Advanced");
            {
                Stack st;
                if (smallButton("Script", Icons::Id::Script, false, !m_playing, "Add a Script (Ctrl+I for more)")) addScript(sel);
                if (smallButton("Model", Icons::Id::Model, false, !m_playing)) addModel();
                if (smallButton("Light", Icons::Id::Light, false, !m_playing)) addLight(LightType::Point);
            }
            {
                Stack st;
                if (smallButton("Sound", Icons::Id::Sound, false, !m_playing)) addSound();
                if (smallButton("Pivot to middle", Icons::Id::Transform, false, editable && sel->kind == NodeKind::Model,
                                "Move this Model's origin to the middle of its parts"))
                    centerModelPivot();
                if (smallButton("Export", Icons::Id::Export, false, !m_playing, "Export to Roblox (.rbxlx)")) exportRoblox(false);
            }
        }
        {
            Group g("Align");
            ImGui::BeginGroup();
            static int axis = 0;
            ImGui::RadioButton("X", &axis, 0); ImGui::SameLine(); ImGui::RadioButton("Y", &axis, 1); ImGui::SameLine();
            ImGui::RadioButton("Z", &axis, 2);
            bool can = m_scene->selectionRoots().size() >= 2 && !m_playing;
            ImGui::BeginDisabled(!can);
            if (ImGui::Button("Min")) alignSelected(axis, 0);
            ImGui::SameLine();
            if (ImGui::Button("Center")) alignSelected(axis, 1);
            ImGui::SameLine();
            if (ImGui::Button("Max")) alignSelected(axis, 2);
            ImGui::EndDisabled();
            ImGui::EndGroup();
            ImGui::SameLine(0, 4);
        }
        break;
    }
    case 2: {   // TEST
        test();
        {
            Group g("Settings");
            if (bigButton("Player", Icons::Id::Player, false, true, "Walk speed, jump, death and gore")) m_showPanel[kPanelPlayer] = true;
            if (bigButton("Lighting", Icons::Id::Lighting, false, true)) m_showPanel[kPanelLighting] = true;
        }
        break;
    }
    case 3: {   // VIEW
        {
            Group g("Show");
            struct P { const char* label; Icons::Id icon; int panel; };
            const P panels[] = {{"Explorer", Icons::Id::Explorer, kPanelExplorer}, {"Properties", Icons::Id::Properties, kPanelProperties},
                                {"Toolbox", Icons::Id::Toolbox, kPanelToolbox}, {"Output", Icons::Id::Output, kPanelOutput},
                                {"Command Bar", Icons::Id::CommandBar, kPanelCommandBar}, {"Script Editor", Icons::Id::Script, kPanelScript},
                                {"Lighting", Icons::Id::Lighting, kPanelLighting}, {"Player", Icons::Id::Player, kPanelPlayer},
                                {"Team Chat", Icons::Id::Team, kPanelTeam}};
            for (const P& p : panels)
                if (bigButton(p.label, p.icon, m_showPanel[p.panel], true, "Show / hide")) m_showPanel[p.panel] = !m_showPanel[p.panel];
        }
        {
            Group g("Other");
            if (bigButton("Shortcuts", Icons::Id::Keyboard, m_showShortcuts, true, "Every keyboard shortcut (F1)"))
                m_showShortcuts = !m_showShortcuts;
            if (bigButton("Settings", Icons::Id::Settings, false, true, "Graphics and frame rate")) m_showSettings = true;
            if (bigButton("Reset Camera", Icons::Id::Workspace, false, true)) m_viewport->resetCamera();
        }
        break;
    }
    }
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + tabsH + ribbonH));
    ImGui::Dummy(ImVec2(0, 0));
}
