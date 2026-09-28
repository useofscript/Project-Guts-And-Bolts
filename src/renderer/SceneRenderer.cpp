#include "SceneRenderer.h"
#include "Shader.h"
#include "Shaders.h"
#include "Mesh.h"
#include "Camera.h"
#include "Framebuffer.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../core/Settings.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
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

void createTarget(SceneRenderer::Target& t, int w, int h, GLenum fmt, bool withDepth) {
    destroyTarget(t);
    t.w = std::max(1, w);
    t.h = std::max(1, h);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);

    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    GLenum base = (fmt == GL_R8) ? GL_RED : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, t.w, t.h, 0, base, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);

    if (withDepth) {
        glGenTextures(1, &t.depth);
        glBindTexture(GL_TEXTURE_2D, t.depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, t.w, t.h, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
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
    createTarget(m_hdr, w, h, GL_RGBA16F, true);
    createTarget(m_ao, w / 2, h / 2, GL_R8, false);
    createTarget(m_ldr, w, h, GL_RGBA8, false);
    int bw = w / 2, bh = h / 2;
    for (auto& b : m_bloom) {
        createTarget(b, bw, bh, GL_RGBA16F, false);
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

    drawGeometry(scene, camera);
    glBindVertexArray(0);

    postProcess(scene, camera, target);
}

void SceneRenderer::drawGeometry(Scene& scene, const Camera& camera) {
    struct Item { SceneNode* node; glm::mat4 model; float dist; };
    std::vector<Item> opaque, transparent;
    glm::vec3 camPos = camera.position();

    // Walk manually so hidden models hide everything inside them.
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* node = stack.back();
        stack.pop_back();
        if (!node->visible) continue;
        for (auto& c : node->children) stack.push_back(c.get());
        if (!node->mesh || node->kind != NodeKind::Part) continue;
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (auto& it : transparent) draw(it);
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
