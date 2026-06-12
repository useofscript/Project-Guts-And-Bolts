#include "ViewportPanel.h"
#include "../EditorState.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../renderer/Shader.h"
#include "../../renderer/Mesh.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <limits>

namespace {

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

uniform bool  uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uSunDir);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.3;
    vec3  sun  = uSunColor * uSunIntensity;

    vec3 base = uColor * (uAmbient + sun * diff) + sun * spec;

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

    FragColor = vec4(base, 1.0);
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

// Canonical local-space bounds of each primitive, used as a pick proxy. All
// primitives fit in the unit cube; the plane is given a little thickness so a
// near-horizontal ray can still hit it.
void localBounds(PrimitiveType type, glm::vec3& bmin, glm::vec3& bmax) {
    if (type == PrimitiveType::Plane) {
        bmin = {-0.5f, -0.02f, -0.5f};
        bmax = { 0.5f,  0.02f,  0.5f};
    } else {
        bmin = {-0.5f, -0.5f, -0.5f};
        bmax = { 0.5f,  0.5f,  0.5f};
    }
}

// Slab-method ray/AABB. Returns the nearest non-negative hit distance.
bool rayAABB(const glm::vec3& ro, const glm::vec3& rd,
             const glm::vec3& bmin, const glm::vec3& bmax, float& tHit) {
    float t0 = -std::numeric_limits<float>::max();
    float t1 =  std::numeric_limits<float>::max();
    for (int i = 0; i < 3; ++i) {
        if (std::abs(rd[i]) < 1e-8f) {
            if (ro[i] < bmin[i] || ro[i] > bmax[i]) return false;
        } else {
            float inv = 1.0f / rd[i];
            float ta = (bmin[i] - ro[i]) * inv;
            float tb = (bmax[i] - ro[i]) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
    }
    tHit = (t0 >= 0.0f) ? t0 : t1;
    return tHit >= 0.0f;
}

} // namespace

ViewportPanel::ViewportPanel(GLFWwindow* window, Scene* scene, EditorState* state)
    : m_window(window), m_scene(scene), m_state(state) {
    m_shader     = std::make_unique<Shader>(kLitVert,  kLitFrag);
    m_gridShader = std::make_unique<Shader>(kGridVert, kGridFrag);
    m_skyShader  = std::make_unique<Shader>(kSkyVert,  kSkyFrag);
    buildGrid();
    buildAxes();
    buildSky();
}

ViewportPanel::~ViewportPanel() {
    if (m_gridVbo) glDeleteBuffers(1, &m_gridVbo);
    if (m_gridVao) glDeleteVertexArrays(1, &m_gridVao);
    if (m_axisVbo) glDeleteBuffers(1, &m_axisVbo);
    if (m_axisVao) glDeleteVertexArrays(1, &m_axisVao);
    if (m_skyVao)  glDeleteVertexArrays(1, &m_skyVao);
}

