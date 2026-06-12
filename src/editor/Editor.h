#pragma once
#include <memory>
#include <string>
#include "EditorState.h"
#include "../scene/SceneNode.h"

struct GLFWwindow;
class Scene;
class Mesh;
class ViewportPanel;
class OutlinerPanel;
class PropertiesPanel;

class Editor {
public:
    Editor(GLFWwindow* window, Scene* scene);
    ~Editor();
    void render(float dt);

private:
    void buildDockspace();
    void renderMenuBar();
    void renderToolbar();
    void renderStatusBar();
    void handleShortcuts();

    SceneNode* addPrimitive(const char* label, PrimitiveType type,
                            std::shared_ptr<Mesh> mesh);
    void duplicateSelected();
    void deleteSelected();
    void newScene();

    GLFWwindow*                      m_window;
    Scene*                           m_scene;
    EditorState                      m_state;
    std::unique_ptr<ViewportPanel>   m_viewport;
    std::unique_ptr<OutlinerPanel>   m_outliner;
    std::unique_ptr<PropertiesPanel> m_properties;

    bool m_firstLayout = true;
    int  m_objCounter  = 0;
};
