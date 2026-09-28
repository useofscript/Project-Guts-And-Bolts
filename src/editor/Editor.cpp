#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "panels/OutlinerPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/EnvironmentPanel.h"
#include "panels/ToolboxPanel.h"
#include "panels/PlayerPanel.h"
#include "panels/OutputPanel.h"
#include "panels/ScriptEditorPanel.h"
#include "../scene/Scene.h"
#include "../scene/Player.h"
#include "../scene/Serializer.h"
#include "../game/GameSession.h"
#include "../game/SettingsWindow.h"
#include "../game/UpdateToast.h"
#include "../core/Settings.h"
#include "../renderer/MeshLibrary.h"
#include "../core/Log.h"
#include "../core/Paths.h"

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <ImGuizmo.h>
#include <string>
#include <vector>

namespace {
const char* kNewScript =
R"(-- A new script! It runs when you press Play (F5).
-- 'script.Parent' is the object this script is inside of.

print("Hello world!")
)";
} // namespace

Editor::Editor(GLFWwindow* window, Scene* scene)
    : m_window(window), m_scene(scene) {
    m_session  = std::make_unique<GameSession>(scene);
    m_viewport = std::make_unique<ViewportPanel>(window, scene, &m_state);
    auto open  = [this](SceneNode* n) { openScript(n); };
    m_outliner = std::make_unique<OutlinerPanel>(scene, open, [this](SceneNode* n) { addScript(n); });
    m_properties   = std::make_unique<PropertiesPanel>(scene, open);
    m_environment  = std::make_unique<EnvironmentPanel>(scene);
    m_player       = std::make_unique<PlayerPanel>(scene);
    m_output       = std::make_unique<OutputPanel>();
    m_scriptEditor = std::make_unique<ScriptEditorPanel>(scene);

    ToolboxPanel::Actions actions;
    actions.spawnPart    = [this](PrimitiveType t) { spawnPrimitive(t); };
    actions.addScript    = [this] { addScript(m_scene->selected()); };
    actions.addModel     = [this] { addModel(); };
    actions.addLight     = [this](LightType t) { addLight(t); };
    actions.addSound     = [this] { addSound(); };
    actions.spawnPremade = [this](Premade p) { spawnPremade(p); };
    m_toolbox = std::make_unique<ToolboxPanel>(actions);

    resetHistory();
    Log::system("Welcome to Guts and Bolts! Press Play (F5) to test your game.");
}

// Out-of-line so the panel types are complete here.
Editor::~Editor() {
    if (m_session) m_session->stop();
}

void Editor::render(float dt) {
    ImGuizmo::BeginFrame();
    // Docked tabs appear over a few frames; make sure the 3D view ends up on top.
    if (++m_frame <= 3) m_viewport->focus();
    handleShortcuts();
    if (m_playing) {
        m_session->update(dt, m_viewport->cameraYaw(), true);
        if (Player* p = m_scene->player()) m_viewport->frameOn(p->focusPoint());
    }
    buildDockspace();
    m_viewport->render(dt);
    m_outliner->render();
    m_properties->render();
    m_environment->render();
    m_toolbox->render();
    m_player->render();
    m_output->render();
    m_scriptEditor->render();
    renderDialogs();
    SettingsWindow::draw(&m_showSettings);
    if (UpdateToast::draw("GutsAndBolts")) glfwSetWindowShouldClose(m_window, GLFW_TRUE);

    trackChanges();
    updateTitle();
}

// ---------------------------------------------------------------------------
// Play mode
// ---------------------------------------------------------------------------

