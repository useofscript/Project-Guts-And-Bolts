#include "SceneRenderer.h"
#include "Shader.h"
#include "Mesh.h"
#include "Camera.h"
#include "Framebuffer.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace {

// Grid lines sit a hair above y = 0 so they show on top of the Baseplate.
constexpr float kGridY = 0.01f;


const char* kLitVert = R"(#version 450 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;

out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos  = world.xyz;
    vNormal    = normalize(uNormalMat * aNormal);
    gl_Position = uProj * uView * world;
}
)";

const char* kLitFrag = R"(#version 450 core
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3  uColor;
uniform vec3  uSunDir;        // surface -> sun, normalized
uniform vec3  uSunColor;
uniform float uSunIntensity;
uniform vec3  uAmbient;       // ambient colour * intensity
uniform vec3  uViewPos;
uniform bool  uSelected;
uniform int   uMaterial;      // 0 plastic, 1 metal, 2 neon, 3 wood
uniform float uAlpha;         // 1 = opaque

uniform bool  uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;

uniform bool      uShadowsEnabled;
uniform mat4      uLightSpace;
uniform sampler2D uShadowMap;

out vec4 FragColor;

// Returns 1.0 = fully lit, 0.0 = fully shadowed (3x3 PCF).
float sunVisibility(vec3 N, vec3 L) {
    vec4 lc = uLightSpace * vec4(vWorldPos, 1.0);
    vec3 p  = lc.xyz / lc.w * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;

    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0008);
    vec2  texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float d = texture(uShadowMap, p.xy + vec2(x, y) * texel).r;
            shadow += (p.z - bias > d) ? 1.0 : 0.0;
        }
    return 1.0 - shadow / 9.0;
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vWorldPos);

    // Neon: unlit, emissive — glows regardless of the sun.
    if (uMaterial == 2) {
        FragColor = vec4(uColor * 1.5, uAlpha);
        return;
    }

    vec3 L = normalize(uSunDir);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);

    // Per-material specular response.
    float specPow = 32.0, specScale = 0.30;
    if      (uMaterial == 1) { specPow = 80.0; specScale = 0.90; }  // metal
    else if (uMaterial == 3) { specPow =  8.0; specScale = 0.04; }  // wood (matte)

    float spec = pow(max(dot(N, H), 0.0), specPow) * specScale;
    vec3  sun  = uSunColor * uSunIntensity;
    float vis  = uShadowsEnabled ? sunVisibility(N, L) : 1.0;
    vec3  base = uColor * (uAmbient + sun * diff * vis) + sun * spec * vis;

    if (uSelected) {
        // Fresnel-style rim glow in editor orange for the active object.
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        base = mix(base, vec3(1.0, 0.55, 0.15), rim * 0.8);
    }

    if (uFogEnabled) {
        float dist = length(uViewPos - vWorldPos);
        float f = clamp(1.0 - exp(-uFogDensity * dist), 0.0, 1.0);
        base = mix(base, uFogColor, f);
    }

    FragColor = vec4(base, uAlpha);
}
)";

const char* kSkyVert = R"(#version 450 core
// Fullscreen triangle generated from gl_VertexID — no vertex buffer needed.
out vec2 vNdc;
void main() {
    float x = float((gl_VertexID & 1) << 2) - 1.0;  // -1, 3, -1
    float y = float((gl_VertexID & 2) << 1) - 1.0;  // -1, -1, 3
    vNdc = vec2(x, y);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

const char* kSkyFrag = R"(#version 450 core
in vec2 vNdc;
uniform mat4  uInvViewProj;
uniform vec3  uZenith;
uniform vec3  uHorizon;
uniform vec3  uGround;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;
out vec4 FragColor;

void main() {
    // Reconstruct the world-space view ray for this pixel.
    vec4 near = uInvViewProj * vec4(vNdc, -1.0, 1.0);
    vec4 far  = uInvViewProj * vec4(vNdc,  1.0, 1.0);
    vec3 dir  = normalize(far.xyz / far.w - near.xyz / near.w);

    float t = dir.y;
    vec3 sky;
    if (t > 0.0) sky = mix(uHorizon, uZenith, pow(clamp(t, 0.0, 1.0), 0.45));
    else         sky = mix(uHorizon, uGround, pow(clamp(-t, 0.0, 1.0), 0.5));

    // Sun disc + soft glow.
    float d    = max(dot(dir, normalize(uSunDir)), 0.0);
    float disc = smoothstep(0.9990, 0.9996, d);
    float glow = pow(d, 250.0) * 0.6 + pow(d, 12.0) * 0.15;
    sky += uSunColor * (disc * 4.0 + glow) * uSunIntensity;

    FragColor = vec4(sky, 1.0);
}
)";