void ViewportPanel::buildGrid() {
    std::vector<glm::vec3> lines;
    const int   half = 10;
    const float ext  = (float)half;
    for (int i = -half; i <= half; ++i) {
        // Skip the two centre lines; they are drawn separately as coloured axes.
        if (i == 0) continue;
        lines.push_back({(float)i, 0.0f, -ext});
        lines.push_back({(float)i, 0.0f,  ext});
        lines.push_back({-ext, 0.0f, (float)i});
        lines.push_back({ ext, 0.0f, (float)i});
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

void ViewportPanel::buildAxes() {
    // Two coloured centre lines (X and Z) in their own VAO, drawn as two ranges.
    const glm::vec3 verts[] = {
        {-10, 0, 0}, {10, 0, 0},   // X axis  (range 0..2)
        {0, 0, -10}, {0, 0, 10},   // Z axis  (range 2..4)
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

void ViewportPanel::buildSky() {
    // The sky shader synthesises its own vertices, but core-profile draws still
    // require a bound (empty) VAO.
    glGenVertexArrays(1, &m_skyVao);
}

void ViewportPanel::resetCamera() {
    m_camera.yaw      = 45.0f;
    m_camera.pitch    = 25.0f;
    m_camera.distance = 8.0f;
    m_camera.pivot    = {0, 0, 0};
}

void ViewportPanel::focusSelected() {
    if (SceneNode* sel = m_scene->selected())
        m_camera.pivot = glm::vec3(sel->worldMatrix()[3]);
}

void ViewportPanel::handleInput() {
    if (!m_hovered) return;
    ImGuiIO& io = ImGui::GetIO();

    bool mmb   = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    bool shift = io.KeyShift;

    if (mmb) {
        ImVec2 d = io.MouseDelta;
        if (shift) m_camera.pan(d.x, d.y);
        else       m_camera.orbit(d.x, d.y);
    }
    if (io.MouseWheel != 0.0f)
        m_camera.zoom(io.MouseWheel);

    if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        focusSelected();
}

void ViewportPanel::pickAt(const glm::vec2& mouse, const glm::vec2& imgMin,
                           const glm::vec2& imgSize, const glm::mat4& view,
                           const glm::mat4& proj) {
    if (imgSize.x <= 0 || imgSize.y <= 0) return;

    // Mouse position in the image -> normalised device coords.
    float nx = (mouse.x - imgMin.x) / imgSize.x * 2.0f - 1.0f;
    float ny = 1.0f - (mouse.y - imgMin.y) / imgSize.y * 2.0f;

    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 pNear = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 pFar  = invVP * glm::vec4(nx, ny,  1.0f, 1.0f);
    pNear /= pNear.w;
    pFar  /= pFar.w;

    glm::vec3 ro  = glm::vec3(pNear);
    glm::vec3 rd  = glm::normalize(glm::vec3(pFar - pNear));

    SceneNode* best     = nullptr;
    float      bestDist = std::numeric_limits<float>::max();

    m_scene->forEach([&](SceneNode* node) {
        if (!node->mesh || !node->visible) return;

        glm::mat4 world = node->worldMatrix();
        glm::mat4 inv   = glm::inverse(world);
        glm::vec3 lro   = glm::vec3(inv * glm::vec4(ro, 1.0f));
        glm::vec3 lrd   = glm::vec3(inv * glm::vec4(rd, 0.0f));

        glm::vec3 bmin, bmax;
        localBounds(node->primitiveType, bmin, bmax);

        float tLocal;
        if (rayAABB(lro, lrd, bmin, bmax, tLocal)) {
            // Convert the local hit into a world-space distance so objects of
            // different scales are compared fairly.
            glm::vec3 worldHit = glm::vec3(world * glm::vec4(lro + lrd * tLocal, 1.0f));
            float worldDist = glm::dot(worldHit - ro, rd);
            if (worldDist > 0.0f && worldDist < bestDist) {
                bestDist = worldDist;
                best     = node;
            }
        }
    });

    if (best) m_scene->select(best);
    else      m_scene->deselect();
}

void ViewportPanel::drawGizmo(const glm::mat4& view, const glm::mat4& proj,
                              const glm::vec2& imgMin, const glm::vec2& imgSize) {
    SceneNode* sel = m_scene->selected();
    if (!sel || sel == m_scene->root() || m_state->tool == GizmoTool::Select)
        return;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSize.x, imgSize.y);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (m_state->tool == GizmoTool::Rotate)     op = ImGuizmo::ROTATE;
    else if (m_state->tool == GizmoTool::Scale) op = ImGuizmo::SCALE;
    ImGuizmo::MODE mode = m_state->gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    float snap[3] = {0, 0, 0};
    if (m_state->snapEnabled) {
        float s = (op == ImGuizmo::TRANSLATE) ? m_state->snapTranslate
                : (op == ImGuizmo::ROTATE)    ? m_state->snapRotate
                                              : m_state->snapScale;
        snap[0] = snap[1] = snap[2] = s;
    }

    glm::mat4 world = sel->worldMatrix();
    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, mode,
                             glm::value_ptr(world), nullptr,
                             m_state->snapEnabled ? snap : nullptr)) {
        // Convert the manipulated world matrix back into a local transform.
        glm::mat4 local = world;
        if (sel->parent)
            local = glm::inverse(sel->parent->worldMatrix()) * world;

        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local), t, r, s);

        glm::vec3 newRot{r[0], r[1], r[2]};
        // Accumulate the rotation delta to avoid Euler-angle flips at +/-90 deg.
        glm::vec3 deltaRot = newRot - sel->transform.rotation;
        sel->transform.position = {t[0], t[1], t[2]};
        sel->transform.rotation += deltaRot;
        sel->transform.scale    = {s[0], s[1], s[2]};
    }
}

