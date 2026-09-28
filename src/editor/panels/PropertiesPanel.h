#pragma once
#include <functional>

class Scene;
class SceneNode;

// Inspector for the selected object's properties.
class PropertiesPanel {
public:
    PropertiesPanel(Scene* scene, std::function<void(SceneNode*)> openScript);
    void render();

private:
    Scene*                          m_scene;
    std::function<void(SceneNode*)> m_openScript;
};
