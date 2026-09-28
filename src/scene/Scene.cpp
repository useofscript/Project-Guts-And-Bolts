#include "Scene.h"
#include "../renderer/MeshLibrary.h"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

namespace {

const char* kWelcomeScript =
R"(-- Welcome to scripting in Guts and Bolts!
-- Scripts run when you press Play (F5). Anything you print()
-- shows up in the Output window at the bottom.

print("Hello world! The game has started.")

-- 'workspace' is the whole world. Let's find the part called "Cube":
local cube = workspace:FindFirstChild("Cube")

if cube then
    -- Make it slowly change colour, forever.
    while true do
        cube.Color = Color3.fromHSV((os.clock() * 0.1) % 1, 0.7, 1)
        wait(0.05)
    end
end
)";

} // namespace

Scene::Scene() {
    m_player = std::make_unique<Player>(this);
    buildDefault();
}

void Scene::buildDefault() {
    m_selected = nullptr;
    m_root = std::make_unique<SceneNode>("Workspace", NodeKind::Model);
    markDirty();

    // A big flat floor, like Roblox's Baseplate. Its top sits at y = 0.
    auto* base = addNode("Baseplate", PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    base->transform.position = {0.0f, -0.5f, 0.0f};
    base->transform.scale    = {64.0f, 1.0f, 64.0f};
    base->color              = {0.36f, 0.62f, 0.33f};

    // Where the character appears when you press Play.
    auto* spawn = addNode("SpawnLocation", PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    spawn->transform.position = {0.0f, 0.1f, 0.0f};
    spawn->transform.scale    = {3.0f, 0.2f, 3.0f};
    spawn->color              = {0.55f, 0.57f, 0.60f};

    auto* cube = addNode("Cube", PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    cube->transform.position = {3.0f, 0.5f, -3.0f};
    cube->color              = {0.85f, 0.35f, 0.30f};

    auto script = std::make_unique<SceneNode>("WelcomeScript", NodeKind::Script);
    script->source = kWelcomeScript;
    insert(std::move(script));

    m_env   = Environment{};
    m_world = WorldSettings{};
    m_info  = GameInfo{};
    m_player->setSpawn({0.0f, 0.2f, 0.0f});
    m_player->resetSettings();
    m_player->build();
}

void Scene::select(SceneNode* node) {
    if (m_selected) m_selected->selected = false;
    m_selected = node;
    if (m_selected) m_selected->selected = true;
}

void Scene::deselect() {
    if (m_selected) m_selected->selected = false;
    m_selected = nullptr;
}

SceneNode* Scene::addNode(const std::string& name, PrimitiveType type, std::shared_ptr<Mesh> mesh) {
    auto node           = std::make_unique<SceneNode>(name);
    node->primitiveType = type;
    node->mesh          = std::move(mesh);
    return insert(std::move(node));
}

SceneNode* Scene::insert(std::unique_ptr<SceneNode> node, SceneNode* parent) {
    if (!parent) parent = m_root.get();
    SceneNode* raw = parent->addChild(std::move(node));
    markDirty();
    return raw;
}

void Scene::removeNode(SceneNode* node) {
    if (!node || node == m_root.get()) return;
    if (m_selected && (node == m_selected || node->isAncestorOf(m_selected))) deselect();
    if (node->parent) node->parent->removeChild(node);
    markDirty();
}

std::unique_ptr<SceneNode> Scene::detach(SceneNode* node) {
    if (!node || node == m_root.get() || !node->parent) return nullptr;
    if (m_selected && (node == m_selected || node->isAncestorOf(m_selected))) deselect();
    markDirty();
    return node->parent->detachChild(node);
}

bool Scene::reparent(SceneNode* node, SceneNode* newParent) {
    if (!node || !newParent || node == m_root.get() || !node->parent) return false;
    if (node == newParent || node->isAncestorOf(newParent)) return false;   // no loops
    if (node->parent == newParent) return false;

    glm::mat4 world = node->worldMatrix();
    SceneNode* sel = m_selected;
    std::unique_ptr<SceneNode> owned = node->parent->detachChild(node);
    glm::mat4 local = glm::inverse(newParent->worldMatrix()) * world;

    float t[3], r[3], s[3];
    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local), t, r, s);
    owned->transform.position = {t[0], t[1], t[2]};
    owned->transform.rotation = {r[0], r[1], r[2]};
    owned->transform.scale    = {s[0], s[1], s[2]};

    newParent->addChild(std::move(owned));
    m_selected = sel;
    markDirty();
    return true;
}

void Scene::replaceRoot(std::unique_ptr<SceneNode> root) {
    m_selected = nullptr;
    m_root = std::move(root);
    markDirty();
}

void Scene::rebuildIndex() {
    m_index.clear();
    forEach([this](SceneNode* n) { m_index[n->id] = n; });
    m_indexDirty = false;
}

SceneNode* Scene::findById(uint64_t id) {
    if (m_indexDirty) rebuildIndex();
    auto it = m_index.find(id);
    if (it != m_index.end()) return it->second;
    // Nodes may have been added since the last rebuild — try once more.
    rebuildIndex();
    it = m_index.find(id);
    return it != m_index.end() ? it->second : nullptr;
}

bool Scene::isProtected(const SceneNode* node) const {
    return node == m_root.get() || (m_player && node && node->id == m_player->rootId());
}

bool Scene::isCharacterPart(const SceneNode* node) const {
    for (const SceneNode* n = node; n; n = n->parent)
        if (m_player && n->id == m_player->rootId()) return true;
    return false;
}

void Scene::forEach(std::function<void(SceneNode*)> fn) {
    walk(m_root.get(), fn);
}

void Scene::walk(SceneNode* node, std::function<void(SceneNode*)>& fn) {
    fn(node);
    for (auto& child : node->children)
        walk(child.get(), fn);
}
