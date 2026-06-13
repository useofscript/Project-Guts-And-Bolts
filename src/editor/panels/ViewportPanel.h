#pragma once
#include <memory>
#include <glm/glm.hpp>
#include "../../renderer/Camera.h"
#include "../../renderer/Framebuffer.h"
#include "../../renderer/ShadowMap.h"

struct GLFWwindow;
struct EditorState;
struct ImVec2;
class Scene;
class Shader;

// 3D viewport: renders the scene to an off-screen framebuffer and displays it
// as an ImGui image. Handles Blender-style camera navigation, click-to-select
// picking and transform gizmos while hovered.
class ViewportPanel {
public:
    ViewportPanel(GLFWwindow* window, Scene* scene, EditorState* state);
    ~ViewportPanel();

    void render(float dt);
    void resetCamera();

    float cameraYaw() const;                  // for camera-relative controls
    void  frameOn(const glm::vec3& target);   // point the camera at a target

private:
    void handleInput();
    void drawScene();
    void renderShadowPass(const glm::mat4& lightSpace);
    void buildGrid();
    void buildAxes();
    void buildSky();
    void drawGizmo(const glm::mat4& view, const glm::mat4& proj,
                   const glm::vec2& imgMin, const glm::vec2& imgSize);
    void pickAt(const glm::vec2& mouse, const glm::vec2& imgMin, const glm::vec2& imgSize,
                const glm::mat4& view, const glm::mat4& proj);
    void focusSelected();

    GLFWwindow*  m_window;
    Scene*       m_scene;
    EditorState* m_state;

    Camera                  m_camera;
    Framebuffer             m_fbo;
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Shader> m_gridShader;
    std::unique_ptr<Shader> m_skyShader;
    std::unique_ptr<Shader> m_depthShader;
    ShadowMap               m_shadow;

    unsigned int m_gridVao = 0, m_gridVbo = 0;
    unsigned int m_axisVao = 0, m_axisVbo = 0;
    unsigned int m_skyVao  = 0;
    int          m_gridVertexCount = 0;

    int  m_viewW = 0, m_viewH = 0;
    bool m_hovered = false;
};
