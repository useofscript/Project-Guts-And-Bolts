#pragma once
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include "ShadowMap.h"

class Scene;
class SceneNode;
class Shader;
class Camera;
class Framebuffer;
class Mesh;

// Draws a Scene into a framebuffer: sun shadows, sky with clouds and stars,
// physically-based lit parts, point / spot lights, fog, then post-processing
// (ambient occlusion, bloom, tone mapping, colour grading, FXAA).
// Shared by the editor viewport and the Player app.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    void render(Scene& scene, const Camera& camera, Framebuffer& target, bool showGrid);

    // Offscreen buffer with a colour texture and (optionally) a depth texture.
    struct Target {
        unsigned int fbo = 0, color = 0, depth = 0;
        int w = 0, h = 0;
    };

private:
    void renderShadowPass(Scene& scene, const glm::mat4& lightSpace);
    void drawGeometry(Scene& scene, const Camera& camera, bool editing);
    static glm::mat4 decalMatrix(const SceneNode& decal);
    void drawConstraints(Scene& scene, bool editing);
    void postProcess(Scene& scene, const Camera& camera, Framebuffer& target);
    void renderLiquid(Scene& scene, const Camera& camera);   // real liquid (Liquid.cpp)
    void ensureTargets(int w, int h);
    void buildGrid();
public:
    // The floor grid's line spacing in studs (it follows Move snapping).
    void setGridSpacing(float studs);
private:
    float m_gridSpacing = 1.0f;
    void buildAxes();

    std::unique_ptr<Shader> m_lit, m_grid, m_sky, m_depth;
    std::unique_ptr<Shader> m_ssao, m_bloomPre, m_bloomDown, m_bloomUp, m_composite, m_fxaa;
    std::unique_ptr<Shader> m_fluidDepth, m_fluidThick, m_fluidBlur, m_fluidShade, m_fluidSimple;
    Target m_fDepth, m_fTmp, m_fThick, m_sceneCopy;   // liquid: depth, blur scratch, thickness, the scene behind
    unsigned int m_fluidVao = 0, m_fluidVbo = 0;
    ShadowMap               m_shadow;
    int                     m_shadowRes = 0;

    static constexpr int kBloomLevels = 6;
    Target m_hdr, m_ao, m_ldr, m_bloom[kBloomLevels];

    unsigned int m_gridVao = 0, m_gridVbo = 0;
    unsigned int m_axisVao = 0, m_axisVbo = 0;
    unsigned int m_emptyVao = 0;
    int          m_gridVertexCount = 0;
    double       m_startTime = 0.0;
    std::unordered_map<uint64_t, std::unique_ptr<Mesh>> m_waterMeshes;   // wavy surfaces while playing
    std::unique_ptr<Mesh> m_floodMesh;                                    // flowing water
};
