#pragma once
#include "SceneNode.h"
#include "Environment.h"
#include <functional>
#include <memory>

class Scene {
public:
    Scene();

    SceneNode*   root()        { return m_root.get(); }
    SceneNode*   selected()    { return m_selected; }
    Environment& environment() { return m_env; }

    void select  (SceneNode* node);
    void deselect();

    SceneNode* addNode(const std::string& name, PrimitiveType type, std::shared_ptr<Mesh> mesh);
    void       removeNode(SceneNode* node);

    void forEach(std::function<void(SceneNode*)> fn);

private:
    void walk(SceneNode* node, std::function<void(SceneNode*)>& fn);

    std::unique_ptr<SceneNode> m_root;
    SceneNode*                 m_selected = nullptr;
    Environment                m_env;
};
