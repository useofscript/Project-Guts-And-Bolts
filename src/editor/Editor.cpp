#include "Editor.h"
#include "../game/Profile.h"
#include "../scene/EditMesh.h"
#include "../online/OnlineClient.h"
#include "Plugins.h"
#include "../scene/Physics.h"
#include "../scripting/ScriptEngine.h"
#include "Theme.h"
#include "Icons.h"
#include "../scene/RobloxFile.h"
#include "panels/ViewportPanel.h"
#include "panels/OutlinerPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/EnvironmentPanel.h"
#include "panels/ToolboxPanel.h"
#include "panels/PlayerPanel.h"
#include "panels/OutputPanel.h"
#include "panels/ScriptEditorPanel.h"
#include "TeamCreate.h"
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
#include <glm/gtx/euler_angles.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <ImGuizmo.h>
#include <string>
#include <algorithm>
#include <filesystem>
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
    EditorTheme::applyStudio();
    m_outliner->onInsert = [this](SceneNode* parent) { m_insertParent = parent; m_openInsert = true; };
    m_viewport->onMode = [this](StudioMode mode) { setMode(mode); };
    m_outliner->contextMenuExtras = [this] {
        ImGui::Separator();
        if (ImGui::MenuItem("Expand Selected", "Ctrl+Right")) m_outliner->expand(m_scene->selection());
        if (ImGui::MenuItem("Collapse Selected", "Ctrl+Left")) m_outliner->collapse(m_scene->selection());
        if (ImGui::MenuItem("Group", "Ctrl+G")) m_deferred = [this] { groupSelected(); };
        SceneNode* sel = m_scene->selected();
        if (sel && sel->kind == NodeKind::Model && ImGui::MenuItem("Ungroup", "Ctrl+U"))
            m_deferred = [this] { ungroupSelected(); };
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) m_deferred = [this] { duplicateSelected(); };
    };
    m_properties   = std::make_unique<PropertiesPanel>(scene, open);
    m_properties->m_editMesh = [this](SceneNode* n) {
        m_deferred = [this, n] { m_scene->select(n); setMode(StudioMode::Modeling); };   // not while Properties is drawing
    };
    m_environment  = std::make_unique<EnvironmentPanel>(scene);
    m_player       = std::make_unique<PlayerPanel>(scene);
    m_output       = std::make_unique<OutputPanel>();
    m_scriptEditor = std::make_unique<ScriptEditorPanel>(scene);
    m_team         = std::make_unique<TeamCreate>(scene);
    m_viewport->setTeam(m_team.get());

    ToolboxPanel::Actions actions;
    actions.spawnPart    = [this](PrimitiveType t) { spawnPrimitive(t); };
    actions.addScript    = [this] { addScript(m_scene->selected()); };
    actions.addModel     = [this] { addModel(); };
    actions.addLight     = [this](LightType t) { addLight(t); };
    actions.addSound     = [this] { addSound(); };
    actions.startConnect = [this](int t) { m_state.connectTool = t; m_state.connectFirst = 0; m_viewport->focus(); };
    m_viewport->onConnect = [this](SceneNode* a, glm::vec3 pa, SceneNode* b, glm::vec3 pb) {
        connectParts(m_state.connectTool, a, pa, b, pb);
    };
    actions.spawnPremade = [this](Premade p) { spawnPremade(p); };
    m_toolbox = std::make_unique<ToolboxPanel>(actions);

    resetHistory();
    Log::system("Welcome to Guts and Bolts! Press Play (F5) to test your game.");
    m_plugins = std::make_unique<Plugins>(scene);
    m_plugins->reload();
}

// Out-of-line so the panel types are complete here.
Editor::~Editor() {
    m_plugins.reset();
    m_team.reset();
    if (m_session) m_session->stop();
}

void Editor::render(float dt) {
    Online::update();   // replies from the Guts&Bolts server
    ImGuizmo::BeginFrame();
    // Docked tabs appear over a few frames; make sure the 3D view ends up on top.
    if (++m_frame <= 3) m_viewport->focus();

    // Team Create: bring in other people's edits (they wait while playtesting).
    m_team->update(!m_playing);
    if (m_team->consumeRemoteEdit()) {
        m_committed = Serializer::saveScene(*m_scene);
        m_undo.clear();          // undo can't safely rewind other people's work
        m_redo.clear();
        m_dirty = true;
    }
    m_team->setSelection(m_scene->selected() ? m_scene->selected()->id : 0);
    handleShortcuts();
    checkModeling();
    if (m_playing && (!m_state.simPaused || m_state.simStep)) {
        // F6 pauses the world; F7 moves it on by one frame.
        m_session->update(m_state.simStep ? 1.0f / 60.0f : dt, m_viewport->cameraYaw(), true);
        m_state.simStep = false;
        Player* p = m_scene->player();
        if (p && !m_session->runOnly()) m_viewport->frameOn(p->focusPoint());   // Run: the camera stays free
    }
    buildDockspace();
    m_viewport->render(dt);
    if (m_showPanel[kPanelExplorer]) m_outliner->render();
    if (m_deferred) { auto f = std::move(m_deferred); m_deferred = nullptr; f(); }
    if (m_showPanel[kPanelProperties]) m_properties->render();
    if (m_showPanel[kPanelLighting])   m_environment->render();
    if (m_showPanel[kPanelToolbox])    m_toolbox->render();
    if (m_showPanel[kPanelPlayer])     m_player->render();
    if (m_showPanel[kPanelOutput])     m_output->render();
    if (m_showPanel[kPanelScript])     m_scriptEditor->render();
    m_scriptEditor->renderFindAll();
    renderServerDialog();
    renderPublishDialog();
    renderMarketplace();
    if (m_showPanel[kPanelCommandBar]) renderCommandBar();
    renderInsertObject();
    renderDialogs();
    renderShortcuts();
    if (m_showPanel[kPanelTeam]) renderTeamPanel();
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
        if (m_state.mode == StudioMode::Modeling) exitModeling();
        m_state.mode = m_session->runOnly() ? StudioMode::Simulate : StudioMode::Play;
        m_state.simPaused = false;
        Log::system(m_session->runOnly() ? "Simulation started (no character) - Shift+F5 to stop." : "Game started.");
        m_session->start();
    } else {
        uint64_t sel = m_scene->selected() ? m_scene->selected()->id : 0;
        m_session->stop();
        m_session->setRunOnly(false);
        m_viewport->setSession(nullptr);
        m_playing = false;
        Serializer::loadScene(*m_scene, m_playSnapshot);
        m_scene->select(m_scene->findById(sel));
        m_committed = m_playSnapshot;
        m_state.mode = StudioMode::Build;
        m_state.simPaused = false;
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
    m_team->localChanged();
}

