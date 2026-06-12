#pragma once

class Scene;
class SceneNode;

// Hierarchy / outliner: lists every node in the scene as a selectable tree and
// offers a right-click context menu for per-node actions (delete, etc.).
class OutlinerPanel {
public:
    explicit OutlinerPanel(Scene* scene);
    void render();

private:
    void drawNode(SceneNode* node);

    Scene*      m_scene;
    SceneNode*  m_pendingDelete = nullptr;
};
