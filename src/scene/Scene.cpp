#include "Scene.h"

Scene::Scene() {
    m_root = std::make_unique<SceneNode>("Scene");

    m_player = std::make_unique<Player>();
    m_player->setSpawn({2.0f, 0.0f, 0.0f});
    m_player->build(this);
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
    return m_root->addChild(std::move(node));
}

void Scene::removeNode(SceneNode* node) {
    if (!node || node == m_root.get()) return;
    if (node == m_selected) deselect();
    if (node->parent) node->parent->removeChild(node);
}

void Scene::forEach(std::function<void(SceneNode*)> fn) {
    walk(m_root.get(), fn);
}

void Scene::walk(SceneNode* node, std::function<void(SceneNode*)>& fn) {
    fn(node);
    for (auto& child : node->children)
        walk(child.get(), fn);
}
