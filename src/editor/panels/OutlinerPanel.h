#pragma once
#include <functional>

class Scene;
class SceneNode;

// The Explorer: every object in the scene as a tree. Click to select,
// double-click a Script to edit it, drag an object onto another to put it
// inside, right-click for more actions.
class OutlinerPanel {
public:
    using NodeFn = std::function<void(SceneNode*)>;
    OutlinerPanel(Scene* scene, NodeFn openScript, NodeFn addScriptTo);
    void render();

private:
    void drawNode(SceneNode* node);

    Scene*     m_scene;
    NodeFn     m_openScript;
    NodeFn     m_addScriptTo;
    SceneNode* m_pendingDelete = nullptr;
    SceneNode* m_dragNode      = nullptr;   // reparent after drawing
    SceneNode* m_dropTarget    = nullptr;
};