void Editor::restore(const std::string& snapshot) {
    std::vector<uint64_t> sel;
    for (SceneNode* n : m_scene->selection()) sel.push_back(n->id);
    Serializer::loadScene(*m_scene, snapshot);
    for (uint64_t id : sel) m_scene->addToSelection(m_scene->findById(id));
    m_committed = snapshot;
    m_dirty = true;
    m_team->localChanged();
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

// A new MeshPart (a cube to start from), straight into Modeling mode.
void Editor::addMeshPart() {
    SceneNode* n = addPrimitive("MeshPart", PrimitiveType::Cube);
    MeshEdit::attach(*n, MeshEdit::fromPrimitive(PrimitiveType::Cube));
    n->transform.scale = glm::vec3(2.0f);
    n->transform.position.y += 0.5f;
    setMode(StudioMode::Modeling);
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

void Editor::startTeamCreate(bool host, const std::string& address) {
    std::string err;
    bool ok = host ? m_team->host(kTeamCreatePort, err) : m_team->join(address, err);
    if (!ok) Log::error("Team Create: " + err);
}

void Editor::testAddPart(const std::string& name) {
    SceneNode* n = addPrimitive("Cube", PrimitiveType::Cube);
    n->name = name;
    n->color = {0.2f, 0.4f, 1.0f};
    n->transform.position = {-3, 0.5f, 2};
}

void Editor::testExportRoblox(const std::string& path) {
    std::string err;
    if (RobloxFile::exportPlace(*m_scene, path, err)) Log::system("Exported " + path);
    else Log::error(err);
}

void Editor::testSelect(const std::string& names) {
    m_scene->deselect();
    std::string list = "," + names + ",";
    m_scene->forEach([&](SceneNode* n) {
        if (list.find("," + n->name + ",") != std::string::npos) m_scene->addToSelection(n);
    });
    Log::info("Selected " + std::to_string(m_scene->selection().size()) + " object(s)");
}

void Editor::testPremades(const std::string& list) {
    int i = 0;
    for (const PremadeInfo& p : premadeList()) {
        if (list != "all" && list.find(p.name) == std::string::npos) continue;
        glm::vec3 at((i % 4) * 14.0f - 21.0f, 0.0f, (i / 4) * -14.0f - 8.0f);
        buildPremade(*m_scene, p.kind, at);
        ++i;
    }
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

void Editor::connectParts(int type, SceneNode* a, glm::vec3 pa, SceneNode* b, glm::vec3 pb) {
    if (!canEdit(a) || !canEdit(b)) {
        Log::warn("Constraints can only connect normal parts (not the character).");
        return;
    }
    bool motor = type == 5;
    ConstraintType ct = motor ? ConstraintType::Hinge : (ConstraintType)type;
    // Hinges turn around the direction the first clicked surface faces.
    glm::mat4 wa = a->worldMatrix();
    glm::vec3 local = glm::vec3(glm::inverse(wa) * glm::vec4(pa, 1.0f));
    int ax = 0;
    for (int i = 1; i < 3; ++i) if (std::abs(local[i]) > std::abs(local[ax])) ax = i;
    glm::vec3 normal = glm::normalize(glm::vec3(wa[ax])) * (local[ax] >= 0 ? 1.0f : -1.0f);

    SceneNode* made = makeConstraint(*m_scene, ct, a, pa, b, pb, normal, motor);
    m_scene->select(made);
    Log::system(std::string("Connected ") + a->name + " and " + b->name + " with a " +
                (motor ? "motor" : kConstraintNames[(int)ct]) + ". Press Play to try it!");
}

void Editor::openScript(SceneNode* script) {
    if (script && script->isScript()) {
        m_scene->select(script);
        m_showPanel[kPanelScript] = true;
        m_scriptEditor->open(script->id);
    }
}

void Editor::duplicateSelected() {
    std::vector<SceneNode*> copies;
    for (SceneNode* sel : m_scene->selectionRoots()) {
        if (!canEdit(sel)) continue;
        auto copy = Serializer::clone(*sel);
        copy->name += " Copy";
        if (copy->kind == NodeKind::Part) copy->transform.position.x += 1.0f;   // so it's visible
        copies.push_back(m_scene->insert(std::move(copy), sel->parent));
    }
    if (copies.empty()) return;
    m_scene->deselect();
    for (SceneNode* c : copies) m_scene->addToSelection(c);
}

void Editor::deleteSelected() {
    for (SceneNode* sel : m_scene->selectionRoots())
        if (!m_scene->isProtected(sel)) m_scene->removeNode(sel);
}

void Editor::copySelected() {
    std::vector<std::string> clip;
    for (SceneNode* sel : m_scene->selectionRoots())
        if (canEdit(sel)) clip.push_back(Serializer::nodeToString(*sel));
    if (!clip.empty()) m_clipboard = std::move(clip);
}

void Editor::paste() {
    if (m_clipboard.empty()) return;
    SceneNode* parent = m_scene->selected();
    if (!parent || parent->kind != NodeKind::Model || m_scene->isCharacterPart(parent))
        parent = m_scene->root();
    std::vector<SceneNode*> pasted;
    for (const std::string& c : m_clipboard) {
        auto n = Serializer::nodeFromString(c, true);
        if (!n) continue;
        if (n->kind == NodeKind::Part) n->transform.position.x += 1.0f;
        pasted.push_back(m_scene->insert(std::move(n), parent));
    }
    if (pasted.empty()) return;
    m_scene->deselect();
    for (SceneNode* n : pasted) m_scene->addToSelection(n);
}

// Ctrl+G: put everything selected inside a new Model.
void Editor::groupSelected() {
    std::vector<SceneNode*> items;
    for (SceneNode* n : m_scene->selectionRoots()) if (canEdit(n)) items.push_back(n);
    if (items.empty()) return;
    SceneNode* parent = items.back()->parent ? items.back()->parent : m_scene->root();
    for (SceneNode* n : items) if (n->parent != parent) { parent = m_scene->root(); break; }
    SceneNode* model = m_scene->insert(std::make_unique<SceneNode>("Model", NodeKind::Model), parent);
    for (SceneNode* n : items) m_scene->reparent(n, model);
    m_scene->select(model);
    m_outliner->expand({model});
    Log::info("Grouped " + std::to_string(items.size()) + " object(s) into a Model.");
}

// Ctrl+U: take everything out of the selected Models and remove the Models.
void Editor::ungroupSelected() {
    std::vector<SceneNode*> freed;
    for (SceneNode* m : m_scene->selectionRoots()) {
        if (m->kind != NodeKind::Model || !canEdit(m) || !m->parent) continue;
        SceneNode* parent = m->parent;
        std::vector<SceneNode*> kids;
        for (auto& c : m->children) kids.push_back(c.get());
        for (SceneNode* k : kids) if (m_scene->reparent(k, parent)) freed.push_back(k);
        m_scene->removeNode(m);
    }
    if (freed.empty()) return;
    m_scene->deselect();
    for (SceneNode* n : freed) m_scene->addToSelection(n);
}

void Editor::selectAll() {
    m_scene->deselect();
    for (auto& c : m_scene->root()->children)
        if (canEdit(c.get()) && !c->internal) m_scene->addToSelection(c.get());
}

void Editor::selectParent() {
    std::vector<SceneNode*> parents;
    for (SceneNode* n : m_scene->selection())
        if (n->parent && n->parent != m_scene->root() &&
            std::find(parents.begin(), parents.end(), n->parent) == parents.end())
            parents.push_back(n->parent);
    if (parents.empty()) return;
    m_scene->deselect();
    for (SceneNode* p : parents) m_scene->addToSelection(p);
}

void Editor::selectChildren() {
    std::vector<SceneNode*> kids;
    for (SceneNode* n : m_scene->selection())
        for (auto& c : n->children) if (!c->internal) kids.push_back(c.get());
    if (kids.empty()) return;
    m_outliner->expand(m_scene->selection());
    m_scene->deselect();
    for (SceneNode* k : kids) m_scene->addToSelection(k);
}

void Editor::toggleHidden() {
    auto sel = m_scene->selectionRoots();
    if (sel.empty()) return;
    bool show = !sel.back()->visible;
    for (SceneNode* n : sel) if (!m_scene->isCharacterPart(n)) n->visible = show;
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
    if (RobloxFile::isRobloxFile(path)) {
        RobloxFile::Report report;
        std::string err;
        if (RobloxFile::isPlace(path)) {
            if (!RobloxFile::importPlace(*m_scene, path, report, err)) { Log::error("Couldn't import " + path + ": " + err); return; }
            m_path.clear();   // it's a Guts and Bolts game now: Save As picks a name
            resetHistory();
            m_dirty = true;
            Log::system("Imported Roblox place " + path + ": " + report.summary() + ". Save it to keep it.");
        } else {
            auto added = RobloxFile::importModel(*m_scene, nullptr, path, report, err);
            if (added.empty()) { Log::error("Couldn't import " + path + ": " + err); return; }
            m_scene->deselect();
            for (SceneNode* n : added) m_scene->addToSelection(n);
            Log::system("Inserted Roblox model " + path + ": " + report.summary());
        }
        for (const std::string& n : report.notes) Log::warn("Roblox import: " + n);
        return;
    }
    std::string err;
    if (!Serializer::loadGameFile(*m_scene, path, &err)) {
        Log::error("Couldn't load " + path + ": " + err);
        m_scene->buildDefault();
    } else {
        m_path = path;
        Log::system("Opened " + path);
    }
    resetHistory();
}

void Editor::exportRoblox(bool selectionOnly) {
    std::string name = m_scene->info().title.empty() ? "My Game" : m_scene->info().title;
    std::string err;
    if (selectionOnly) {
        auto sel = m_scene->selectionRoots();
        if (sel.empty()) { Log::warn("Select something to export first."); return; }
        std::string path = (Paths::gamesFolder() / (sel.back()->name + ".rbxmx")).string();
        if (RobloxFile::exportModel(*m_scene, sel, path, err)) Log::system("Exported a Roblox model to " + path);
        else Log::error("Export failed: " + err);
    } else {
        std::string path = (Paths::gamesFolder() / (name + ".rbxlx")).string();
        if (RobloxFile::exportPlace(*m_scene, path, err))
            Log::system("Exported a Roblox place to " + path + " (open it in Roblox Studio)");
        else Log::error("Export failed: " + err);
    }
}

void Editor::saveFile(const std::string& path) {
    if (m_playing) togglePlay();
    // A new game is signed by whoever made it (it shows on the site's Create page).
    GameInfo& info = m_scene->info();
    if ((info.author.empty() || info.author == "Builder") && Profile::get().name != "Player")
        info.author = Profile::get().name;
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

    // Testing (like Roblox Studio): F5 Play, F8 Simulate, Shift+F5 Stop.
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        if (io.KeyShift || m_playing) setMode(StudioMode::Build);
        else setMode(StudioMode::Play);
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false) && !m_playing) { setMode(StudioMode::Simulate); return; }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) m_showShortcuts = !m_showShortcuts;

    if (m_playing) {
        // While testing, WASD drives the character, not the editor.
        if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) m_state.simPaused = !m_state.simPaused;
        if (ImGui::IsKeyPressed(ImGuiKey_F7, false)) { m_state.simPaused = true; m_state.simStep = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) togglePlay();
        return;
    }
    if (m_state.mode == StudioMode::Modeling) { handleModelingKeys(); return; }
    // Tab: reshape the selected part (Blender's Edit Mode).
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !io.KeyCtrl && !io.KeyAlt) { setMode(StudioMode::Modeling); return; }

    if (io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) toggleLocked();
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) toggleAnchored();
        return;
    }

    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { io.KeyShift ? redo() : undo(); }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelected();
        if (ImGui::IsKeyPressed(ImGuiKey_X, false) && !io.KeyShift) cutSelected();   // Ctrl+Shift+X = Explorer search
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) { if (io.KeyShift) pasteInto(); else paste(); }
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (io.KeyShift) { m_nameInput = m_scene->info().title; m_openSaveAs = true; } else save(); }
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) { m_pending = Pending::Open; m_openDiscard = m_dirty; if (!m_dirty) m_openOpen = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_N, false)) { m_pending = Pending::New;  m_openDiscard = m_dirty; if (!m_dirty) { newScene(); m_pending = Pending::None; } }
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) selectAll();
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) groupSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_U, false)) ungroupSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_I, false)) { m_insertParent = m_scene->selected(); m_openInsert = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) m_state.gizmoLocal = !m_state.gizmoLocal;
        // Roblox's tool keys.
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) m_state.tool = GizmoTool::Select;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) m_state.tool = GizmoTool::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) m_state.tool = GizmoTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_4, false)) m_state.tool = GizmoTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) m_outliner->expand(m_scene->selection());
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
            if (io.KeyShift) m_outliner->collapseAll();
            else             m_outliner->collapse(m_scene->selection());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))   selectParent();
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) selectChildren();
        return;
    }

    // Plain keys (W A S D Q E fly the camera in the Viewport).
    if (ImGui::IsKeyPressed(ImGuiKey_H, false)) toggleHidden();
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) m_outliner->beginRename(m_scene->selected());
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) deleteSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_scene->deselect(); m_state.connectTool = -1; }
}

