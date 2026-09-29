#include "SceneRenderer.h"
#include "Textures.h"
#include "Shader.h"
#include "Shaders.h"
#include "Mesh.h"
#include "Camera.h"
#include "Framebuffer.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../core/Settings.h"
#include "MeshLibrary.h"

#include "GL.h"
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace {

// Grid lines sit a hair above y = 0 so they show on top of the Baseplate.
constexpr float kGridY = 0.01f;
constexpr int   kMaxLights = 32;

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void destroyTarget(SceneRenderer::Target& t) {
    if (t.fbo)   glDeleteFramebuffers(1, &t.fbo);
    if (t.color) glDeleteTextures(1, &t.color);
    if (t.depth) glDeleteTextures(1, &t.depth);
    t = SceneRenderer::Target{};
}

// The HDR buffers need float colour targets. Every computer has them; phones
// usually do (an extension), and the rare ones that don't fall back to 8 bits.
GLenum hdrFormat() {
#ifdef GB_GLES
    static GLenum fmt = [] {
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; ++i) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (e && (std::strcmp(e, "GL_EXT_color_buffer_float") == 0 ||
                      std::strcmp(e, "GL_EXT_color_buffer_half_float") == 0))
                return (GLenum)GL_RGBA16F;
        }
        return (GLenum)GL_RGBA8;
    }();
    return fmt;
#else
    return GL_RGBA16F;
#endif
}

