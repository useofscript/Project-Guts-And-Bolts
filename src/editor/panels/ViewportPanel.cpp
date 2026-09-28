#include "ViewportPanel.h"
#include <vector>
#include "../EditorState.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../scene/Physics.h"
#include "../../scene/EditMesh.h"
#include "../../game/GameSession.h"
#include "../../game/Hud.h"
#include "../../core/Audio.h"
#include "../TeamCreate.h"
#include "../Icons.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstdio>

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
    SceneNode* sel = m_scene->selected();
    if (!sel) return;
    m_camera.pivot = glm::vec3(sel->worldMatrix()[3]);
    if (sel->isPart()) {   // back off far enough to see all of it
        AABB b = Physics::worldBounds(sel);
        m_camera.pivot = (b.min + b.max) * 0.5f;
        m_camera.distance = std::clamp(glm::length(b.max - b.min) * 1.6f, 3.0f, 200.0f);
    }
}

void ViewportPanel::handleInput(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    bool playing = m_session != nullptr && !m_session->runOnly();   // Run: fly around like when editing
    bool focused = ImGui::IsWindowFocused();

    if (playing) {
        if (!m_hovered) return;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            m_camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
        if (io.MouseWheel != 0.0f) m_camera.zoom(io.MouseWheel);
        return;
    }

    // Roblox Studio camera: right-drag looks around, middle-drag pans, the
    // wheel zooms, and WASD / Q E fly (hold Shift to go faster).
    static bool looking = false;
    if (m_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) looking = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) looking = false;
    if (looking) m_camera.look(io.MouseDelta.x, io.MouseDelta.y);
    if (m_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        if (io.KeyShift) m_camera.orbit(io.MouseDelta.x, io.MouseDelta.y);   // Shift+middle: orbit
        else             m_camera.pan(io.MouseDelta.x, io.MouseDelta.y);
    }
    if (m_hovered && io.MouseWheel != 0.0f) m_camera.zoom(io.MouseWheel);

    // Modeling mode: Home (or numpad .) looks at the picked points, like Blender.
    if (m_state->mode == StudioMode::Modeling && (focused || m_hovered) && !io.WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Home, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false))) {
        SceneNode* n = m_scene->findById(m_state->modeling.node);
        if (n && n->editMesh) {
            glm::mat4 M = n->worldMatrix();
            glm::vec3 c(0.0f);
            int count = 0;
            const auto& sel = m_state->modeling.sel;
            for (size_t i = 0; i < n->editMesh->verts.size(); ++i)
                if (i < sel.size() && sel[i]) { c += glm::vec3(M * glm::vec4(n->editMesh->verts[i], 1.0f)); ++count; }
            m_camera.pivot = count ? c / (float)count : glm::vec3(M[3]);
        }
    }
    // In Modeling mode the letter keys are tools (E extrude, ...), so fly only while right-dragging.
    bool keysFly = m_state->mode == StudioMode::Modeling ? looking : (focused || looking);
    if (keysFly && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt) {
        float speed = (io.KeyShift ? 60.0f : 18.0f) * dt;
        float f = 0, r = 0, u = 0;
        if (ImGui::IsKeyDown(ImGuiKey_W)) f += speed;
        if (ImGui::IsKeyDown(ImGuiKey_S)) f -= speed;
        if (ImGui::IsKeyDown(ImGuiKey_D)) r += speed;
        if (ImGui::IsKeyDown(ImGuiKey_A)) r -= speed;
        if (ImGui::IsKeyDown(ImGuiKey_E)) u += speed;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) u -= speed;
        if (f != 0 || r != 0 || u != 0) m_camera.fly(f, r, u);
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) focusSelected();
    }
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

    // Models are handled around their middle (their "pivot"), like Roblox,
    // rather than their origin, which may be far away from their parts.
    glm::mat4 base = sel->worldMatrix();
    glm::vec3 localPivot(0.0f);
    if (sel->kind == NodeKind::Model) {
        glm::vec3 lo(1e9f), hi(-1e9f);
        std::vector<SceneNode*> stack{sel};
        while (!stack.empty()) {
            SceneNode* n = stack.back(); stack.pop_back();
            if (n->isPart()) { glm::vec3 p(n->worldMatrix()[3]); lo = glm::min(lo, p); hi = glm::max(hi, p); }
            for (auto& c : n->children) stack.push_back(c.get());
        }
        if (lo.x <= hi.x) localPivot = glm::vec3(glm::inverse(base) * glm::vec4((lo + hi) * 0.5f, 1.0f));
    }
    glm::mat4 world = base * glm::translate(glm::mat4(1.0f), localPivot);
    const glm::vec3 pivotBefore(world[3]);
    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, mode,
                             glm::value_ptr(world), nullptr,
                             m_state->snapEnabled ? snap : nullptr)) {
        glm::vec3 movedPivot = glm::vec3(world[3]) - pivotBefore;
        world = world * glm::translate(glm::mat4(1.0f), -localPivot);
        // Convert the manipulated world matrix back into a local transform.
        glm::mat4 local = world;
        if (sel->parent)
            local = glm::inverse(sel->parent->worldMatrix()) * world;

        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local), t, r, s);

        glm::vec3 newRot{r[0], r[1], r[2]};
        // Accumulate the rotation delta to avoid Euler-angle flips at +/-90 deg.
        glm::vec3 deltaRot = newRot - sel->transform.rotation;
        // Moving several things: everything else selected slides along too.
        glm::vec3 moved = movedPivot;
        if (m_state->tool == GizmoTool::Translate && glm::length(moved) > 0.0f) {
            for (SceneNode* o : m_scene->selectionRoots()) {
                if (o == sel || m_scene->isProtected(o) || m_scene->isCharacterPart(o)) continue;
                glm::vec3 d = moved;
                if (o->parent) d = glm::vec3(glm::inverse(o->parent->worldMatrix()) * glm::vec4(moved, 0.0f));
                o->transform.position += d;
            }
        }
        sel->transform.position = {t[0], t[1], t[2]};
        sel->transform.rotation += deltaRot;
        sel->transform.scale    = {s[0], s[1], s[2]};
    }
}