void Editor::cutSelected() {
    copySelected();
    deleteSelected();
}

// Ctrl+Shift+V: paste inside the selected object instead of next to it.
void Editor::pasteInto() {
    SceneNode* target = m_scene->selected();
    if (m_clipboard.empty() || !target || m_scene->isCharacterPart(target) || target->isScript()) { paste(); return; }
    std::vector<SceneNode*> pasted;
    for (const std::string& c : m_clipboard) {
        auto n = Serializer::nodeFromString(c, true);
        if (n) pasted.push_back(m_scene->insert(std::move(n), target));
    }
    m_outliner->expand({target});
    m_scene->deselect();
    for (SceneNode* n : pasted) m_scene->addToSelection(n);
}

void Editor::toggleLocked() {
    auto sel = m_scene->selectionRoots();
    if (sel.empty()) return;
    bool lock = !sel.back()->locked;
    for (SceneNode* n : sel) if (canEdit(n)) n->locked = lock;
    Log::info(lock ? "Locked: they can't be clicked in the Viewport now (pick them in the Explorer)." : "Unlocked.");
}

void Editor::toggleAnchored() {
    auto sel = m_scene->selectionRoots();
    if (sel.empty()) return;
    bool anchor = !sel.back()->anchored;
    for (SceneNode* n : sel) {
        std::vector<SceneNode*> stack{n};
        while (!stack.empty()) {
            SceneNode* k = stack.back(); stack.pop_back();
            if (k->isPart() && canEdit(k)) k->anchored = anchor;
            for (auto& c : k->children) stack.push_back(c.get());
        }
    }
}