void Editor::togglePlay() {
    if (!m_playing) {
        // Remember the world exactly as it is, so Stop can put it back.
        m_playSnapshot = Serializer::saveScene(*m_scene);
        m_playing = true;
        m_viewport->setSession(m_session.get());
        m_viewport->focus();
        Log::system("Game started.");
        m_session->start();
    } else {
        uint64_t sel = m_scene->selected() ? m_scene->selected()->id : 0;
        m_session->stop();
        m_viewport->setSession(nullptr);
        m_playing = false;
        Serializer::loadScene(*m_scene, m_playSnapshot);
        m_scene->select(m_scene->findById(sel));
        m_committed = m_playSnapshot;
        Log::system("Game stopped - everything is back to how it was.");
    }
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

void Editor::resetHistory() {
    m_undo.clear();
    m_redo.clear();
    m_committed = Serializer::saveScene(*m_scene);
    m_dirty = false;
}

void Editor::trackChanges() {
    // Wait until a drag / text edit is finished so it becomes one undo step.
    if (m_playing || ImGui::IsAnyItemActive() || m_viewport->gizmoInUse()) return;
    std::string now = Serializer::saveScene(*m_scene);
    if (now == m_committed) return;
    m_undo.push_back(std::move(m_committed));
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_committed = std::move(now);
    m_dirty = true;
}

void Editor::restore(const std::string& snapshot) {
    uint64_t sel = m_scene->selected() ? m_scene->selected()->id : 0;
    Serializer::loadScene(*m_scene, snapshot);
    m_scene->select(m_scene->findById(sel));
    m_committed = snapshot;
    m_dirty = true;
}

void Editor::undo() {
    if (m_playing || m_undo.empty()) return;
    m_redo.push_back(m_committed);
    std::string s = std::move(m_undo.back());
    m_undo.pop_back();
    restore(s);
}

void Editor::redo() {
    if (m_playing || m_redo.empty()) return;
    m_undo.push_back(m_committed);
    std::string s = std::move(m_redo.back());
    m_redo.pop_back();
    restore(s);
}

// ---------------------------------------------------------------------------
// Creating things
// ---------------------------------------------------------------------------

glm::vec3 Editor::spawnPoint() const {
    glm::vec3 p = m_viewport->cameraPivot();
    return {std::round(p.x), 0.0f, std::round(p.z)};
}

bool Editor::canEdit(const SceneNode* n) const {
    return n && !m_scene->isProtected(n) && !m_scene->isCharacterPart(n);
}

SceneNode* Editor::addPrimitive(const char* label, PrimitiveType type) {
    ++m_objCounter;
    std::string name = std::string(label) + " " + std::to_string(m_objCounter);
    auto* node = m_scene->addNode(name, type, MeshLibrary::get(type));
    node->transform.position = spawnPoint() + glm::vec3(0, type == PrimitiveType::Plane ? 0.01f : 0.5f, 0);
    m_scene->select(node);
    return node;
}

void Editor::spawnPrimitive(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube:     addPrimitive("Cube",     type); break;
        case PrimitiveType::Sphere:   addPrimitive("Sphere",   type); break;
        case PrimitiveType::Plane:    addPrimitive("Plane",    type); break;
        case PrimitiveType::Cylinder: addPrimitive("Cylinder", type); break;
        default: break;
    }
}

void Editor::spawnPremade(Premade kind) {
    if (SceneNode* n = buildPremade(*m_scene, kind, spawnPoint())) m_scene->select(n);
}

void Editor::addScript(SceneNode* parent) {
    // Scripts can't go inside other scripts or the character.
    if (!parent || parent->isScript() || m_scene->isCharacterPart(parent)) parent = m_scene->root();
    auto s = std::make_unique<SceneNode>("Script", NodeKind::Script);
    s->source = kNewScript;
    SceneNode* raw = m_scene->insert(std::move(s), parent);
    m_scene->select(raw);
    openScript(raw);
}

void Editor::addModel() {
    SceneNode* m = m_scene->insert(std::make_unique<SceneNode>("Model", NodeKind::Model));
    m_scene->select(m);
}

