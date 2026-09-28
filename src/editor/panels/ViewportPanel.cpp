#include "ViewportPanel.h"
#include "../EditorState.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../scene/Physics.h"
#include "../../game/GameSession.h"
#include "../../game/Hud.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

ViewportPanel::ViewportPanel(GLFWwindow* window, Scene* scene, EditorState* state)
    : m_window(window), m_scene(scene), m_state(state) {
    resetCamera();
}

ViewportPanel::~ViewportPanel() = default;

void ViewportPanel::resetCamera() {
    m_camera.yaw      = 45.0f;
    m_camera.pitch    = 25.0f;
    m_camera.distance = 12.0f;
    m_camera.pivot    = {0, 1, 0};
}

float ViewportPanel::cameraYaw() const { return m_camera.yaw; }

void ViewportPanel::frameOn(const glm::vec3& target) { m_camera.pivot = target; }

bool ViewportPanel::gizmoInUse() const { return ImGuizmo::IsUsing(); }

void ViewportPanel::focusSelected() {
    if (SceneNode* sel = m_scene->selected())
        m_camera.pivot = glm::vec3(sel->worldMatrix()[3]);
}

void ViewportPanel::handleInput() {
    if (!m_hovered) return;
    ImGuiIO& io = ImGui::GetIO();

    bool playing = m_session != nullptr;
    bool orbit = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                 (playing && ImGui::IsMouseDown(ImGuiMouseButton_Right));

    if (orbit) {
        ImVec2 d = io.MouseDelta;
        if (io.KeyShift && !playing) m_camera.pan(d.x, d.y);
        else                         m_camera.orbit(d.x, d.y);
    }
    if (io.MouseWheel != 0.0f)
        m_camera.zoom(io.MouseWheel);

    if (!playing && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
        focusSelected();
}

void ViewportPanel::mouseRay(const glm::vec2& mouse, const glm::vec2& imgMin, const glm::vec2& imgSize,
                             const glm::mat4& view, const glm::mat4& proj,
                             glm::vec3& ro, glm::vec3& rd) const {
    // Mouse position in the image -> normalised device coords -> world ray.
    float nx = (mouse.x - imgMin.x) / imgSize.x * 2.0f - 1.0f;
    float ny = 1.0f - (mouse.y - imgMin.y) / imgSize.y * 2.0f;
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 pNear = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 pFar  = invVP * glm::vec4(nx, ny,  1.0f, 1.0f);
    pNear /= pNear.w;
    pFar  /= pFar.w;
    ro = glm::vec3(pNear);
    rd = glm::normalize(glm::vec3(pFar - pNear));
}

void ViewportPanel::drawGizmo(const glm::mat4& view, const glm::mat4& proj,
                              const glm::vec2& imgMin, const glm::vec2& imgSize) {
    SceneNode* sel = m_scene->selected();
    if (!sel || sel == m_scene->root() || sel->isScript() || m_state->tool == GizmoTool::Select)
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

void ViewportPanel::render(float dt) {
    (void)dt;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (m_wantFocus) { ImGui::SetNextWindowFocus(); m_wantFocus = false; }
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
        bool playing = m_session != nullptr;
        m_renderer.render(*m_scene, m_camera, m_fbo, !playing);

        ImVec2 imgPos = ImGui::GetCursorScreenPos();
        // Flip V so the framebuffer texture is the right way up in ImGui.
        ImGui::Image((ImTextureID)(intptr_t)m_fbo.colorTexture(),
                     avail, ImVec2(0, 1), ImVec2(1, 0));

        glm::mat4 view = m_camera.view();
        glm::mat4 proj = m_camera.projection();
        glm::vec2 imgMin{imgPos.x, imgPos.y};
        glm::vec2 imgSize{avail.x, avail.y};
        ImVec2 imgMax(imgPos.x + avail.x, imgPos.y + avail.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        if (playing) {
            // Clicks go to the game (part.Clicked / MouseButton1), not the editor.
            if (m_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 m = ImGui::GetMousePos();
                glm::vec3 ro, rd;
                mouseRay({m.x, m.y}, imgMin, imgSize, view, proj, ro, rd);
                const SceneNode* character = m_scene->player() ? m_scene->player()->root() : nullptr;
                SceneNode* hit = Physics::raycast(*m_scene, ro, rd, nullptr, character);
                m_session->click(hit ? hit->id : 0);
            }
            Hud::draw(dl, imgPos, imgMax, *m_scene, m_session->gui());
            // Green frame = the game is running.
            dl->AddRect(imgPos, imgMax, IM_COL32(60, 200, 90, 255), 0.0f, 0, 3.0f);
            const char* tip = "PLAYING  -  WASD move, Space jump, right-drag camera, F5/Esc stop";
            dl->AddText(ImVec2(imgPos.x + 12, imgMax.y - ImGui::GetFontSize() - 10),
                        IM_COL32(255, 255, 255, 170), tip);
        } else {
            drawGizmo(view, proj, imgMin, imgSize);

            // Left-click to pick — but not while interacting with the gizmo.
            bool overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
            if (m_hovered && !overGizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 m = ImGui::GetMousePos();
                glm::vec3 ro, rd;
                mouseRay({m.x, m.y}, imgMin, imgSize, view, proj, ro, rd);
                if (SceneNode* hit = Physics::raycast(*m_scene, ro, rd)) m_scene->select(hit);
                else                                                      m_scene->deselect();
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}
