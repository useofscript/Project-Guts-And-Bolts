#pragma once
#include <memory>
#include <glm/glm.hpp>
#include "ShadowMap.h"

class Scene;
class Shader;
class Camera;
class Framebuffer;

// Draws a Scene (sky, shadows, lit parts, fog and the optional editor grid)
// into a framebuffer. Shared by the editor viewport and the Player app.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    void render(Scene& scene, const Camera& camera, Framebuffer& target, bool showGrid);

private:
    void renderShadowPass(Scene& scene, const glm::mat4& lightSpace);
    void buildGrid();
    void buildAxes();
    void buildSky();

    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Shader> m_gridShader;
    std::unique_ptr<Shader> m_skyShader;
    std::unique_ptr<Shader> m_depthShader;
    ShadowMap               m_shadow;

    unsigned int m_gridVao = 0, m_gridVbo = 0;
    unsigned int m_axisVao = 0, m_axisVbo = 0;
    unsigned int m_skyVao  = 0;
    int          m_gridVertexCount = 0;
};