void Editor::addLight(LightType type) {
    auto l = std::make_unique<SceneNode>(type == LightType::Spot ? "SpotLight" : "PointLight", NodeKind::Light);
    l->lightType = type;
    l->color     = {1.0f, 0.9f, 0.75f};
    SceneNode* parent = m_scene->selected();
    if (!parent || !parent->isPart() || m_scene->isCharacterPart(parent)) {
        // No part selected: float the light above the ground.
        parent = m_scene->root();
        l->transform.position = spawnPoint() + glm::vec3(0, 4, 0);
    }
    m_scene->select(m_scene->insert(std::move(l), parent));
}

void Editor::addSound() {
    SceneNode* parent = m_scene->selected();
    if (!parent || parent->isScript() || parent->isSound() || m_scene->isCharacterPart(parent))
        parent = m_scene->root();
    auto s = std::make_unique<SceneNode>("Sound", NodeKind::Sound);
    m_scene->select(m_scene->insert(std::move(s), parent));
}

void Editor::openScript(SceneNode* script) {
    if (script && script->isScript()) {
        m_scene->select(script);
        m_scriptEditor->open(script->id);
    }
}

void Editor::duplicateSelected() {
    SceneNode* sel = m_scene->selected();
    if (!canEdit(sel)) return;
    auto copy = Serializer::clone(*sel);
    copy->name += " Copy";
    if (copy->kind == NodeKind::Part) copy->transform.position.x += 1.0f;   // so it's visible
    SceneNode* dup = m_scene->insert(std::move(copy), sel->parent);
    m_scene->select(dup);
}

void Editor::deleteSelected() {
    SceneNode* sel = m_scene->selected();
    if (sel && !m_scene->isProtected(sel)) m_scene->removeNode(sel);
}

void Editor::copySelected() {
    SceneNode* sel = m_scene->selected();
    if (!canEdit(sel)) return;
    m_clipboard = Serializer::nodeToString(*sel);
}

void Editor::paste() {
    if (m_clipboard.empty()) return;
    auto n = Serializer::nodeFromString(m_clipboard, true);
    if (!n) return;
    SceneNode* parent = m_scene->selected();
    if (!parent || parent->kind != NodeKind::Model || m_scene->isCharacterPart(parent))
        parent = m_scene->root();
    if (n->kind == NodeKind::Part) n->transform.position.x += 1.0f;
    m_scene->select(m_scene->insert(std::move(n), parent));
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

void Editor::newScene() {
    if (m_playing) togglePlay();
    m_scene->buildDefault();
    m_objCounter = 0;
    m_path.clear();
    resetHistory();
    Log::system("Started a new game.");
}

void Editor::openFile(const std::string& path) {
    if (m_playing) togglePlay();
    std::string text, err;
    if (!Serializer::readFile(path, text)) {
        Log::error("Couldn't open " + path);
        return;
    }
    if (!Serializer::loadScene(*m_scene, text, &err)) {
        Log::error("Couldn't load " + path + ": " + err);
        m_scene->buildDefault();
    } else {
        m_path = path;
        Log::system("Opened " + path);
    }
    resetHistory();
}

void Editor::saveFile(const std::string& path) {
    if (m_playing) togglePlay();
    if (Serializer::writeFile(path, Serializer::saveScene(*m_scene, true))) {
        m_path  = path;
        m_dirty = false;
        Log::system("Saved to " + path + "  (it now shows up in Guts&BoltsPlayer)");
    } else {
        Log::error("Couldn't save to " + path);
    }
}

void Editor::save() {
    if (m_path.empty()) {
        m_nameInput = m_scene->info().title;
        m_openSaveAs = true;
    } else {
        saveFile(m_path);
    }
}

void Editor::updateTitle() {
    std::string name = m_path.empty() ? "Untitled"
                                      : std::filesystem::path(m_path).stem().string();
    std::string title = name + (m_dirty ? "*" : "") + " - Guts and Bolts";
    if (title != m_shownTitle) {
        glfwSetWindowTitle(m_window, title.c_str());
        m_shownTitle = title;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Editor::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;   // don't steal keys while typing

    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) { togglePlay(); return; }

    if (m_playing) {
        // In playtest mode WASD drives the character, not the editor tools.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) togglePlay();
        return;
    }

    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { io.KeyShift ? redo() : undo(); }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelected();
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) paste();
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (io.KeyShift) { m_nameInput = m_scene->info().title; m_openSaveAs = true; } else save(); }
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) { m_pending = Pending::Open; m_openDiscard = m_dirty; if (!m_dirty) m_openOpen = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_N, false)) { m_pending = Pending::New;  m_openDiscard = m_dirty; if (!m_dirty) { newScene(); m_pending = Pending::None; } }
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_state.tool = GizmoTool::Select;
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_state.tool = GizmoTool::Translate;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_state.tool = GizmoTool::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_state.tool = GizmoTool::Scale;

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_scene->deselect();
}