void ViewportPanel::drawScene() {
    const Environment& env = m_scene->environment();
    glm::vec3 sunDir = env.sunDirection();

    m_fbo.bind();
    glEnable(GL_DEPTH_TEST);
    glClearColor(env.skyHorizon.r, env.skyHorizon.g, env.skyHorizon.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = m_camera.view();
    glm::mat4 proj = m_camera.projection();

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

    // --- Grid + axes ---
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

    // --- Lit scene geometry ---
    m_shader->bind();
    m_shader->setMat4("uView", view);
    m_shader->setMat4("uProj", proj);
    m_shader->setVec3("uSunDir", sunDir);
    m_shader->setVec3("uSunColor", env.sunColor);
    m_shader->setFloat("uSunIntensity", env.sunIntensity);
    m_shader->setVec3("uAmbient", env.ambientColor * env.ambientIntensity);
    m_shader->setVec3("uViewPos", m_camera.position());
    m_shader->setBool("uFogEnabled", env.fogEnabled);
    m_shader->setVec3("uFogColor", env.fogColor);
    m_shader->setFloat("uFogDensity", env.fogDensity);

    m_scene->forEach([&](SceneNode* node) {
        if (!node->mesh || !node->visible) return;
        glm::mat4 model = node->worldMatrix();
        glm::mat3 nrm   = glm::transpose(glm::inverse(glm::mat3(model)));
        m_shader->setMat4("uModel", model);
        m_shader->setMat3("uNormalMat", nrm);
        m_shader->setVec3("uColor", node->color);
        m_shader->setBool("uSelected", node->selected);
        node->mesh->draw();
    });

    glBindVertexArray(0);
    m_fbo.unbind();
}

void ViewportPanel::render(float dt) {
    (void)dt;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");

    m_hovered = ImGui::IsWindowHovered();
    handleInput();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x, h = (int)avail.y;
    if (w > 0 && h > 0) {
        if (w != m_viewW || h != m_viewH) {
            m_viewW = w; m_viewH = h;
            m_fbo.resize(w, h);
            m_camera.resize(w, h);
        }
        drawScene();

        ImVec2 imgPos = ImGui::GetCursorScreenPos();
        // Flip V so the framebuffer texture is the right way up in ImGui.
        ImGui::Image((ImTextureID)(intptr_t)m_fbo.colorTexture(),
                     avail, ImVec2(0, 1), ImVec2(1, 0));

        glm::mat4 view = m_camera.view();
        glm::mat4 proj = m_camera.projection();
        glm::vec2 imgMin{imgPos.x, imgPos.y};
        glm::vec2 imgSize{avail.x, avail.y};

        drawGizmo(view, proj, imgMin, imgSize);

        // Left-click to pick — but not while interacting with the gizmo.
        bool overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        if (m_hovered && !overGizmo &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImVec2 m = ImGui::GetMousePos();
            pickAt({m.x, m.y}, imgMin, imgSize, view, proj);
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}
