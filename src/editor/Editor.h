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
class EnvironmentPanel;
class ToolboxPanel;
class PlayerPanel;

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
    void spawnPrimitive(PrimitiveType type);
    void duplicateSelected();
    void deleteSelected();
    void newScene();

    void togglePlay();
    void updatePlay(float dt);

    GLFWwindow*                      m_window;
    Scene*                           m_scene;
    EditorState                      m_state;
    std::unique_ptr<ViewportPanel>    m_viewport;
    std::unique_ptr<OutlinerPanel>    m_outliner;
    std::unique_ptr<PropertiesPanel>  m_properties;
    std::unique_ptr<EnvironmentPanel> m_environment;
    std::unique_ptr<ToolboxPanel>     m_toolbox;
    std::unique_ptr<PlayerPanel>      m_player;

    bool m_firstLayout = true;
    bool m_playing     = false;
    int  m_objCounter  = 0;
};