// ---------------------------------------------------------------------------
// Layout & chrome
// ---------------------------------------------------------------------------

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

        ImGuiID left, center, right, bottom;
        ImGui::DockBuilderSplitNode(dsId,   ImGuiDir_Left,  0.18f, &left,   &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, &right,  &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,  0.24f, &bottom, &center);
        ImGuiID leftTop, leftBottom;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.55f, &leftTop, &leftBottom);

        ImGui::DockBuilderDockWindow("Explorer",      leftTop);
        ImGui::DockBuilderDockWindow("Toolbox",       leftBottom);
        ImGui::DockBuilderDockWindow("Script Editor", center);
        ImGui::DockBuilderDockWindow("Viewport",      center);
        ImGui::DockBuilderDockWindow("Output",        bottom);
        ImGui::DockBuilderDockWindow("Properties",    right);
        ImGui::DockBuilderDockWindow("Lighting",      right);
        ImGui::DockBuilderDockWindow("Player",        right);
        ImGui::DockBuilderFinish(dsId);
        m_viewport->focus();
    }

    renderStatusBar();

    ImGui::PopStyleVar();   // WindowPadding
    ImGui::End();
}

void Editor::renderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Game", "Ctrl+N")) {
            m_pending = Pending::New;
            if (m_dirty) m_openDiscard = true; else { newScene(); m_pending = Pending::None; }
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            m_pending = Pending::Open;
            if (m_dirty) m_openDiscard = true; else m_openOpen = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) save();
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
            m_nameInput = m_scene->info().title;
            m_openSaveAs = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Game Settings...")) m_openInfo = true;
        if (ImGui::MenuItem("Play in Guts&BoltsPlayer")) {
            if (m_path.empty()) {
                Log::warn("Save your game first (File > Save), then try again.");
                m_nameInput = m_scene->info().title;
                m_openSaveAs = true;
            } else {
                saveFile(m_path);
                if (!Paths::launch(Paths::sibling("GutsAndBoltsPlayer"), m_path))
                    Log::error("Couldn't find GutsAndBoltsPlayer next to the editor.");
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        SceneNode* sel = m_scene->selected();
        bool editable = canEdit(sel);
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undo.empty() && !m_playing)) undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redo.empty() && !m_playing)) redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Copy",      "Ctrl+C", false, editable)) copySelected();
        if (ImGui::MenuItem("Paste",     "Ctrl+V", false, !m_clipboard.empty())) paste();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, editable)) duplicateSelected();
        if (ImGui::MenuItem("Delete",    "Del",    false, sel && !m_scene->isProtected(sel))) deleteSelected();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Cube"))     spawnPrimitive(PrimitiveType::Cube);
        if (ImGui::MenuItem("Sphere"))   spawnPrimitive(PrimitiveType::Sphere);
        if (ImGui::MenuItem("Plane"))    spawnPrimitive(PrimitiveType::Plane);
        if (ImGui::MenuItem("Cylinder")) spawnPrimitive(PrimitiveType::Cylinder);
        ImGui::Separator();
        if (ImGui::MenuItem("Script")) addScript(m_scene->selected());
        if (ImGui::MenuItem("Model"))  addModel();
        if (ImGui::MenuItem("Point Light")) addLight(LightType::Point);
        if (ImGui::MenuItem("Spot Light"))  addLight(LightType::Spot);
        if (ImGui::MenuItem("Sound"))       addSound();
        ImGui::Separator();
        if (ImGui::BeginMenu("Ready-made")) {
            for (const PremadeInfo& p : premadeList()) {
                if (ImGui::MenuItem(p.name)) spawnPremade(p.kind);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tip);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset Camera")) m_viewport->resetCamera();
        ImGui::Separator();
        if (ImGui::MenuItem("Settings...")) m_showSettings = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Test")) {
        if (ImGui::MenuItem(m_playing ? "Stop" : "Play", "F5")) togglePlay();
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void Editor::renderDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    if (m_openDiscard) { ImGui::OpenPopup("Unsaved changes"); m_openDiscard = false; }
    if (m_openSaveAs)  { ImGui::OpenPopup("Save Game");       m_openSaveAs = false; }
    if (m_openOpen)    { ImGui::OpenPopup("Open Game");       m_openOpen = false; m_openPathInput.clear(); }
    if (m_openInfo)    { ImGui::OpenPopup("Game Settings");   m_openInfo = false; }

    // --- Discard changes? ---
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have changes that aren't saved yet.");
        ImGui::Text("Throw them away?");
        ImGui::Spacing();
        if (ImGui::Button("Discard changes", ImVec2(150, 0))) {
            ImGui::CloseCurrentPopup();
            if (m_pending == Pending::New) { newScene(); m_pending = Pending::None; }
            else if (m_pending == Pending::Open) m_openOpen = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_pending = Pending::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- Save As: just ask for a name; it goes in the games folder ---
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Name your game:");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##name", &m_nameInput, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled("Saved in: %s", Paths::gamesFolder().string().c_str());
        ImGui::Spacing();
        bool valid = !m_nameInput.empty() && m_nameInput.find_first_of("/\\:*?\"<>|") == std::string::npos;
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Save", ImVec2(100, 0)) || (enter && valid)) {
            if (m_scene->info().title == "My Game") m_scene->info().title = m_nameInput;
            saveFile((Paths::gamesFolder() / (m_nameInput + Paths::kExtension)).string());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // --- Open: pick from the games folder or type a path ---
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Open Game", nullptr)) {
        m_pending = Pending::None;
        ImGui::TextDisabled("Games in %s", Paths::gamesFolder().string().c_str());
        ImGui::BeginChild("##games", ImVec2(0, 220), ImGuiChildFlags_Borders);
        auto games = Paths::listGames();
        if (games.empty()) ImGui::TextDisabled("No saved games yet.");
        for (auto& g : games) {
            if (ImGui::Selectable(g.stem().string().c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                openFile(g.string());
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click to open");
        }
        ImGui::EndChild();
        ImGui::Text("...or type a file path:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##path", &m_openPathInput);
        ImGui::BeginDisabled(m_openPathInput.empty());
        if (ImGui::Button("Open path", ImVec2(110, 0))) {
            openFile(m_openPathInput);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // --- Game settings (shown in the Player app) ---
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Game Settings", nullptr)) {
        GameInfo& info = m_scene->info();
        ImGui::TextDisabled("How your game shows up in Guts&BoltsPlayer");
        ImGui::InputText("Title",  &info.title);
        ImGui::InputText("Author", &info.author);
        ImGui::InputTextMultiline("Description", &info.description, ImVec2(-1, 90));
        if (ImGui::Button("Done", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::renderToolbar() {
    const ImVec4 kBarBg = {0.086f, 0.094f, 0.114f, 1.0f};

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBarBg);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(10, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 7));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(6, 7));

    float barH = ImGui::GetFrameHeight() + 14.0f;
    ImGui::BeginChild("##toolbar", ImVec2(0, barH), ImGuiChildFlags_AlwaysUseWindowPadding,
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

    // Play / Stop (playtest mode).
    {
        ImVec4 col = m_playing ? ImVec4(0.80f, 0.27f, 0.27f, 1.0f)
                               : ImVec4(0.20f, 0.65f, 0.32f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(col.x + 0.08f, col.y + 0.08f, col.z + 0.08f, 1.0f));
        if (ImGui::Button(m_playing ? "Stop" : "Play")) togglePlay();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s playtest  (F5)", m_playing ? "Stop" : "Start");
        ImGui::SameLine();
    }
    sep();

    ImGui::BeginDisabled(m_playing);
    if (ImGui::Button("Undo")) undo();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Undo  (Ctrl+Z)");
    ImGui::SameLine();
    if (ImGui::Button("Redo")) redo();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Redo  (Ctrl+Y)");
    ImGui::SameLine();
    if (ImGui::Button("Save")) save();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save  (Ctrl+S)");
    ImGui::EndDisabled();
    sep();

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

    if (ImGui::Button("+ Part"))   spawnPrimitive(PrimitiveType::Cube);
    ImGui::SameLine();
    if (ImGui::Button("+ Script")) addScript(m_scene->selected());
    sep();

    bool editable = canEdit(m_scene->selected());
    ImGui::BeginDisabled(!editable);
    if (ImGui::Button("Duplicate")) duplicateSelected();
    ImGui::SameLine();
    if (ImGui::Button("Delete"))    deleteSelected();
    ImGui::EndDisabled();
    sep();
    if (ImGui::Button("Settings")) m_showSettings = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frame rate and graphics quality");

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void Editor::renderStatusBar() {
    const ImVec4 kBarBg  = {0.086f, 0.094f, 0.114f, 1.0f};
    const ImVec4 kAccent = {0.40f, 0.66f, 1.00f, 1.0f};

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBarBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 5));
    ImGui::BeginChild("##statusbar", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (m_playing) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "PLAYING");
    } else {
        const char* toolName =
            m_state.tool == GizmoTool::Select    ? "Select" :
            m_state.tool == GizmoTool::Translate ? "Move"   :
            m_state.tool == GizmoTool::Rotate    ? "Rotate" : "Scale";
        ImGui::TextDisabled("Tool:");
        ImGui::SameLine();
        ImGui::TextColored(kAccent, "%s", toolName);
    }
    ImGui::SameLine(); ImGui::TextDisabled("   "); ImGui::SameLine();

    if (SceneNode* sel = m_scene->selected()) {
        ImGui::TextDisabled("Selected:");
        ImGui::SameLine();
        ImGui::TextUnformatted(sel->name.c_str());
    } else {
        ImGui::TextDisabled("Nothing selected");
    }

    if (GraphicsSettings::get().showFps) {
        ImGui::SameLine(); ImGui::TextDisabled("   "); ImGui::SameLine();
        ImGui::TextDisabled("%.0f FPS", ImGui::GetIO().Framerate);
    }

    ImGui::SameLine();
    const char* hint = m_playing
        ? "WASD move   Space jump   Right-drag camera   Wheel zoom   Click parts   F5/Esc stop"
        : "[Q] Select  [W] Move  [E] Rotate  [R] Scale    MMB orbit  Shift+MMB pan  Wheel zoom    "
          "F focus  Del delete  Ctrl+D duplicate  Ctrl+Z undo  F5 play";
    float avail = ImGui::GetContentRegionAvail().x;
    float tw    = ImGui::CalcTextSize(hint).x;
    if (tw < avail) ImGui::SameLine(ImGui::GetCursorPosX() + (avail - tw));
    ImGui::TextDisabled("%s", hint);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}
