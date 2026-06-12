#include "SceneNode.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

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

SceneNode::SceneNode(std::string name) : name(std::move(name)) {}

SceneNode* SceneNode::addChild(std::unique_ptr<SceneNode> child) {
    child->parent = this;
    children.push_back(std::move(child));
    return children.back().get();
}

void SceneNode::removeChild(SceneNode* child) {
    children.erase(std::remove_if(children.begin(), children.end(),
        [child](const auto& p) { return p.get() == child; }), children.end());
}

glm::mat4 SceneNode::worldMatrix() const {
    if (parent) return parent->worldMatrix() * transform.matrix();
    return transform.matrix();
}
