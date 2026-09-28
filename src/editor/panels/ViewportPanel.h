#pragma once
#include <functional>
#include <memory>
#include <glm/glm.hpp>
#include "../../renderer/Camera.h"
#include "../../renderer/Framebuffer.h"
#include "../../renderer/SceneRenderer.h"
#include "../EditorState.h"
#include <imgui.h>

struct GLFWwindow;
class Scene;
class GameSession;
class TeamCreate;
class SceneNode;

// 3D viewport: renders the scene to an off-screen framebuffer and displays it
// as an ImGui image. In edit mode it handles camera navigation, click-to-select
// and transform gizmos; in Play mode it follows the character, forwards clicks
// to scripts and draws the in-game HUD.
class ViewportPanel {
public:
    ViewportPanel(GLFWwindow* window, Scene* scene, EditorState* state);
    ~ViewportPanel();

    void render(float dt);
    void resetCamera();

    float     cameraYaw() const;                  // for camera-relative controls
    glm::vec3 cameraPivot() const { return m_camera.pivot; }
    void      frameOn(const glm::vec3& target);   // point the camera at a target
    bool      hovered() const { return m_hovered; }
    bool      gizmoInUse() const;

    // While a session is set, the viewport is in Play mode.
    void setSession(GameSession* session) { m_session = session; }
    void focus() { m_wantFocus = true; }
    void setTeam(TeamCreate* t) { m_team = t; }
    // Called when the connect tool has picked two parts (and the clicked points).
    std::function<void(SceneNode*, glm::vec3, SceneNode*, glm::vec3)> onConnect;
    // The mode menu in the Viewport's corner asks for a new mode.
    std::function<void(StudioMode)> onMode;

private:
    void handleInput(float dt);
    void drawGizmo(const glm::mat4& view, const glm::mat4& proj,
                   const glm::vec2& imgMin, const glm::vec2& imgSize);
    void mouseRay(const glm::vec2& mouse, const glm::vec2& imgMin, const glm::vec2& imgSize,
                  const glm::mat4& view, const glm::mat4& proj, glm::vec3& ro, glm::vec3& rd) const;
    void focusSelected();
    void modelingView(const glm::mat4& view, const glm::mat4& proj,
                      const glm::vec2& imgMin, const glm::vec2& imgSize);   // ViewportModeling.cpp
    bool drawModeMenu(ImVec2 imgPos);   // true while the mouse is on it

    GLFWwindow*  m_window;
    Scene*       m_scene;
    EditorState* m_state;
    GameSession* m_session = nullptr;
    TeamCreate*  m_team = nullptr;

    Camera        m_camera;
    Framebuffer   m_fbo;
    SceneRenderer m_renderer;

    int  m_viewW = 0, m_viewH = 0;
    bool m_hovered = false;
    bool m_wantFocus = false;

    // Modeling mode
    glm::mat4 m_meshGizmo{1.0f};
    bool      m_meshDragging = false;
    bool      m_boxing = false;
    ImVec2    m_boxStart{0, 0};
};