// Move a Model's origin to the middle of its parts (keeping the parts where they are).
void Editor::centerModelPivot() {
    SceneNode* m = m_scene->selected();
    if (!m || m->kind != NodeKind::Model || !canEdit(m)) return;
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (auto& c : m->children) if (c->isPart()) { glm::vec3 p(c->worldMatrix()[3]); lo = glm::min(lo, p); hi = glm::max(hi, p); }
    if (lo.x > hi.x) return;
    glm::vec3 center = (lo + hi) * 0.5f;
    glm::vec3 local = glm::vec3(glm::inverse(m->worldMatrix()) * glm::vec4(center, 1.0f));
    for (auto& c : m->children) c->transform.position -= local;
    glm::vec3 d = center - glm::vec3(m->worldMatrix()[3]);
    if (m->parent) d = glm::vec3(glm::inverse(m->parent->worldMatrix()) * glm::vec4(d, 0.0f));
    m->transform.position += d;
}

// Play (0), Play Here (1: start where the camera is looking) or Run (2: no player).
void Editor::startPlay(int mode) {
    if (m_playing) return;
    m_playMode = mode;
    m_session->setRunOnly(mode == 2);
    togglePlay();
    Player* p = m_scene->player();
    SceneNode* r = p ? p->root() : nullptr;
    if (!r) return;
    if (mode == 1) {
        glm::vec3 target = m_viewport->cameraPivot();
        SceneNode* character = r;
        SceneNode* ground = Physics::raycast(*m_scene, target + glm::vec3(0, 60, 0), glm::vec3(0, -1, 0), nullptr, character);
        glm::vec3 at = target;
        if (ground) { AABB b = Physics::worldBounds(ground); at.y = b.max.y + 0.01f; }
        r->transform.position = at;
    }
    if (mode == 2) r->visible = false;   // Run: nobody's playing (Stop brings the character back)
}

