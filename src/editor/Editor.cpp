#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "panels/OutlinerPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/EnvironmentPanel.h"
#include "../scene/Scene.h"
#include "../renderer/Primitives.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <string>
#include <vector>

Editor::Editor(GLFWwindow* window, Scene* scene)
    : m_window(window), m_scene(scene) {
    m_viewport    = std::make_unique<ViewportPanel>(window, scene, &m_state);
    m_outliner    = std::make_unique<OutlinerPanel>(scene);
    m_properties  = std::make_unique<PropertiesPanel>(scene);
    m_environment = std::make_unique<EnvironmentPanel>(scene);

    // Default scene objects
    auto cube = scene->addNode("Cube", PrimitiveType::Cube, Primitives::createCube());
    cube->transform.position = {0, 0.5f, 0};
    scene->addNode("Ground", PrimitiveType::Plane, Primitives::createPlane())->transform.scale = {6,1,6};
}

// Out-of-line so the panel types are complete here (they are only forward
// declared in Editor.h, which is what Application.cpp sees).
Editor::~Editor() = default;

void Editor::render(float dt) {
    ImGuizmo::BeginFrame();
    handleShortcuts();
    buildDockspace();
    m_viewport->render(dt);
    m_outliner->render();
    m_properties->render();
    m_environment->render();
}

SceneNode* Editor::addPrimitive(const char* label, PrimitiveType type,
                                std::shared_ptr<Mesh> mesh) {
    ++m_objCounter;
    std::string name = std::string(label) + " " + std::to_string(m_objCounter);
    auto* node = m_scene->addNode(name, type, std::move(mesh));
    node->transform.position = {0, 0.5f, 0};
    m_scene->select(node);
    return node;
}

void Editor::duplicateSelected() {
    SceneNode* sel = m_scene->selected();
    if (!sel || sel == m_scene->root()) return;

    // Share the mesh GPU buffers — they are immutable once uploaded.
    auto* dup = m_scene->addNode(sel->name + " Copy", sel->primitiveType, sel->mesh);
    dup->transform = sel->transform;
    dup->transform.position.x += 1.0f;   // nudge so the copy is visible
    dup->color   = sel->color;
    dup->visible = sel->visible;
    m_scene->select(dup);
}

void Editor::deleteSelected() {
    if (SceneNode* sel = m_scene->selected())
        m_scene->removeNode(sel);
}

void Editor::newScene() {
    SceneNode* root = m_scene->root();
    std::vector<SceneNode*> kids;
    for (auto& c : root->children) kids.push_back(c.get());
    for (auto* k : kids) m_scene->removeNode(k);
    m_objCounter = 0;
}

void Editor::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;   // don't steal keys while editing a field

    if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_state.tool = GizmoTool::Select;
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_state.tool = GizmoTool::Translate;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_state.tool = GizmoTool::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_state.tool = GizmoTool::Scale;

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_scene->deselect();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected();
}

void Editor::buildDockspace() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking        | ImGuiWindowFlags_NoTitleBar    |
        ImGuiWindowFlags_NoCollapse       | ImGuiWindowFlags_NoResize      |
        ImGuiWindowFlags_NoMove           | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus       | ImGuiWindowFlags_MenuBar;

    // Push WindowPadding first so it survives the early pop below and keeps the
    // host body edge-to-edge (the toolbar/status strips add their own padding).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##DockHost", nullptr, hostFlags);
    ImGui::PopStyleVar(2);  // rounding + border; WindowPadding(0,0) stays active

    renderMenuBar();
    renderToolbar();

    // Reserve a row at the bottom of the host window for the status bar.
    float statusH = ImGui::GetFrameHeightWithSpacing();
    ImGuiID dsId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dsId, ImVec2(0, -statusH), ImGuiDockNodeFlags_None);

    if (m_firstLayout) {
        m_firstLayout = false;
        ImGui::DockBuilderRemoveNode(dsId);
        ImGui::DockBuilderAddNode(dsId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dsId, vp->WorkSize);

        ImGuiID left, center, right;
        ImGui::DockBuilderSplitNode(dsId,   ImGuiDir_Left,  0.20f, &left,   &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, &right,  &center);

        ImGui::DockBuilderDockWindow("Outliner",    left);
        ImGui::DockBuilderDockWindow("Viewport",    center);
        ImGui::DockBuilderDockWindow("Properties",  right);
        ImGui::DockBuilderDockWindow("Environment", right);
        ImGui::DockBuilderFinish(dsId);
    }

    renderStatusBar();

    ImGui::PopStyleVar();   // WindowPadding
    ImGui::End();
}

