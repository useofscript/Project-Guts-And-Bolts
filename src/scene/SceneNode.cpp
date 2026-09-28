#include "SceneNode.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace { uint64_t g_nextId = 1; }

uint64_t SceneNode::newId() { return g_nextId++; }
void SceneNode::reserveId(uint64_t used) { if (used >= g_nextId) g_nextId = used + 1; }

glm::mat4 Transform::matrix() const {
    // Rotation order Z * Y * X (applied X first) to match how ImGuizmo composes
    // and decomposes Euler angles, so the gizmo stays in sync with the inspector.
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m = glm::rotate(m, glm::radians(rotation.z), {0,0,1});
    m = glm::rotate(m, glm::radians(rotation.y), {0,1,0});
    m = glm::rotate(m, glm::radians(rotation.x), {1,0,0});
    m = glm::scale(m, scale);
    return m;
}

SceneNode::SceneNode(std::string name, NodeKind kind)
    : id(newId()), name(std::move(name)), kind(kind) {}

SceneNode* SceneNode::addChild(std::unique_ptr<SceneNode> child) {
    child->parent = this;
    children.push_back(std::move(child));
    return children.back().get();
}

void SceneNode::removeChild(SceneNode* child) {
    children.erase(std::remove_if(children.begin(), children.end(),
        [child](const auto& p) { return p.get() == child; }), children.end());
}

std::unique_ptr<SceneNode> SceneNode::detachChild(SceneNode* child) {
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (it->get() == child) {
            std::unique_ptr<SceneNode> out = std::move(*it);
            children.erase(it);
            out->parent = nullptr;
            return out;
        }
    }
    return nullptr;
}

SceneNode* SceneNode::findChild(const std::string& n, bool recursive) const {
    for (auto& c : children)
        if (c->name == n) return c.get();
    if (recursive)
        for (auto& c : children)
            if (SceneNode* f = c->findChild(n, true)) return f;
    return nullptr;
}

bool SceneNode::isAncestorOf(const SceneNode* other) const {
    for (const SceneNode* p = other ? other->parent : nullptr; p; p = p->parent)
        if (p == this) return true;
    return false;
}

glm::mat4 SceneNode::worldMatrix() const {
    if (parent) return parent->worldMatrix() * transform.matrix();
    return transform.matrix();
}

std::string SceneNode::fullName() const {
    if (!parent) return name;
    return parent->fullName() + "." + name;
}