// Ctrl+I / the Explorer's + button: a searchable list of things to insert.
void Editor::insertObject(const std::string& what, SceneNode* parent) {
    if (parent && (m_scene->isCharacterPart(parent) || parent->isScript())) parent = nullptr;
    auto put = [&](std::unique_ptr<SceneNode> n) {
        SceneNode* raw = m_scene->insert(std::move(n), parent);
        m_scene->select(raw);
        if (parent) m_outliner->expand({parent});
        return raw;
    };
    auto part = [&](const char* name, PrimitiveType t) {
        auto n = std::make_unique<SceneNode>(name, NodeKind::Part);
        n->primitiveType = t;
        n->mesh = MeshLibrary::get(t);
        n->transform.scale = t == PrimitiveType::Sphere ? glm::vec3(2) : glm::vec3(2, 1, 1);
        n->transform.position = parent ? glm::vec3(0, 1, 0) : spawnPoint() + glm::vec3(0, 0.5f, 0);
        return put(std::move(n));
    };
    if (what == "Part") part("Part", PrimitiveType::Cube);
    else if (what == "Sphere") part("Sphere", PrimitiveType::Sphere);
    else if (what == "Cylinder") part("Cylinder", PrimitiveType::Cylinder);
    else if (what == "MeshPart") {
        SceneNode* mp = part("MeshPart", PrimitiveType::Cube);
        MeshEdit::attach(*mp, MeshEdit::fromPrimitive(PrimitiveType::Cube));
        mp->transform.scale = glm::vec3(2.0f);
        setMode(StudioMode::Modeling);
    }
    else if (what == "SpawnLocation") {
        SceneNode* sp = part("SpawnLocation", PrimitiveType::Cube);
        sp->transform.scale = {3, 0.2f, 3};
        sp->color = {0.25f, 0.6f, 1.0f};
    }
    else if (what == "Model" || what == "Folder") put(std::make_unique<SceneNode>(what, NodeKind::Model));
    else if (what == "Script" || what == "LocalScript") { addScript(parent); }
    else if (what == "ModuleScript") {
        auto n = std::make_unique<SceneNode>("ModuleScript", NodeKind::Script);
        n->isModule = true;
        n->source = "local module = {}\n\nreturn module\n";
        openScript(put(std::move(n)));
    }
    else if (what == "PointLight" || what == "SpotLight") {
        m_scene->select(parent);
        addLight(what == "SpotLight" ? LightType::Spot : LightType::Point);
    }
    else if (what == "Sound") { m_scene->select(parent); addSound(); }
    else if (what == "Attachment") put(std::make_unique<SceneNode>("Attachment", NodeKind::Attachment));
    else if (what == "ForceField") put(std::make_unique<SceneNode>("ForceField", NodeKind::ForceField));
    else if (what == "IntValue" || what == "NumberValue" || what == "StringValue" || what == "BoolValue") {
        auto v = std::make_unique<SceneNode>(what, NodeKind::Value);
        v->intValue = what == "IntValue";
        v->value.type = what == "StringValue" ? Attribute::String : what == "BoolValue" ? Attribute::Bool : Attribute::Number;
        put(std::move(v));
    }
    else if (what == "Decal") {
        auto d = std::make_unique<SceneNode>("Decal", NodeKind::Decal);
        d->color = {1.0f, 1.0f, 1.0f};
        put(std::move(d));
    }
    else if (what == "Tool") {
        // A tool with a Handle ready to hold, sitting where you're looking.
        SceneNode* tool = put(std::make_unique<SceneNode>("Tool", NodeKind::Tool));
        tool->transform.position = parent ? glm::vec3(0.0f) : spawnPoint() + glm::vec3(0.0f, 1.0f, 0.0f);
        auto handle = std::make_unique<SceneNode>("Handle");
        handle->primitiveType = PrimitiveType::Cube;
        handle->mesh = MeshLibrary::get(PrimitiveType::Cube);
        handle->transform.scale = {0.2f, 1.2f, 0.2f};
        handle->color = {0.55f, 0.35f, 0.2f};
        handle->canCollide = false;
        tool->addChild(std::move(handle));
        m_scene->markDirty();
    }
    else {
        for (const PremadeInfo& p : premadeList()) if (what == p.name) { spawnPremade(p.kind); break; }
    }
}