void ViewportPanel::render(float dt) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (m_wantFocus) { ImGui::SetNextWindowFocus(); m_wantFocus = false; }
    ImGui::Begin("Viewport");

    m_hovered = ImGui::IsWindowHovered();
    handleInput(dt);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x, h = (int)avail.y;
    if (w > 0 && h > 0) {
        if (w != m_viewW || h != m_viewH) {
            m_viewW = w; m_viewH = h;
            m_fbo.resize(w, h);
            m_camera.resize(w, h);
        }
        bool playing = m_session != nullptr;
        m_renderer.render(*m_scene, m_camera, m_fbo, !playing || m_session->runOnly());
        Audio::setListener(m_camera.position(), glm::normalize(m_camera.pivot - m_camera.position()));

        ImVec2 imgPos = ImGui::GetCursorScreenPos();
        // Flip V so the framebuffer texture is the right way up in ImGui.
        ImGui::Image((ImTextureID)(intptr_t)m_fbo.colorTexture(),
                     avail, ImVec2(0, 1), ImVec2(1, 0));
        if (drawModeMenu(imgPos)) m_hovered = false;   // clicks on the menu aren't clicks in the world

        glm::mat4 view = m_camera.view();
        glm::mat4 proj = m_camera.projection();
        glm::vec2 imgMin{imgPos.x, imgPos.y};
        glm::vec2 imgSize{avail.x, avail.y};
        ImVec2 imgMax(imgPos.x + avail.x, imgPos.y + avail.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        if (playing && m_session->runOnly()) {
            // Simulate: click things to look at them in Properties, and drag
            // them with the gizmo while the world keeps running.
            drawGizmo(view, proj, imgMin, imgSize);
            bool overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
            if (ImGuizmo::IsUsing())
                for (SceneNode* n : m_scene->selectionRoots()) { n->velocity = glm::vec3(0.0f); n->angularVelocity = glm::vec3(0.0f); n->sleepTime = 0; }
            if (m_hovered && !overGizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 m = ImGui::GetMousePos();
                glm::vec3 ro, rd;
                mouseRay({m.x, m.y}, imgMin, imgSize, view, proj, ro, rd);
                SceneNode* hit = Physics::raycast(*m_scene, ro, rd);
                if (hit && !ImGui::GetIO().KeyAlt && !m_scene->isCharacterPart(hit)) {
                    SceneNode* top = hit;
                    for (SceneNode* p = hit->parent; p && p != m_scene->root(); p = p->parent)
                        if (p->kind == NodeKind::Model) top = p;
                    hit = top;
                }
                if (hit) m_scene->select(hit); else m_scene->deselect();
            }
            Hud::draw(dl, imgPos, imgMax, *m_scene, m_session->gui(), 0.0f, false);
            dl->AddRect(imgPos, imgMax, IM_COL32(60, 170, 230, 255), 0.0f, 0, 3.0f);   // blue frame = simulating
            const char* tip = m_state->simPaused
                ? "SIMULATE (paused)  -  F6 resume, F7 step one frame, Shift+F5 stop"
                : "SIMULATE  -  click things to inspect / drag them, right-drag + WASD fly, F6 pause, Shift+F5 stop";
            dl->AddText(ImVec2(imgPos.x + 12, imgMax.y - ImGui::GetFontSize() - 10), IM_COL32(255, 255, 255, 190), tip);
        } else if (playing) {
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
            const char* tip = m_state->simPaused ? "PLAY (paused)  -  F6 resume, F7 step, Shift+F5 stop"
                                                 : "PLAYING  -  WASD move, Space jump, right-drag camera, F6 pause, F5/Esc stop";
            dl->AddText(ImVec2(imgPos.x + 12, imgMax.y - ImGui::GetFontSize() - 10),
                        IM_COL32(255, 255, 255, 170), tip);
        } else if (m_state->mode == StudioMode::Modeling) {
            modelingView(view, proj, imgMin, imgSize);
            dl->AddRect(imgPos, imgMax, IM_COL32(255, 150, 40, 255), 0.0f, 0, 3.0f);   // orange frame = modeling
            static const char* kPickNames[] = {"vertices", "edges", "faces"};
            char tip[256];
            std::snprintf(tip, sizeof(tip),
                          "MODELING (picking %s)  -  1/2/3 vertex/edge/face, click / Shift+click / drag a box, A all, "
                          "E extrude, I inset, X delete, M merge, F fill, Tab done",
                          kPickNames[std::clamp(m_state->modeling.selectMode, 0, 2)]);
            dl->AddText(ImVec2(imgPos.x + 12, imgMax.y - ImGui::GetFontSize() - 10), IM_COL32(255, 255, 255, 200), tip);
        } else {
            drawGizmo(view, proj, imgMin, imgSize);

            // Team Create: a coloured box around what each other person has selected.
            if (m_team && m_team->active()) {
                for (const auto& mem : m_team->members()) {
                    if (mem.id == m_team->myId() || !mem.selected) continue;
                    SceneNode* n = m_scene->findById(mem.selected);
                    if (!n || !n->isPart()) continue;
                    AABB b = Physics::worldBounds(n);
                    ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
                    bool behind = false;
                    for (int c = 0; c < 8; ++c) {
                        glm::vec3 p{(c & 1) ? b.max.x : b.min.x, (c & 2) ? b.max.y : b.min.y, (c & 4) ? b.max.z : b.min.z};
                        glm::vec4 clip = proj * view * glm::vec4(p, 1.0f);
                        if (clip.w <= 0.05f) { behind = true; break; }
                        float sx = imgMin.x + (clip.x / clip.w * 0.5f + 0.5f) * imgSize.x;
                        float sy = imgMin.y + (0.5f - clip.y / clip.w * 0.5f) * imgSize.y;
                        lo = ImVec2(std::min(lo.x, sx), std::min(lo.y, sy));
                        hi = ImVec2(std::max(hi.x, sx), std::max(hi.y, sy));
                    }
                    if (behind) continue;
                    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(mem.color.r, mem.color.g, mem.color.b, 1));
                    dl->AddRect(lo, hi, col, 3.0f, 0, 2.0f);
                    ImVec2 ts = ImGui::CalcTextSize(mem.name.c_str());
                    dl->AddRectFilled(ImVec2(lo.x, lo.y - ts.y - 6), ImVec2(lo.x + ts.x + 10, lo.y), col, 3.0f);
                    dl->AddText(ImVec2(lo.x + 5, lo.y - ts.y - 3), IM_COL32(20, 20, 25, 255), mem.name.c_str());
                }
            }

            // Light icons (lights have no shape, so draw a marker you can click).
            SceneNode* iconHit = nullptr;
            ImVec2 mouse = ImGui::GetMousePos();
            glm::mat4 vp = proj * view;
            m_scene->forEach([&](SceneNode* n) {
                if (!n->isLight()) return;
                glm::vec4 c = vp * glm::vec4(glm::vec3(n->worldMatrix()[3]), 1.0f);
                if (c.w <= 0.05f) return;
                glm::vec2 ndc = glm::vec2(c) / c.w;
                if (std::abs(ndc.x) > 1.05f || std::abs(ndc.y) > 1.05f) return;
                ImVec2 sp(imgMin.x + (ndc.x * 0.5f + 0.5f) * imgSize.x,
                          imgMin.y + (0.5f - ndc.y * 0.5f) * imgSize.y);
                glm::vec3 col = n->enabled ? n->color : glm::vec3(0.4f);
                ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(col.r, col.g, col.b, 0.9f));
                dl->AddCircleFilled(sp, 8.0f, fill);
                dl->AddCircle(sp, 10.0f, n->selected ? IM_COL32(255, 150, 40, 255) : IM_COL32(20, 20, 20, 200), 0, 2.0f);
                if (n->lightType == LightType::Spot)
                    dl->AddLine(sp, ImVec2(sp.x, sp.y + 16), IM_COL32(20, 20, 20, 200), 2.0f);
                float dx = mouse.x - sp.x, dy = mouse.y - sp.y;
                if (dx * dx + dy * dy < 12.0f * 12.0f) iconHit = n;
            });

            // Connect tool: click part A, then part B.
            if (m_state->connectTool >= 0) {
                static const char* names[] = {"rope", "rod", "spring", "weld", "hinge", "motor"};
                const char* what = names[std::clamp(m_state->connectTool, 0, 5)];
                char hint[160];
                if (!m_state->connectFirst)
                    std::snprintf(hint, sizeof(hint), "Adding a %s: click the FIRST part   (Esc to cancel)", what);
                else
                    std::snprintf(hint, sizeof(hint), "Adding a %s: now click the SECOND part   (Esc to cancel)", what);
                ImVec2 ts = ImGui::CalcTextSize(hint);
                ImVec2 hp(imgMin.x + (imgSize.x - ts.x) * 0.5f, imgMin.y + 14);
                dl->AddRectFilled(ImVec2(hp.x - 10, hp.y - 6), ImVec2(hp.x + ts.x + 10, hp.y + ts.y + 6), IM_COL32(20, 90, 200, 230), 6);
                dl->AddText(hp, IM_COL32(255, 255, 255, 255), hint);
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_state->connectTool = -1; m_state->connectFirst = 0; }
                if (m_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    ImVec2 m = ImGui::GetMousePos();
                    glm::vec3 ro, rd;
                    mouseRay({m.x, m.y}, imgMin, imgSize, view, proj, ro, rd);
                    float dist = 0.0f;
                    if (SceneNode* hit = Physics::raycast(*m_scene, ro, rd, &dist)) {
                        glm::vec3 point = ro + rd * dist;
                        if (!m_state->connectFirst) {
                            m_state->connectFirst = hit->id;
                            m_state->connectPoint[0] = point.x; m_state->connectPoint[1] = point.y; m_state->connectPoint[2] = point.z;
                            m_scene->select(hit);
                        } else if (hit->id != m_state->connectFirst) {
                            SceneNode* first = m_scene->findById(m_state->connectFirst);
                            glm::vec3 p0(m_state->connectPoint[0], m_state->connectPoint[1], m_state->connectPoint[2]);
                            if (first && onConnect) onConnect(first, p0, hit, point);
                            m_state->connectTool = -1;
                            m_state->connectFirst = 0;
                        }
                    }
                }
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }

            // Left-click to pick — but not while interacting with the gizmo.
            bool overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
            // Ctrl+click adds / removes, Shift+click adds.
            auto pick = [&](SceneNode* hit) {
                ImGuiIO& io = ImGui::GetIO();
                if (io.KeyCtrl)       m_scene->toggleSelection(hit);
                else if (io.KeyShift) m_scene->addToSelection(hit);
                else if (hit)         m_scene->select(hit);
                else                  m_scene->deselect();
            };
            if (m_hovered && !overGizmo && iconHit && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                pick(iconHit);
            } else if (m_hovered && !overGizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 m = ImGui::GetMousePos();
                glm::vec3 ro, rd;
                mouseRay({m.x, m.y}, imgMin, imgSize, view, proj, ro, rd);
                SceneNode* hit = Physics::raycast(*m_scene, ro, rd);
                if (hit && hit->locked) hit = nullptr;            // Locked parts can't be clicked (Alt+L)
                // Like Roblox: clicking a part inside a Model picks the whole Model
                // (the top one under the Workspace). Alt+click picks just the part.
                if (hit && !ImGui::GetIO().KeyAlt && !m_scene->isCharacterPart(hit)) {
                    SceneNode* top = hit;
                    for (SceneNode* p = hit->parent; p && p != m_scene->root(); p = p->parent)
                        if (p->kind == NodeKind::Model) top = p;
                    hit = top;
                }
                if (hit || !(ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift)) pick(hit);
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// Blender-style mode menu in the Viewport's top-left corner.
bool ViewportPanel::drawModeMenu(ImVec2 imgPos) {
    static const Icons::Id kIcons[] = {Icons::Id::Build, Icons::Id::Mesh, Icons::Id::Simulate, Icons::Id::Play};
    static const char* kKeys[] = {"Shift+F5", "Tab", "F8", "F5"};
    static const char* kTips[] = {"Place and change objects", "Reshape the selected part's mesh (corners, edges, faces)",
                                  "Physics and scripts run live; fly around, inspect and drag things",
                                  "Playtest with your character"};
    int cur = (int)m_state->mode;
    char label[64];
    std::snprintf(label, sizeof(label), "%s Mode", kStudioModeNames[cur]);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 a(imgPos.x + 8, imgPos.y + 8), b(a.x + ts.x + 50, a.y + ts.y + 12);
    ImGui::SetCursorScreenPos(a);
    bool clicked = ImGui::InvisibleButton("##mode", ImVec2(b.x - a.x, b.y - a.y));
    bool hover = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, hover ? IM_COL32(70, 70, 76, 235) : IM_COL32(40, 40, 44, 220), 5.0f);
    dl->AddRect(a, b, IM_COL32(90, 90, 96, 255), 5.0f);
    Icons::draw(dl, ImVec2(a.x + 14, (a.y + b.y) * 0.5f), 16.0f, kIcons[cur]);
    dl->AddText(ImVec2(a.x + 27, a.y + 6), IM_COL32(235, 235, 235, 255), label);
    float ax = b.x - 13, ay = (a.y + b.y) * 0.5f;
    dl->AddTriangleFilled(ImVec2(ax - 4, ay - 2), ImVec2(ax + 4, ay - 2), ImVec2(ax, ay + 3), IM_COL32(200, 200, 200, 255));
    if (hover) ImGui::SetTooltip("Switch mode (like Blender)");
    if (clicked) ImGui::OpenPopup("##modes");
    ImGui::SetNextWindowPos(ImVec2(a.x, b.y + 2));
    if (ImGui::BeginPopup("##modes")) {
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            char row[64];
            std::snprintf(row, sizeof(row), "      %s", kStudioModeNames[i]);
            if (ImGui::Selectable(row, cur == i, 0, ImVec2(230, 0)) && onMode) onMode((StudioMode)i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kTips[i]);
            Icons::draw(ImGui::GetWindowDrawList(), ImVec2(p.x + 10, p.y + ImGui::GetTextLineHeight() * 0.5f), 16.0f, kIcons[i]);
            ImVec2 ks = ImGui::CalcTextSize(kKeys[i]);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 230 - ks.x, p.y), IM_COL32(140, 140, 140, 255), kKeys[i]);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    return hover;
}