void createTarget(SceneRenderer::Target& t, int w, int h, GLenum fmt, bool withDepth) {
    destroyTarget(t);
    t.w = std::max(1, w);
    t.h = std::max(1, h);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);

    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    GLenum base = (fmt == GL_R8) ? GL_RED : GL_RGBA;
    GLenum type = (fmt == GL_RGBA16F) ? GL_FLOAT : GL_UNSIGNED_BYTE;   // OpenGL ES is strict about this
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, t.w, t.h, 0, base, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);

    if (withDepth) {
        glGenTextures(1, &t.depth);
        glBindTexture(GL_TEXTURE_2D, t.depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, t.w, t.h, 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t.depth, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void bindTarget(const SceneRenderer::Target& t) {
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glViewport(0, 0, t.w, t.h);
}

void bindTex(int unit, unsigned int tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

} // namespace

SceneRenderer::SceneRenderer() {
    using namespace Shaders;
    m_lit       = std::make_unique<Shader>(litVert, litFrag);
    m_grid      = std::make_unique<Shader>(gridVert, gridFrag);
    m_sky       = std::make_unique<Shader>(skyVert, skyFrag);
    m_depth     = std::make_unique<Shader>(depthVert, depthFrag);
    m_ssao      = std::make_unique<Shader>(fullscreenVert, ssaoFrag);
    m_bloomPre  = std::make_unique<Shader>(fullscreenVert, bloomPrefilterFrag);
    m_bloomDown = std::make_unique<Shader>(fullscreenVert, bloomDownFrag);
    m_bloomUp   = std::make_unique<Shader>(fullscreenVert, bloomUpFrag);
    m_composite = std::make_unique<Shader>(fullscreenVert, compositeFrag);
    m_fxaa      = std::make_unique<Shader>(fullscreenVert, fxaaFrag);
    buildGrid();
    buildAxes();
    // Fullscreen passes make their own vertices, but core GL still needs a VAO.
    glGenVertexArrays(1, &m_emptyVao);
    m_startTime = now();
}

SceneRenderer::~SceneRenderer() {
    if (m_gridVbo)  glDeleteBuffers(1, &m_gridVbo);
    if (m_gridVao)  glDeleteVertexArrays(1, &m_gridVao);
    if (m_axisVbo)  glDeleteBuffers(1, &m_axisVbo);
    if (m_axisVao)  glDeleteVertexArrays(1, &m_axisVao);
    if (m_emptyVao) glDeleteVertexArrays(1, &m_emptyVao);
    destroyTarget(m_hdr);
    destroyTarget(m_ao);
    destroyTarget(m_ldr);
    for (auto& b : m_bloom) destroyTarget(b);
}

void SceneRenderer::setGridSpacing(float studs) {
    studs = std::clamp(studs, 0.25f, 64.0f);
    if (std::fabs(studs - m_gridSpacing) < 1e-4f) return;
    m_gridSpacing = studs;
    if (m_gridVbo)  glDeleteBuffers(1, &m_gridVbo);
    if (m_gridVao)  glDeleteVertexArrays(1, &m_gridVao);
    m_gridVbo = m_gridVao = 0;
    buildGrid();
}

void SceneRenderer::buildGrid() {
    std::vector<glm::vec3> lines;
    // About 20 studs each way (more for big steps), at most 80 lines each way.
    const float s    = m_gridSpacing;
    const int   half = std::clamp((int)std::ceil(20.0f / s), 10, 80);
    const float ext  = half * s;
    for (int i = -half; i <= half; ++i) {
        // Skip the two centre lines; they are drawn separately as coloured axes.
        if (i == 0) continue;
        lines.push_back({i * s, kGridY, -ext});
        lines.push_back({i * s, kGridY,  ext});
        lines.push_back({-ext, kGridY, i * s});
        lines.push_back({ ext, kGridY, i * s});
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

void SceneRenderer::ensureTargets(int w, int h) {
    if (m_hdr.w == w && m_hdr.h == h && m_hdr.fbo) return;
    createTarget(m_hdr, w, h, hdrFormat(), true);
    createTarget(m_ao, w, h, GL_R8, false);
    createTarget(m_ldr, w, h, GL_RGBA8, false);
    int bw = w / 2, bh = h / 2;
    for (auto& b : m_bloom) {
        createTarget(b, bw, bh, hdrFormat(), false);
        bw = std::max(1, bw / 2);
        bh = std::max(1, bh / 2);
    }
}

void SceneRenderer::renderShadowPass(Scene& scene, const glm::mat4& lightSpace) {
    m_shadow.bindForWrite();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    // Cull front faces while filling the shadow map to reduce surface acne.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    m_depth->bind();
    m_depth->setMat4("uLightSpace", lightSpace);
    scene.forEach([&](SceneNode* node) {
        if (!node->mesh || !node->visible || !node->castShadow) return;
        for (SceneNode* p = node->parent; p; p = p->parent) if (!p->visible) return;   // inside something hidden (a backpack)
        if (node->kind != NodeKind::Part || node->transparency > 0.5f) return;
        m_depth->setMat4("uModel", node->worldMatrix());
        node->mesh->draw();
    });

    glDisable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
}

void SceneRenderer::render(Scene& scene, const Camera& camera, Framebuffer& target, bool showGrid) {
    GraphicsSettings& gs = GraphicsSettings::get();
    const Environment& env = scene.environment();
    glm::vec3 sunDir = env.sunDirection();

    int w = std::max(1, (int)(target.width()  * std::clamp(gs.renderScale, 0.25f, 2.0f)));
    int h = std::max(1, (int)(target.height() * std::clamp(gs.renderScale, 0.25f, 2.0f)));
    ensureTargets(w, h);
    if (m_shadowRes != gs.shadowRes) {
        m_shadow.init(gs.shadowRes);
        m_shadowRes = gs.shadowRes;
    }

    // --- Shadow map, centred on what the camera looks at and snapped to the
    //     texel grid so shadows don't shimmer as the camera moves ---
    const float extent   = std::max(5.0f, env.shadowDistance * 0.5f);
    const float sunDist  = extent * 2.0f + 30.0f;
    const float texel    = (2.0f * extent) / (float)m_shadow.size();
    glm::mat4 lightView0 = glm::lookAt(sunDir * sunDist, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    glm::vec3 lsCenter   = glm::vec3(lightView0 * glm::vec4(camera.pivot, 1.0f));
    lsCenter.x = std::floor(lsCenter.x / texel) * texel;
    lsCenter.y = std::floor(lsCenter.y / texel) * texel;
    glm::mat4 lightProj  = glm::ortho(lsCenter.x - extent, lsCenter.x + extent,
                                      lsCenter.y - extent, lsCenter.y + extent,
                                      -lsCenter.z - sunDist, -lsCenter.z + sunDist);
    glm::mat4 lightSpace = lightProj * lightView0;
    bool shadows = env.shadows && env.sunElevation > -5.0f;
    if (shadows) renderShadowPass(scene, lightSpace);

    // --- Main HDR pass ---
    bindTarget(m_hdr);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = camera.view();
    glm::mat4 proj = camera.projection();
    float time = (float)(now() - m_startTime);

    if (env.showSky) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        m_sky->bind();
        m_sky->setMat4("uInvViewProj", glm::inverse(proj * view));
        m_sky->setVec3("uZenith",  env.skyZenith);
        m_sky->setVec3("uHorizon", env.skyHorizon);
        m_sky->setVec3("uGround",  env.skyGround);
        m_sky->setFloat("uSkyBrightness", env.skyBrightness);
        m_sky->setVec3("uSunDir",  sunDir);
        m_sky->setVec3("uSunColor", env.sunColor);
        m_sky->setFloat("uSunIntensity", std::max(0.0f, env.sunIntensity));
        m_sky->setFloat("uSunSize", env.sunSize);
        m_sky->setVec3("uAmbient", env.ambientColor * env.ambientIntensity);
        m_sky->setBool("uClouds", env.clouds);
        m_sky->setFloat("uCloudCover", env.cloudCover);
        m_sky->setFloat("uCloudSpeed", env.cloudSpeed);
        m_sky->setVec3("uCloudColor", env.cloudColor);
        m_sky->setBool("uStars", env.stars);
        m_sky->setFloat("uTime", time);
        glBindVertexArray(m_emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    } else {
        glm::vec3 c = glm::pow(env.skyHorizon, glm::vec3(2.2f));
        glClearColor(c.r, c.g, c.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    if (showGrid) {
        m_grid->bind();
        m_grid->setMat4("uView", view);
        m_grid->setMat4("uProj", proj);
        m_grid->setVec3("uColor", {0.12f, 0.13f, 0.16f});
        glBindVertexArray(m_gridVao);
        glDrawArrays(GL_LINES, 0, m_gridVertexCount);
        glBindVertexArray(m_axisVao);
        m_grid->setVec3("uColor", {0.55f, 0.06f, 0.06f});
        glDrawArrays(GL_LINES, 0, 2);
        m_grid->setVec3("uColor", {0.06f, 0.2f, 0.62f});
        glDrawArrays(GL_LINES, 2, 2);
    }

    // --- Lit geometry ---
    m_lit->bind();
    m_lit->setMat4("uView", view);
    m_lit->setMat4("uProj", proj);
    m_lit->setVec3("uViewPos", camera.position());
    m_lit->setFloat("uTime", time);
    m_lit->setVec3("uSunDir", sunDir);
    m_lit->setVec3("uSunColor", env.sunColor);
    m_lit->setFloat("uSunIntensity", std::max(0.0f, env.sunIntensity));
    m_lit->setVec3("uAmbientSky", env.ambientColor);
    m_lit->setVec3("uAmbientGround", env.groundAmbient);
    m_lit->setFloat("uAmbientIntensity", env.ambientIntensity);
    m_lit->setFloat("uReflections", env.reflections);
    m_lit->setVec3("uZenith",  env.skyZenith);
    m_lit->setVec3("uHorizon", env.skyHorizon);
    m_lit->setVec3("uGround",  env.skyGround);
    m_lit->setBool("uFogEnabled", env.fogEnabled);
    m_lit->setVec3("uFogColor", env.fogColor);
    m_lit->setFloat("uFogDensity", env.fogDensity);
    m_lit->setFloat("uFogSunGlow", env.fogSunGlow);
    m_lit->setBool("uShadowsEnabled", shadows);
    m_lit->setInt("uShadowQuality", gs.shadowQuality);
    m_lit->setFloat("uShadowSoftness", env.shadowSoftness);
    m_lit->setFloat("uShadowStrength", env.shadowStrength);
    m_lit->setFloat("uShadowTexelWorld", texel);
    m_lit->setFloat("uShadowDepthRange", sunDist * 2.0f);
    m_lit->setMat4("uLightSpace", lightSpace);
    m_shadow.bindForRead(0);
    m_lit->setInt("uShadowMap", 0);

    // Collect point / spot lights; keep the ones closest to the camera.
    struct LightItem { glm::vec4 pos, col, dir; float dist; };
    std::vector<LightItem> lights;
    glm::vec3 camPos = camera.position();
    scene.forEach([&](SceneNode* n) {
        if (!n->isLight() || !n->enabled || n->brightness <= 0.0f) return;
        for (SceneNode* p = n; p; p = p->parent) if (!p->visible) return;
        glm::mat4 m = n->worldMatrix();
        glm::vec3 pos(m[3]);
        glm::vec3 dir = glm::normalize(glm::vec3(m * glm::vec4(0, -1, 0, 0)));
        float cosHalf = std::cos(glm::radians(std::clamp(n->spotAngle, 1.0f, 179.0f) * 0.5f));
        glm::vec3 col = glm::pow(n->color, glm::vec3(2.2f)) * n->brightness;
        lights.push_back({glm::vec4(pos, n->range), glm::vec4(col, n->lightType == LightType::Spot ? 1.0f : 0.0f),
                          glm::vec4(dir, cosHalf), glm::length(pos - camPos)});
    });
    std::sort(lights.begin(), lights.end(), [](auto& a, auto& b) { return a.dist < b.dist; });
    int count = std::min<int>((int)lights.size(), std::min(kMaxLights, gs.maxLights));
    glm::vec4 lp[kMaxLights], lc[kMaxLights], ld[kMaxLights];
    for (int i = 0; i < count; ++i) { lp[i] = lights[i].pos; lc[i] = lights[i].col; ld[i] = lights[i].dir; }
    m_lit->setInt("uLightCount", count);
    m_lit->setVec4Array("uLightPosRange", lp, count);
    m_lit->setVec4Array("uLightColor", lc, count);
    m_lit->setVec4Array("uLightDir", ld, count);

    drawGeometry(scene, camera, showGrid);
    glBindVertexArray(0);

    postProcess(scene, camera, target);
}

namespace {
// A cylinder from a to b (used for ropes, rods and springs).
glm::mat4 segment(const glm::vec3& a, const glm::vec3& b, float thickness) {
    glm::vec3 d = b - a;
    float len = glm::length(d);
    glm::vec3 y = len > 1e-6f ? d / len : glm::vec3(0, 1, 0);
    glm::vec3 x = std::abs(y.y) < 0.99f ? glm::normalize(glm::cross(glm::vec3(0, 1, 0), y)) : glm::vec3(1, 0, 0);
    glm::vec3 z = glm::cross(x, y);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x * thickness, 0);
    m[1] = glm::vec4(y * len, 0);
    m[2] = glm::vec4(z * thickness, 0);
    m[3] = glm::vec4((a + b) * 0.5f, 1);
    return m;
}
} // namespace

void SceneRenderer::drawConstraints(Scene& scene, bool editing) {
    auto cyl = MeshLibrary::get(PrimitiveType::Cylinder);
    auto ball = MeshLibrary::get(PrimitiveType::Sphere);
    m_lit->setBool("uSelected", false);
    m_lit->setFloat("uAlpha", 1.0f);
    auto draw = [&](const glm::mat4& m, const glm::vec3& color, Material mat, Mesh& mesh) {
        m_lit->setMat4("uModel", m);
        m_lit->setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(m))));
        m_lit->setVec3("uColor", color);
        m_lit->setInt("uMaterial", (int)mat);
        mesh.draw();
    };
    scene.forEach([&](SceneNode* n) {
        if (n->isAttachment() && editing) {
            glm::vec3 p(n->worldMatrix()[3]);
            glm::mat4 m = glm::translate(glm::mat4(1.0f), p) * glm::scale(glm::mat4(1.0f), glm::vec3(0.18f));
            draw(m, n->selected ? glm::vec3(1, 0.6f, 0.1f) : glm::vec3(0.2f, 1.0f, 0.3f), Material::Neon, *ball);
            return;
        }
        if (!n->isConstraint() || !n->visible || n->constraintType == ConstraintType::Weld) return;
        SceneNode* a0 = scene.findById(n->ref0);
        SceneNode* a1 = scene.findById(n->ref1);
        if (!a0 || !a1) return;
        glm::vec3 p0(a0->worldMatrix()[3]), p1(a1->worldMatrix()[3]);
        float th = std::max(0.02f, n->thickness);
        glm::vec3 col = n->selected ? glm::vec3(1, 0.6f, 0.1f) : n->color;
        switch (n->constraintType) {
            case ConstraintType::Rope: {
                // Slack ropes sag in the middle.
                float d = glm::length(p1 - p0);
                float sag = n->length > d ? std::sqrt(n->length * n->length - d * d) * 0.5f : 0.0f;
                const int segs = 12;
                glm::vec3 prev = p0;
                for (int i = 1; i <= segs; ++i) {
                    float t = (float)i / segs;
                    glm::vec3 p = glm::mix(p0, p1, t) - glm::vec3(0, sag * 4.0f * t * (1.0f - t), 0);
                    draw(segment(prev, p, th), col, Material::Plastic, *cyl);
                    prev = p;
                }
                break;
            }
            case ConstraintType::Rod:
                draw(segment(p0, p1, th), col, Material::Metal, *cyl);
                break;
            case ConstraintType::Spring: {
                // Zig-zag coil.
                glm::vec3 d = p1 - p0;
                float len = glm::length(d);
                if (len < 1e-4f) break;
                glm::vec3 y = d / len;
                glm::vec3 x = std::abs(y.y) < 0.99f ? glm::normalize(glm::cross(glm::vec3(0, 1, 0), y)) : glm::vec3(1, 0, 0);
                glm::vec3 z = glm::cross(x, y);
                const int turns = 8, per = 6;
                glm::vec3 prev = p0;
                for (int i = 1; i <= turns * per; ++i) {
                    float t = (float)i / (turns * per);
                    float ang = t * turns * 6.2831853f;
                    float r = (i == turns * per) ? 0.0f : 0.25f;
                    glm::vec3 p = p0 + d * t + (x * std::cos(ang) + z * std::sin(ang)) * r;
                    draw(segment(prev, p, th * 0.6f), col, Material::Metal, *cyl);
                    prev = p;
                }
                break;
            }
            case ConstraintType::Hinge:
                draw(segment(p0 - glm::vec3(a0->worldMatrix()[0]) * 0.0f, p1, th * 0.5f), col, Material::Metal, *cyl);
                break;
            default: break;
        }
    });
}

// Where a decal sits: the plane mesh (flat in X/Z, facing +Y) turned to lie on
// one side of its part's unit box, so the picture reads the right way round
// when you look at that side.
glm::mat4 SceneRenderer::decalMatrix(const SceneNode& d) {
    glm::vec3 R, U, N;
    switch (d.face) {
        case Face::Front:  R = {-1, 0, 0}; U = {0, 1, 0};  N = {0, 0, -1}; break;
        case Face::Back:   R = {1, 0, 0};  U = {0, 1, 0};  N = {0, 0, 1};  break;
        case Face::Right:  R = {0, 0, -1}; U = {0, 1, 0};  N = {1, 0, 0};  break;
        case Face::Left:   R = {0, 0, 1};  U = {0, 1, 0};  N = {-1, 0, 0}; break;
        case Face::Top:    R = {1, 0, 0};  U = {0, 0, -1}; N = {0, 1, 0};  break;
        default:           R = {1, 0, 0};  U = {0, 0, 1};  N = {0, -1, 0}; break;   // Bottom
    }
    glm::mat4 local(1.0f);
    local[0] = glm::vec4(R, 0);
    local[1] = glm::vec4(N, 0);
    local[2] = glm::vec4(U, 0);
    local[3] = glm::vec4(N * 0.5f, 1);
    return d.parent->worldMatrix() * local;
}

void SceneRenderer::drawGeometry(Scene& scene, const Camera& camera, bool editing) {
    struct Item { SceneNode* node; glm::mat4 model; float dist; };
    std::vector<Item> opaque, transparent, shielded, decals;
    glm::vec3 camPos = camera.position();

    // Walk manually so hidden models hide everything inside them, and so
    // everything inside a character with a ForceField gets the glowing shell.
    std::vector<std::pair<SceneNode*, bool>> stack{{scene.root(), false}};
    while (!stack.empty()) {
        auto [node, ff] = stack.back();
        stack.pop_back();
        if (!node->visible) continue;
        ff = ff || node->hasForceField();
        for (auto& c : node->children) stack.push_back({c.get(), ff});
        if (node->isDecal() && !node->texture.empty() && node->transparency < 0.99f &&
            node->parent && node->parent->kind == NodeKind::Part)
            decals.push_back({node, decalMatrix(*node), 0.0f});
        if (!node->mesh || node->kind != NodeKind::Part) continue;
        if (ff && !node->internal && node->transparency < 0.99f) shielded.push_back({node, node->worldMatrix(), 0.0f});
        float alpha = 1.0f - node->transparency;
        if (alpha <= 0.001f) continue;   // fully transparent — nothing to draw
        glm::mat4 model = node->worldMatrix();
        float dist = glm::length(glm::vec3(model[3]) - camPos);
        bool see = alpha < 0.999f || node->material == Material::Glass;
        (see ? transparent : opaque).push_back({node, model, dist});
    }
    // Transparent things are drawn last, far to near, so blending looks right.
    std::sort(transparent.begin(), transparent.end(), [](auto& a, auto& b) { return a.dist > b.dist; });

    auto draw = [&](const Item& it) {
        SceneNode* node = it.node;
        glm::mat3 nrm = glm::transpose(glm::inverse(glm::mat3(it.model)));
        m_lit->setMat4("uModel", it.model);
        m_lit->setMat3("uNormalMat", nrm);
        m_lit->setVec3("uColor", node->color);
        m_lit->setBool("uSelected", node->selected);
        m_lit->setInt("uMaterial", (int)node->material);
        m_lit->setFloat("uAlpha", 1.0f - node->transparency);
        node->mesh->draw();
    };

    glDisable(GL_BLEND);
    for (auto& it : opaque) draw(it);
    drawConstraints(scene, editing);

    // --- Particles (blood, oil, gibs, bolts, sparks, fire, smoke) ---
    const auto& parts = scene.particles().items();
    std::vector<const Particle*> fading;
    if (!parts.empty()) {
        auto cube = MeshLibrary::get(PrimitiveType::Cube);
        auto cyl  = MeshLibrary::get(PrimitiveType::Cylinder);
        m_lit->setBool("uSelected", false);
        auto drawParticle = [&](const Particle& p, float alpha) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), p.pos);
            m = glm::rotate(m, glm::radians(p.rot.z), {0, 0, 1});
            m = glm::rotate(m, glm::radians(p.rot.y), {0, 1, 0});
            m = glm::rotate(m, glm::radians(p.rot.x), {1, 0, 0});
            m = glm::scale(m, p.size);
            m_lit->setMat4("uModel", m);
            m_lit->setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(m))));
            m_lit->setVec3("uColor", p.color);
            int mat = (p.kind == Particle::Fire || p.kind == Particle::Spark) ? (int)Material::Neon
                    : p.glossy ? (int)Material::Metal : (int)Material::Plastic;
            m_lit->setInt("uMaterial", mat);
            m_lit->setFloat("uAlpha", alpha);
            (p.kind == Particle::Bolt || p.kind == Particle::Splat ? cyl : cube)->draw();
        };
        for (const Particle& p : parts) {
            bool fade = p.kind == Particle::Smoke || (p.kind == Particle::Splat && p.life < 1.0f);
            if (fade) fading.push_back(&p);
            else      drawParticle(p, 1.0f);
        }
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        for (const Particle* p : fading) {
            float a = p->kind == Particle::Smoke ? 0.55f * std::min(1.0f, p->life / p->maxLife * 2.0f)
                                                 : std::max(0.0f, p->life);
            drawParticle(*p, a);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    // --- Decals: pictures stuck flat on a side of their part ---
    if (!decals.empty()) {
        auto plane = MeshLibrary::get(PrimitiveType::Plane);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -4.0f);          // win the depth fight with the part's own side
        glDisable(GL_CULL_FACE);
        m_lit->setBool("uUseDecal", true);
        m_lit->setInt("uDecal", 5);
        for (auto& it : decals) {
            unsigned tex = Textures::get(it.node->texture);
            if (!tex) continue;
            bindTex(5, tex);
            m_lit->setMat4("uModel", it.model);
            m_lit->setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(it.model))));
            m_lit->setVec3("uColor", it.node->color);
            m_lit->setBool("uSelected", it.node->selected || it.node->parent->selected);
            m_lit->setInt("uMaterial", (int)Material::Plastic);
            m_lit->setFloat("uAlpha", 1.0f - it.node->transparency);
            plane->draw();
        }
        m_lit->setBool("uUseDecal", false);
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_CULL_FACE);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    for (auto& it : transparent) draw(it);

    // --- ForceFields: a glowing neon shell cycling through the rainbow ---
    if (!shielded.empty()) {
        float t = (float)(now() - m_startTime);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);          // additive: it glows (and blooms)
        m_lit->setBool("uSelected", false);
        m_lit->setInt("uMaterial", (int)Material::Neon);
        for (size_t i = 0; i < shielded.size(); ++i) {
            const Item& it = shielded[i];
            // Slightly bigger than the part, pulsing a little.
            float grow = 1.12f + 0.03f * std::sin(t * 5.0f + (float)i);
            glm::mat4 m = it.model * glm::scale(glm::mat4(1.0f), glm::vec3(grow));
            float hue = std::fmod(t * 0.35f + (float)i * 0.04f, 1.0f);
            glm::vec3 c = glm::clamp(glm::abs(glm::mod(hue * 6.0f + glm::vec3(0, 4, 2), 6.0f) - 3.0f) - 1.0f, 0.0f, 1.0f);
            m_lit->setMat4("uModel", m);
            m_lit->setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(m))));
            m_lit->setVec3("uColor", c);
            m_lit->setFloat("uAlpha", 0.15f + 0.06f * std::sin(t * 7.0f + (float)i * 0.7f));
            it.node->mesh->draw();
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glDepthMask(GL_TRUE);
}