void Editor::renderInsertObject() {
    if (m_openInsert) { ImGui::OpenPopup("Insert Object"); m_openInsert = false; m_insertFilter.clear(); }
    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing, ImVec2(0.0f, 0.0f));
    if (!ImGui::BeginPopup("Insert Object")) return;
    SceneNode* parent = m_insertParent;
    ImGui::TextDisabled("Insert into: %s", parent ? parent->name.c_str() : "Workspace");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputTextWithHint("##find", "Search objects", &m_insertFilter, ImGuiInputTextFlags_EnterReturnsTrue);
    struct O { const char* name; Icons::Id icon; };
    std::vector<O> list = {
        {"Part", Icons::Id::Part}, {"Sphere", Icons::Id::Sphere}, {"Cylinder", Icons::Id::Cylinder},
        {"MeshPart", Icons::Id::Mesh}, {"SpawnLocation", Icons::Id::Part}, {"Model", Icons::Id::Model}, {"Folder", Icons::Id::Folder},
        {"Script", Icons::Id::Script}, {"LocalScript", Icons::Id::Script}, {"ModuleScript", Icons::Id::ModuleScript},
        {"PointLight", Icons::Id::Light}, {"SpotLight", Icons::Id::Light}, {"Sound", Icons::Id::Sound},
        {"Attachment", Icons::Id::Attachment}, {"ForceField", Icons::Id::ForceField}, {"Tool", Icons::Id::Tool}, {"Decal", Icons::Id::Decal},
        {"IntValue", Icons::Id::Value}, {"NumberValue", Icons::Id::Value}, {"StringValue", Icons::Id::Value}, {"BoolValue", Icons::Id::Value}};
    for (const PremadeInfo& p : premadeList()) list.push_back({p.name, Icons::Id::Model});
    std::string f = m_insertFilter;
    for (char& c : f) c = (char)std::tolower((unsigned char)c);
    ImGui::BeginChild("##list");
    bool first = true;
    for (const O& o : list) {
        std::string n = o.name;
        for (char& c : n) c = (char)std::tolower((unsigned char)c);
        if (!f.empty() && n.find(f) == std::string::npos) continue;
        Icons::inlineIcon(o.icon);
        ImGui::SameLine();
        if (ImGui::Selectable(o.name) || (enter && first)) {
            insertObject(o.name, parent);
            ImGui::CloseCurrentPopup();
        }
        first = false;
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

// The Command Bar: type Lua and press Enter to run it on the game right now
// (e.g. to change lots of parts at once). Up / Down go through what you ran.
// Command Bar: run a bit of Lua against the game right now (undoable).
void Editor::runCommand(const std::string& code) {
    Log::info("> " + code);
    ScriptEngine engine(m_scene);
    engine.start(false);
    std::string err;
    if (!engine.runCommand(code, err)) Log::error(err);
    engine.stop();
}

void Editor::renderCommandBar() {
    if (!ImGui::Begin("Command Bar")) { ImGui::End(); return; }
    ImGui::TextDisabled("Run Lua on your game now. Enter runs, Shift+Enter adds a line, Ctrl+Up / Ctrl+Down: history.");
    ImGuiIO& io = ImGui::GetIO();
    static bool active = false;
    static std::string pending;
    static bool hasPending = false;
    if (active && io.KeyCtrl && !m_cmdHistory.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            m_cmdHistoryPos = m_cmdHistoryPos < 0 ? (int)m_cmdHistory.size() - 1 : std::max(0, m_cmdHistoryPos - 1);
            pending = m_cmdHistory[m_cmdHistoryPos]; hasPending = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && m_cmdHistoryPos >= 0) {
            m_cmdHistoryPos = m_cmdHistoryPos + 1 >= (int)m_cmdHistory.size() ? -1 : m_cmdHistoryPos + 1;
            pending = m_cmdHistoryPos >= 0 ? m_cmdHistory[m_cmdHistoryPos] : std::string(); hasPending = true;
        }
    }
    auto swap = [](ImGuiInputTextCallbackData* d) -> int {
        if (!hasPending) return 0;
        d->DeleteChars(0, d->BufTextLen);
        d->InsertChars(0, pending.c_str());
        hasPending = false;
        return 0;
    };
    // Shift+Enter makes a new line; plain Enter runs (ImGui's Ctrl+Enter mode, with Shift standing in for Ctrl).
    bool shiftEnter = io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Enter);
    ImGuiInputTextFlags fl = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways |
                             ImGuiInputTextFlags_AllowTabInput;
    if (shiftEnter) fl &= ~ImGuiInputTextFlags_EnterReturnsTrue;
    bool run = ImGui::InputTextMultiline("##cmd", &m_cmdInput, ImVec2(-1, -1), fl, swap);
    active = ImGui::IsItemActive();
    if (run && !m_cmdInput.empty()) {
        runCommand(m_cmdInput);
        m_cmdHistory.push_back(m_cmdInput);
        m_cmdHistoryPos = -1;
        m_cmdInput.clear();
        pending.clear(); hasPending = true;   // clear the box
    }
    ImGui::End();
}