const char* kGridVert = R"(#version 450 core
layout(location=0) in vec3 aPos;
uniform mat4 uView;
uniform mat4 uProj;
out float vDist;
void main() {
    vDist = length(aPos.xz);
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

const char* kGridFrag = R"(#version 450 core
in float vDist;
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    // Fade grid lines out with distance from the origin.
    float a = clamp(1.0 - vDist / 22.0, 0.0, 1.0);
    FragColor = vec4(uColor, a * 0.6);
}
)";

const char* kDepthVert = R"(#version 450 core
layout(location=0) in vec3 aPos;
uniform mat4 uLightSpace;
uniform mat4 uModel;
void main() { gl_Position = uLightSpace * uModel * vec4(aPos, 1.0); }
)";

const char* kDepthFrag = R"(#version 450 core
void main() {}
)";

} // namespace

SceneRenderer::SceneRenderer() {
    m_shader      = std::make_unique<Shader>(kLitVert,   kLitFrag);
    m_gridShader  = std::make_unique<Shader>(kGridVert,  kGridFrag);
    m_skyShader   = std::make_unique<Shader>(kSkyVert,   kSkyFrag);
    m_depthShader = std::make_unique<Shader>(kDepthVert, kDepthFrag);
    m_shadow.init(2048);
    buildGrid();
    buildAxes();
    buildSky();
}

SceneRenderer::~SceneRenderer() {
    if (m_gridVbo) glDeleteBuffers(1, &m_gridVbo);
    if (m_gridVao) glDeleteVertexArrays(1, &m_gridVao);
    if (m_axisVbo) glDeleteBuffers(1, &m_axisVbo);
    if (m_axisVao) glDeleteVertexArrays(1, &m_axisVao);
    if (m_skyVao)  glDeleteVertexArrays(1, &m_skyVao);
}

void SceneRenderer::buildGrid() {
    std::vector<glm::vec3> lines;
    const int   half = 10;
    const float ext  = (float)half;
    for (int i = -half; i <= half; ++i) {
        // Skip the two centre lines; they are drawn separately as coloured axes.
        if (i == 0) continue;
        lines.push_back({(float)i, kGridY, -ext});
        lines.push_back({(float)i, kGridY,  ext});
        lines.push_back({-ext, kGridY, (float)i});
        lines.push_back({ ext, kGridY, (float)i});
    }
    m_gridVertexCount = (int)lines.size();

    glGenVertexArrays(1, &m_gridVao);
    glGenBuffers(1, &m_gridVbo);
    glBindVertexArray(m_gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(lines.size() * sizeof(glm::vec3)),
                 lines.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);
}

void SceneRenderer::buildAxes() {
    // Two coloured centre lines (X and Z) in their own VAO, drawn as two ranges.
    const glm::vec3 verts[] = {
        {-10, kGridY, 0}, {10, kGridY, 0},   // X axis  (range 0..2)
        {0, kGridY, -10}, {0, kGridY, 10},   // Z axis  (range 2..4)
    };
    glGenVertexArrays(1, &m_axisVao);
    glGenBuffers(1, &m_axisVbo);
    glBindVertexArray(m_axisVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_axisVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);
}

void SceneRenderer::buildSky() {
    // The sky shader synthesises its own vertices, but core-profile draws still
    // require a bound (empty) VAO.
    glGenVertexArrays(1, &m_skyVao);
}