void SceneRenderer::postProcess(Scene& scene, const Camera& camera, Framebuffer& target) {
    GraphicsSettings& gs = GraphicsSettings::get();
    const Environment& env = scene.environment();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(m_emptyVao);

    // --- Ambient occlusion (half resolution) ---
    bool useAO = gs.ssao && env.aoIntensity > 0.0f;
    if (useAO) {
        bindTarget(m_ao);
        m_ssao->bind();
        bindTex(0, m_hdr.depth);
        m_ssao->setInt("uDepth", 0);
        glm::mat4 proj = camera.projection();
        m_ssao->setMat4("uProj", proj);
        m_ssao->setMat4("uInvProj", glm::inverse(proj));
        m_ssao->setVec2("uTexel", glm::vec2(1.0f / m_hdr.w, 1.0f / m_hdr.h));
        m_ssao->setFloat("uRadius", 0.7f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // --- Bloom: bright-pass, then a blur pyramid down and back up ---
    bool useBloom = gs.bloom && env.bloomIntensity > 0.0f;
    if (useBloom) {
        bindTarget(m_bloom[0]);
        m_bloomPre->bind();
        bindTex(0, m_hdr.color);
        m_bloomPre->setInt("uSrc", 0);
        m_bloomPre->setFloat("uThreshold", env.bloomThreshold);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        m_bloomDown->bind();
        m_bloomDown->setInt("uSrc", 0);
        for (int i = 1; i < kBloomLevels; ++i) {
            bindTarget(m_bloom[i]);
            bindTex(0, m_bloom[i - 1].color);
            m_bloomDown->setVec2("uTexel", glm::vec2(1.0f / m_bloom[i - 1].w, 1.0f / m_bloom[i - 1].h));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        m_bloomUp->bind();
        m_bloomUp->setInt("uSrc", 0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        for (int i = kBloomLevels - 1; i > 0; --i) {
            bindTarget(m_bloom[i - 1]);
            bindTex(0, m_bloom[i].color);
            m_bloomUp->setVec2("uTexel", glm::vec2(1.0f / m_bloom[i].w, 1.0f / m_bloom[i].h));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // --- Composite: tone mapping + grading, then optional FXAA ---
    bool useFxaa = gs.fxaa;
    if (useFxaa) bindTarget(m_ldr);
    else         target.bind();
    m_composite->bind();
    bindTex(0, m_hdr.color);
    bindTex(1, m_bloom[0].color);
    bindTex(2, m_ao.color);
    m_composite->setInt("uHdr", 0);
    m_composite->setInt("uBloom", 1);
    m_composite->setInt("uAO", 2);
    m_composite->setBool("uUseBloom", useBloom);
    m_composite->setBool("uUseAO", useAO);
    m_composite->setBool("uPost", gs.postFx);
    m_composite->setBool("uLumaAlpha", useFxaa);
    m_composite->setVec2("uAOTexel", glm::vec2(1.0f / m_ao.w, 1.0f / m_ao.h));
    m_composite->setFloat("uExposure", env.exposure);
    m_composite->setFloat("uBloomIntensity", env.bloomIntensity);
    m_composite->setFloat("uAOIntensity", env.aoIntensity);
    m_composite->setFloat("uContrast", env.contrast);
    m_composite->setFloat("uSaturation", env.saturation);
    m_composite->setFloat("uVignette", env.vignette);
    m_composite->setVec3("uTint", env.tint);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (useFxaa) {
        target.bind();
        m_fxaa->bind();
        bindTex(0, m_ldr.color);
        m_fxaa->setInt("uSrc", 0);
        m_fxaa->setVec2("uTexel", glm::vec2(1.0f / m_ldr.w, 1.0f / m_ldr.h));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    target.unbind();
}