void Editor::renderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene"))  newScene();
        if (ImGui::MenuItem("Open...", nullptr, false, false))  { /* TODO: serialization */ }
        if (ImGui::MenuItem("Save",    nullptr, false, false))  { /* TODO: serialization */ }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Cube"))     addPrimitive("Cube",     PrimitiveType::Cube,     Primitives::createCube());
        if (ImGui::MenuItem("Sphere"))   addPrimitive("Sphere",   PrimitiveType::Sphere,   Primitives::createSphere());
        if (ImGui::MenuItem("Plane"))    addPrimitive("Plane",    PrimitiveType::Plane,    Primitives::createPlane());
        if (ImGui::MenuItem("Cylinder")) addPrimitive("Cylinder", PrimitiveType::Cylinder, Primitives::createCylinder());
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        bool hasSel = m_scene->selected() && m_scene->selected() != m_scene->root();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSel)) duplicateSelected();
        if (ImGui::MenuItem("Delete",    "Del",    false, hasSel)) deleteSelected();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset Camera")) m_viewport->resetCamera();
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void Editor::renderToolbar() {
    const ImVec4 kBarBg = {0.086f, 0.094f, 0.114f, 1.0f};

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBarBg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(10, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 7));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(6, 7));

    float barH = ImGui::GetFrameHeight() + 14.0f;
    ImGui::BeginChild("##toolbar", ImVec2(0, barH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    auto toolBtn = [&](const char* label, GizmoTool t, const char* tip) {
        bool active = (m_state.tool == t);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.26f, 0.59f, 0.98f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.64f, 1.00f, 1.0f));
        }
        if (ImGui::Button(label)) m_state.tool = t;
        if (active) ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };

    auto sep = [] {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    };

    toolBtn("Select", GizmoTool::Select,    "Select / pick objects  (Q)");
    toolBtn("Move",   GizmoTool::Translate, "Move tool  (W)");
    toolBtn("Rotate", GizmoTool::Rotate,    "Rotate tool  (E)");
    toolBtn("Scale",  GizmoTool::Scale,     "Scale tool  (R)");
    sep();

    if (ImGui::Button(m_state.gizmoLocal ? "Local" : "World"))
        m_state.gizmoLocal = !m_state.gizmoLocal;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gizmo orientation: local vs. world");
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_state.snapEnabled);
    sep();

    if (ImGui::Button("+ Cube"))     addPrimitive("Cube",     PrimitiveType::Cube,     Primitives::createCube());
    ImGui::SameLine();
    if (ImGui::Button("+ Sphere"))   addPrimitive("Sphere",   PrimitiveType::Sphere,   Primitives::createSphere());
    ImGui::SameLine();
    if (ImGui::Button("+ Plane"))    addPrimitive("Plane",    PrimitiveType::Plane,    Primitives::createPlane());
    ImGui::SameLine();
    if (ImGui::Button("+ Cylinder")) addPrimitive("Cylinder", PrimitiveType::Cylinder, Primitives::createCylinder());
    sep();

    bool hasSel = m_scene->selected() && m_scene->selected() != m_scene->root();
    ImGui::BeginDisabled(!hasSel);
    if (ImGui::Button("Duplicate")) duplicateSelected();
    ImGui::SameLine();
    if (ImGui::Button("Delete"))    deleteSelected();
    ImGui::EndDisabled();

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void Editor::renderStatusBar() {
    const ImVec4 kBarBg  = {0.086f, 0.094f, 0.114f, 1.0f};
    const ImVec4 kAccent = {0.40f, 0.66f, 1.00f, 1.0f};

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBarBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 5));
    ImGui::BeginChild("##statusbar", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const char* toolName =
        m_state.tool == GizmoTool::Select    ? "Select" :
        m_state.tool == GizmoTool::Translate ? "Move"   :
        m_state.tool == GizmoTool::Rotate    ? "Rotate" : "Scale";

    ImGui::TextDisabled("Tool:");
    ImGui::SameLine();
    ImGui::TextColored(kAccent, "%s", toolName);
    ImGui::SameLine(); ImGui::TextDisabled("   "); ImGui::SameLine();

    if (SceneNode* sel = m_scene->selected()) {
        ImGui::TextDisabled("Selected:");
        ImGui::SameLine();
        ImGui::TextUnformatted(sel->name.c_str());
    } else {
        ImGui::TextDisabled("Nothing selected");
    }

    ImGui::SameLine();
    const char* hint = "[Q] Select   [W] Move   [E] Rotate   [R] Scale     "
                       "MMB orbit   Shift+MMB pan   Wheel zoom     "
                       "Click select   F focus   Del delete   Ctrl+D duplicate";
    float avail = ImGui::GetContentRegionAvail().x;
    float tw    = ImGui::CalcTextSize(hint).x;
    if (tw < avail) ImGui::SameLine(ImGui::GetCursorPosX() + (avail - tw));
    ImGui::TextDisabled("%s", hint);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}