void SceneRenderer::renderShadowPass(Scene& scene, const glm::mat4& lightSpace) {
    m_shadow.bindForWrite();
    glEnable(GL_DEPTH_TEST);
    // Cull front faces while filling the shadow map to reduce surface acne.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    m_depthShader->bind();
    m_depthShader->setMat4("uLightSpace", lightSpace);
    scene.forEach([&](SceneNode* node) {
        if (!node->mesh || !node->visible || !node->castShadow) return;
        if (node->transparency > 0.5f) return;   // mostly see-through: skip
        m_depthShader->setMat4("uModel", node->worldMatrix());
        node->mesh->draw();
    });

    glDisable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void SceneRenderer::render(Scene& scene, const Camera& camera, Framebuffer& fbo, bool showGrid) {
    const Environment& env = scene.environment();
    glm::vec3 sunDir = env.sunDirection();

    // --- Shadow map (rendered from the sun's point of view) ---
    // Centre the shadow area on what the camera looks at, snapped to the
    // shadow-map texel grid so shadows don't shimmer as the camera moves.
    const float shadowExtent = 22.0f, shadowDist = 40.0f;
    float texel = (2.0f * shadowExtent) / (float)m_shadow.size();
    glm::vec3 shadowCenter = glm::floor(camera.pivot / texel) * texel;
    glm::mat4 lightView = glm::lookAt(shadowCenter + sunDir * shadowDist,
                                      shadowCenter, glm::vec3(0, 1, 0));
    glm::mat4 lightProj = glm::ortho(-shadowExtent, shadowExtent,
                                     -shadowExtent, shadowExtent,
                                     0.1f, shadowDist * 2.0f);
    glm::mat4 lightSpace = lightProj * lightView;
    if (env.shadows) renderShadowPass(scene, lightSpace);

    fbo.bind();
    glEnable(GL_DEPTH_TEST);
    glClearColor(env.skyHorizon.r, env.skyHorizon.g, env.skyHorizon.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = camera.view();
    glm::mat4 proj = camera.projection();

    // --- Procedural sky (drawn first, behind everything) ---
    if (env.showSky) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        m_skyShader->bind();
        m_skyShader->setMat4("uInvViewProj", glm::inverse(proj * view));
        m_skyShader->setVec3("uZenith",  env.skyZenith);
        m_skyShader->setVec3("uHorizon", env.skyHorizon);
        m_skyShader->setVec3("uGround",  env.skyGround);
        m_skyShader->setVec3("uSunDir",  sunDir);
        m_skyShader->setVec3("uSunColor", env.sunColor);
        m_skyShader->setFloat("uSunIntensity", env.sunIntensity);
        glBindVertexArray(m_skyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    // --- Grid + axes (editor only) ---
    if (showGrid) {
    m_gridShader->bind();
    m_gridShader->setMat4("uView", view);
    m_gridShader->setMat4("uProj", proj);
    m_gridShader->setVec3("uColor", {0.35f, 0.36f, 0.40f});
    glBindVertexArray(m_gridVao);
    glDrawArrays(GL_LINES, 0, m_gridVertexCount);

    // Coloured centre axes (X = red, Z = blue), from a persistent VAO.
    glBindVertexArray(m_axisVao);
    m_gridShader->setVec3("uColor", {0.75f, 0.25f, 0.25f});
    glDrawArrays(GL_LINES, 0, 2);
    m_gridShader->setVec3("uColor", {0.25f, 0.45f, 0.80f});
    glDrawArrays(GL_LINES, 2, 2);
    }

    // --- Lit scene geometry ---
    m_shader->bind();
    m_shader->setMat4("uView", view);
    m_shader->setMat4("uProj", proj);
    m_shader->setVec3("uSunDir", sunDir);
    m_shader->setVec3("uSunColor", env.sunColor);
    m_shader->setFloat("uSunIntensity", env.sunIntensity);
    m_shader->setVec3("uAmbient", env.ambientColor * env.ambientIntensity);
    m_shader->setVec3("uViewPos", camera.position());
    m_shader->setBool("uFogEnabled", env.fogEnabled);
    m_shader->setVec3("uFogColor", env.fogColor);
    m_shader->setFloat("uFogDensity", env.fogDensity);
    m_shader->setBool("uShadowsEnabled", env.shadows);
    m_shader->setMat4("uLightSpace", lightSpace);
    m_shadow.bindForRead(0);
    m_shader->setInt("uShadowMap", 0);

    scene.forEach([&](SceneNode* node) {
        if (!node->mesh || !node->visible) return;
        if (node->kind != NodeKind::Part) return;
        float alpha = 1.0f - node->transparency;
        if (alpha <= 0.001f) return;   // fully transparent — nothing to draw

        glm::mat4 model = node->worldMatrix();
        glm::mat3 nrm   = glm::transpose(glm::inverse(glm::mat3(model)));
        m_shader->setMat4("uModel", model);
        m_shader->setMat3("uNormalMat", nrm);
        m_shader->setVec3("uColor", node->color);
        m_shader->setBool("uSelected", node->selected);
        m_shader->setInt("uMaterial", (int)node->material);
        m_shader->setFloat("uAlpha", alpha);

        // Don't let translucent parts occlude what's behind them via the depth
        // buffer (blending is enabled globally).
        bool translucent = alpha < 0.999f;
        if (translucent) glDepthMask(GL_FALSE);
        node->mesh->draw();
        if (translucent) glDepthMask(GL_TRUE);
    });

    glBindVertexArray(0);
    fbo.unbind();
}