// F1: every keyboard shortcut in one place.
void Editor::renderShortcuts() {
    if (!m_showShortcuts) return;
    struct Row { const char* keys; const char* what; };
    static const Row kSections[][14] = {
        {{"Ctrl+1 / 2 / 3 / 4", "Select / Move / Scale / Rotate tool"},
         {"Ctrl+L", "Switch the gizmo between local and world"},
         {"W A S D", "Fly the camera (hold Shift to go faster)"},
         {"Q / E", "Camera down / up"},
         {"Right-drag", "Look around"},
         {"Middle-drag", "Pan (Shift+middle-drag orbits)"},
         {"Mouse wheel", "Zoom"},
         {"F", "Focus the camera on the selection"}},
        {{"Click", "Select (a part in a Model picks the Model)"},
         {"Alt+click", "Select just the part"},
         {"Ctrl+click / Shift+click", "Add to the selection"},
         {"Ctrl+A", "Select everything in the Workspace"},
         {"Ctrl+Up / Ctrl+Down", "Select parent / children"},
         {"Esc", "Select nothing"}},
        {{"Ctrl+Right / Ctrl+Left", "Expand / collapse selected (everything inside)"},
         {"Ctrl+Shift+Left", "Collapse the whole Explorer"},
         {"Ctrl+I", "Insert Object (searchable)"},
         {"F2", "Rename"},
         {"H", "Hide / show"}},
        {{"Ctrl+C / X / V", "Copy / cut / paste"},
         {"Ctrl+Shift+V", "Paste into the selection"},
         {"Ctrl+D", "Duplicate"},
         {"Del", "Delete"},
         {"Ctrl+G / Ctrl+U", "Group into a Model / ungroup"},
         {"Alt+L", "Lock (can't be clicked in the Viewport)"},
         {"Alt+A", "Anchor / unanchor"},
         {"Ctrl+Z / Ctrl+Y", "Undo / redo"}},
        {{"Ctrl+N / Ctrl+O", "New / open (also Roblox .rbxl / .rbxm)"},
         {"Ctrl+S", "Save (Ctrl+Shift+S: save as)"},
         {"F1", "Show / hide this list"}},
        {{"Tab", "Modeling mode: reshape the selected part (Tab again = done)"},
         {"F8", "Simulate mode: physics and scripts run, you fly around"},
         {"F5", "Play mode: playtest with your character"},
         {"Shift+F5 / Esc", "Stop: back to Build mode"},
         {"F6 / F7", "Pause / step one frame (Simulate and Play)"}},
        {{"1 / 2 / 3", "Pick vertices / edges / faces"},
         {"Click / Shift+click / Ctrl+click", "Pick / add or toggle / take away"},
         {"Drag a box", "Pick everything inside it"},
         {"A / Alt+A / Ctrl+I", "Pick all (again = none) / none / swap"},
         {"E", "Extrude: pull faces or edges out"},
         {"I", "Inset: a smaller face inside each face"},
         {"X / Del", "Delete what's picked"},
         {"M", "Merge the picked corners into one"},
         {"F", "Fill: make a face between picked corners"},
         {"Alt+Z", "X-ray: see and pick through the mesh"},
         {"Right-drag + WASD", "Fly the camera (letters are tools here)"}},
    };
    static const char* kTitles[] = {"Camera & tools", "Selecting", "Explorer", "Editing", "Files",
                                    "Modes", "Modeling mode"};
    ImGui::SetNextWindowSize(ImVec2(520, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Keyboard Shortcuts", &m_showShortcuts)) {
        for (int s = 0; s < 7; ++s) {
            ImGui::SeparatorText(kTitles[s]);
            if (ImGui::BeginTable(kTitles[s], 2)) {
                ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
                for (const Row& r : kSections[s]) {
                    if (!r.keys) break;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1), "%s", r.keys);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.what);
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
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

        // Roblox Studio's layout: Toolbox on the left, Explorer over
        // Properties on the right, Output and the Command Bar along the bottom.
        ImGuiID left, center, right, bottom;
        ImGui::DockBuilderSplitNode(dsId,   ImGuiDir_Left,  0.17f, &left,   &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, &right,  &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,  0.26f, &bottom, &center);
        ImGuiID rightTop, rightBottom;
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.45f, &rightTop, &rightBottom);
        ImGuiID bottomLeft, bottomRight;
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.42f, &bottomRight, &bottomLeft);

        ImGui::DockBuilderDockWindow("Toolbox",       left);
        ImGui::DockBuilderDockWindow("Script Editor", center);
        ImGui::DockBuilderDockWindow("Viewport",      center);
        ImGui::DockBuilderDockWindow("Output",        bottomLeft);
        ImGui::DockBuilderDockWindow("Command Bar",   bottomRight);
        ImGui::DockBuilderDockWindow("Team",          bottomRight);
        ImGui::DockBuilderDockWindow("Explorer",      rightTop);
        ImGui::DockBuilderDockWindow("Properties",    rightBottom);
        ImGui::DockBuilderDockWindow("Lighting",      rightBottom);
        ImGui::DockBuilderDockWindow("Player",        rightBottom);
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
        if (ImGui::MenuItem("Import Roblox File (.rbxl / .rbxm)...")) { m_pending = Pending::Open; m_openOpen = true; }
        if (ImGui::MenuItem("Export to Roblox Place (.rbxlx)")) exportRoblox(false);
        if (ImGui::MenuItem("Export Selection to Roblox Model (.rbxmx)", nullptr, false, m_scene->selected() != nullptr))
            exportRoblox(true);
        ImGui::Separator();
        if (ImGui::MenuItem(m_team->active() ? "Team Create (on)..." : "Team Create...")) m_openTeam = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Game Settings...")) m_openInfo = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Publish to Guts&Bolts...")) m_openPublish = true;
        if (ImGui::MenuItem("Guts&Bolts Server...")) m_openServer = true;
        if (ImGui::MenuItem("Marketplace (plugins, audio)")) m_showMarketplace = true;
        ImGui::Separator();
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
        if (ImGui::MenuItem("Rename",    "F2",     false, editable)) m_outliner->beginRename(sel);
        ImGui::Separator();
        if (ImGui::MenuItem("Select All", "Ctrl+A")) selectAll();
        if (ImGui::MenuItem("Select Parent", "Ctrl+Up", false, sel != nullptr)) selectParent();
        if (ImGui::MenuItem("Select Children", "Ctrl+Down", false, sel != nullptr)) selectChildren();
        ImGui::Separator();
        if (ImGui::MenuItem("Group", "Ctrl+G", false, editable)) groupSelected();
        if (ImGui::MenuItem("Ungroup", "Ctrl+U", false, editable && sel->kind == NodeKind::Model)) ungroupSelected();
        if (ImGui::MenuItem("Hide / Show", "H", false, sel != nullptr)) toggleHidden();
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
        SceneNode* sel = m_scene->selected();
        if (ImGui::MenuItem("Expand Selected", "Ctrl+Right", false, sel != nullptr)) m_outliner->expand(m_scene->selection());
        if (ImGui::MenuItem("Collapse Selected", "Ctrl+Left", false, sel != nullptr)) m_outliner->collapse(m_scene->selection());
        if (ImGui::MenuItem("Collapse All", "Ctrl+Shift+Left")) m_outliner->collapseAll();
        ImGui::Separator();
        if (ImGui::MenuItem("Settings...")) m_showSettings = true;
        if (ImGui::MenuItem("Keyboard Shortcuts", "F1", m_showShortcuts)) m_showShortcuts = !m_showShortcuts;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Mode")) {
        static const char* keys[] = {"Shift+F5", "Tab", "F8", "F5"};
        for (int i = 0; i < 4; ++i)
            if (ImGui::MenuItem(kStudioModeNames[i], keys[i], (int)m_state.mode == i)) setMode((StudioMode)i);
        ImGui::Separator();
        if (ImGui::MenuItem("Play Here", nullptr, false, !m_playing)) startPlay(1);
        if (ImGui::MenuItem(m_state.simPaused ? "Resume" : "Pause", "F6", false, m_playing)) m_state.simPaused = !m_state.simPaused;
        if (ImGui::MenuItem("Step one frame", "F7", false, m_playing)) { m_state.simPaused = true; m_state.simStep = true; }
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
    if (m_openTeam)    { ImGui::OpenPopup("Team Create");     m_openTeam = false; m_teamError.clear(); }

    // --- Team Create: host or join ---
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Team Create", nullptr)) {
        if (m_team->active()) {
            ImGui::TextWrapped("You're in Team Create with %d %s.", (int)m_team->members().size(),
                               m_team->members().size() == 1 ? "person" : "people");
            ImGui::TextDisabled("See the Team panel for who's here and to chat.");
            ImGui::Spacing();
            if (ImGui::Button("Leave Team Create", ImVec2(180, 0))) { m_team->leave(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextWrapped("Build a game together! Everyone sees everyone else's changes as they happen.");
            ImGui::SeparatorText("Host");
            ImGui::TextWrapped("Share the game that's open right now. Friends join with your address.");
            if (ImGui::Button("Start Team Create", ImVec2(200, 0))) {
                std::string err;
                if (m_team->host(kTeamCreatePort, err)) ImGui::CloseCurrentPopup();
                else m_teamError = err;
            }
            ImGui::SeparatorText("Join");
            ImGui::TextWrapped("Joining replaces what you have open with the host's game (save first!).");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##tcaddr", "host address, e.g. 192.168.1.20", &m_teamAddress);
            if (ImGui::Button("Join", ImVec2(120, 0))) {
                std::string err;
                if (m_team->join(m_teamAddress, err)) ImGui::CloseCurrentPopup();
                else m_teamError = err;
            }
            if (!m_teamError.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", m_teamError.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

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
        // Roblox models sitting in the games folder can be inserted too.
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(Paths::gamesFolder(), ec))
            if (e.path().extension() == ".rbxm" || e.path().extension() == ".rbxmx") games.push_back(e.path());
        if (games.empty()) ImGui::TextDisabled("No saved games yet.");
        for (auto& g : games) {
            std::string ext = g.extension().string();
            std::string label = g.stem().string();
            if (ext == ".rbxl" || ext == ".rbxlx") label += "   (Roblox place)";
            if (ext == ".rbxm" || ext == ".rbxmx") label += "   (Roblox model - inserts into this game)";
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                openFile(g.string());
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click to open");
        }
        ImGui::EndChild();
        ImGui::Text("...or type a file path (.gbscene, or Roblox .rbxl / .rbxlx / .rbxm / .rbxmx):");
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

void Editor::renderTeamPanel() {
    ImGui::Begin("Team");
    if (!m_team->active()) {
        if (m_team->waiting()) ImGui::TextDisabled("Joining...");
        else {
            ImGui::TextDisabled("Not in Team Create.");
            if (ImGui::Button("Start or join Team Create...")) m_openTeam = true;
        }
    } else {
        ImGui::TextDisabled(m_team->hosting() ? "Hosting Team Create" : "In Team Create");
        for (const auto& mem : m_team->members()) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float h = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.35f,
                ImGui::ColorConvertFloat4ToU32(ImVec4(mem.color.r, mem.color.g, mem.color.b, 1)));
            ImGui::Dummy(ImVec2(h, h));
            ImGui::SameLine();
            ImGui::Text("%s%s", mem.name.c_str(), mem.id == m_team->myId() ? " (you)" : "");
            if (SceneNode* n = mem.selected ? m_scene->findById(mem.selected) : nullptr) {
                ImGui::SameLine();
                ImGui::TextDisabled("- editing %s", n->name.c_str());
            }
        }
    }
    ImGui::Separator();
    ImGui::BeginChild("##teamchat", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
    for (const auto& l : m_team->chat()) {
        if (l.system) ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "%s", l.text.c_str());
        else {
            ImGui::TextColored(ImVec4(0.55f, 0.8f, 1, 1), "%s:", l.from.c_str());
            ImGui::SameLine();
            ImGui::TextWrapped("%s", l.text.c_str());
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::BeginDisabled(!m_team->active());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##teamsay", "Message your team...", &m_teamChat, ImGuiInputTextFlags_EnterReturnsTrue)) {
        m_team->say(m_teamChat);
        m_teamChat.clear();
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void Editor::renderStatusBar() {
    const ImVec4 kBarBg  = {0.086f, 0.094f, 0.114f, 1.0f};
    const ImVec4 kAccent = {0.40f, 0.66f, 1.00f, 1.0f};

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBarBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 5));
    ImGui::BeginChild("##statusbar", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (m_playing) {
        ImGui::TextColored(m_session->runOnly() ? ImVec4(0.4f, 0.75f, 1.0f, 1.0f) : ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "%s%s",
                           m_session->runOnly() ? "SIMULATE" : "PLAY", m_state.simPaused ? " (paused)" : "");
    } else if (m_state.mode == StudioMode::Modeling) {
        static const char* picks[] = {"vertices", "edges", "faces"};
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "MODELING");
        ImGui::SameLine();
        SceneNode* mn = m_scene->findById(m_state.modeling.node);
        ImGui::TextDisabled("  %d picked (%s)   %d corners, %d faces", MeshEdit::countSelected(m_state.modeling.sel),
                            picks[std::clamp(m_state.modeling.selectMode, 0, 2)],
                            mn && mn->editMesh ? (int)mn->editMesh->verts.size() : 0,
                            mn && mn->editMesh ? (int)mn->editMesh->faces.size() : 0);
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
    const char* hint = m_state.mode == StudioMode::Modeling
        ? "1/2/3 pick mode   E extrude   I inset   X delete   M merge   F fill   A all   Right-drag + WASD fly   Tab done"
        : m_playing && m_session->runOnly()
        ? "Click to inspect   Drag with gizmo   Right-drag + WASDQE fly   F6 pause   Shift+F5 stop"
        : m_playing
        ? "WASD move   Space jump   Right-drag camera   Wheel zoom   Click parts   F5/Esc stop"
        : "Ctrl+1-4 tools   Right-drag + WASDQE fly   MMB pan   F focus   Ctrl+D duplicate   F5 play   F1 all shortcuts";
    float avail = ImGui::GetContentRegionAvail().x;
    float tw    = ImGui::CalcTextSize(hint).x;
    if (tw < avail) ImGui::SameLine(ImGui::GetCursorPosX() + (avail - tw));
    ImGui::TextDisabled("%s", hint);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}
