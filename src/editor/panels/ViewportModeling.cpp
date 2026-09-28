// The Viewport in Modeling mode: pick corners (vertices), edges or faces of
// one part's mesh and move, turn or stretch them with the gizmo, like
// Blender's Edit Mode.
#include "ViewportPanel.h"
#include "../EditorState.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../scene/EditMesh.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

namespace {

const ImU32 kPicked   = IM_COL32(255, 150, 40, 255);
const ImU32 kWire     = IM_COL32(20, 20, 24, 220);
const ImU32 kWireBack = IM_COL32(20, 20, 24, 70);

float segmentDistance(ImVec2 p, ImVec2 a, ImVec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0f, 1.0f) : 0.0f;
    float ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y;
    return std::sqrt(ex * ex + ey * ey);
}

// Ray against a triangle (Moller-Trumbore). Returns the distance along the ray.
bool rayTriangle(glm::vec3 ro, glm::vec3 rd, glm::vec3 a, glm::vec3 b, glm::vec3 c, float& t) {
    glm::vec3 e1 = b - a, e2 = c - a, p = glm::cross(rd, e2);
    float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-10f) return false;
    float inv = 1.0f / det;
    glm::vec3 s = ro - a;
    float u = glm::dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    glm::vec3 q = glm::cross(s, e1);
    float v = glm::dot(rd, q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = glm::dot(e2, q) * inv;
    return t > 0;
}

} // namespace

void ViewportPanel::modelingView(const glm::mat4& view, const glm::mat4& proj,
                                 const glm::vec2& imgMin, const glm::vec2& imgSize) {
    ModelingState& ms = m_state->modeling;
    SceneNode* node = m_scene->findById(ms.node);
    if (!node || !node->editMesh) return;
    const EditMesh& m = *node->editMesh;
    if (ms.sel.size() != m.verts.size()) ms.sel.assign(m.verts.size(), 0);   // e.g. after undo

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const glm::mat4 M = node->worldMatrix();
    const glm::mat4 vp = proj * view;
    const glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);

    // Where everything is, in the world and on screen.
    const size_t nv = m.verts.size();
    std::vector<glm::vec3> world(nv);
    std::vector<ImVec2> screen(nv);
    std::vector<char> onScreen(nv, 0);
    for (size_t i = 0; i < nv; ++i) {
        world[i] = glm::vec3(M * glm::vec4(m.verts[i], 1.0f));
        glm::vec4 c = vp * glm::vec4(world[i], 1.0f);
        if (c.w <= 0.01f) continue;
        screen[i] = ImVec2(imgMin.x + (c.x / c.w * 0.5f + 0.5f) * imgSize.x, imgMin.y + (0.5f - c.y / c.w * 0.5f) * imgSize.y);
        onScreen[i] = 1;
    }
    // Which faces point at the camera (the others are round the back).
    std::vector<char> front(m.faces.size(), 1);
    std::vector<char> vertFront(nv, 0);
    for (size_t f = 0; f < m.faces.size(); ++f) {
        const auto& face = m.faces[f];
        glm::vec3 n(0.0f), c(0.0f);
        for (size_t i = 0; i < face.size(); ++i) {
            n += glm::cross(world[face[i]], world[face[(i + 1) % face.size()]]);
            c += world[face[i]];
        }
        c /= (float)face.size();
        front[f] = glm::dot(n, eye - c) > 0.0f;
        if (front[f]) for (uint32_t v : face) vertFront[v] = 1;
    }
    auto visibleVert = [&](uint32_t v) { return onScreen[v] && (ms.xray || vertFront[v]); };

    // --- Draw: picked faces, edges, then corners ---
    for (size_t f = 0; f < m.faces.size(); ++f) {
        const auto& face = m.faces[f];
        bool all = true, shown = true;
        for (uint32_t v : face) { if (!ms.sel[v]) all = false; if (!onScreen[v]) shown = false; }
        if (!shown || (!ms.xray && !front[f])) continue;
        if (all) {
            std::vector<ImVec2> pts;
            for (uint32_t v : face) pts.push_back(screen[v]);
            for (size_t k = 1; k + 1 < pts.size(); ++k)   // fan: works for concave faces too
                dl->AddTriangleFilled(pts[0], pts[k], pts[k + 1], IM_COL32(255, 150, 40, 70));
        }
        if (ms.selectMode == 2) {
            glm::vec3 c(0.0f);
            for (uint32_t v : face) c += world[v];
            glm::vec4 cc = vp * glm::vec4(c / (float)face.size(), 1.0f);
            if (cc.w > 0.01f) {
                ImVec2 sp(imgMin.x + (cc.x / cc.w * 0.5f + 0.5f) * imgSize.x, imgMin.y + (0.5f - cc.y / cc.w * 0.5f) * imgSize.y);
                dl->AddRectFilled(ImVec2(sp.x - 2.5f, sp.y - 2.5f), ImVec2(sp.x + 2.5f, sp.y + 2.5f), all ? kPicked : kWire);
            }
        }
    }
    auto edgeList = MeshEdit::edges(m);
    std::vector<char> edgeFront(edgeList.size(), 0);
    {
        // An edge shows if one of its faces faces us.
        std::vector<std::pair<uint64_t, char>> keys;
        for (size_t f = 0; f < m.faces.size(); ++f) {
            const auto& face = m.faces[f];
            for (size_t i = 0; i < face.size(); ++i) {
                uint32_t a = face[i], b = face[(i + 1) % face.size()];
                if (a > b) std::swap(a, b);
                keys.push_back({((uint64_t)a << 32) | b, front[f]});
            }
        }
        std::sort(keys.begin(), keys.end());
        for (size_t e = 0; e < edgeList.size(); ++e) {
            uint64_t k = ((uint64_t)edgeList[e].first << 32) | edgeList[e].second;
            auto it = std::lower_bound(keys.begin(), keys.end(), std::make_pair(k, (char)0));
            for (; it != keys.end() && it->first == k; ++it) if (it->second) edgeFront[e] = 1;
        }
    }
    for (size_t e = 0; e < edgeList.size(); ++e) {
        auto [a, b] = edgeList[e];
        if (!onScreen[a] || !onScreen[b]) continue;
        bool back = !edgeFront[e];
        if (back && !ms.xray) continue;
        bool picked = ms.sel[a] && ms.sel[b];
        dl->AddLine(screen[a], screen[b], picked ? kPicked : (back ? kWireBack : kWire), picked ? 2.2f : 1.4f);
    }
    if (ms.selectMode == 0)
        for (size_t i = 0; i < nv; ++i)
            if (visibleVert((uint32_t)i)) dl->AddCircleFilled(screen[i], ms.sel[i] ? 4.0f : 3.0f, ms.sel[i] ? kPicked : kWire);

    // --- Gizmo on the picked points ---
    int picked = MeshEdit::countSelected(ms.sel);
    bool gizmoBusy = false;
    if (picked > 0 && m_state->tool != GizmoTool::Select) {
        glm::vec3 pivot(0.0f);
        for (size_t i = 0; i < nv; ++i) if (ms.sel[i]) pivot += world[i];
        pivot /= (float)picked;
        if (!ImGuizmo::IsUsing()) {
            glm::mat4 g(1.0f);
            if (m_state->gizmoLocal)
                for (int k = 0; k < 3; ++k) g[k] = glm::vec4(glm::normalize(glm::vec3(M[k])), 0.0f);
            g[3] = glm::vec4(pivot, 1.0f);
            m_meshGizmo = g;
        }
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSize.x, imgSize.y);
        ImGuizmo::OPERATION op = m_state->tool == GizmoTool::Rotate ? ImGuizmo::ROTATE
                               : m_state->tool == GizmoTool::Scale  ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
        float snap[3] = {0, 0, 0};
        if (m_state->snapEnabled) {
            float s = op == ImGuizmo::TRANSLATE ? m_state->snapTranslate : op == ImGuizmo::ROTATE ? m_state->snapRotate
                                                                                                  : m_state->snapScale;
            snap[0] = snap[1] = snap[2] = s;
        }
        glm::mat4 before = m_meshGizmo;
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op,
                                 m_state->gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                                 glm::value_ptr(m_meshGizmo), nullptr, m_state->snapEnabled ? snap : nullptr)) {
            glm::mat4 delta = m_meshGizmo * glm::inverse(before);
            glm::mat4 toLocal = glm::inverse(M);
            EditMesh& em = MeshEdit::own(*node);
            for (size_t i = 0; i < nv; ++i)
                if (ms.sel[i]) em.verts[i] = glm::vec3(toLocal * delta * glm::vec4(world[i], 1.0f));
            MeshEdit::refresh(*node);
        }
        gizmoBusy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    }
    // Finished dragging: wrap the part's box around the new shape.
    bool using_ = ImGuizmo::IsUsing();
    if (m_meshDragging && !using_) MeshEdit::fit(*node);
    m_meshDragging = using_;

    // --- Picking: click, or drag a box ---
    ImVec2 mouse = io.MousePos;
    if (m_hovered && !gizmoBusy && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_boxStart = mouse;
        m_boxing = true;
    }
    if (!m_boxing) return;
    float dragX = mouse.x - m_boxStart.x, dragY = mouse.y - m_boxStart.y;
    bool isBox = dragX * dragX + dragY * dragY > 25.0f;
    ImVec2 lo(std::min(mouse.x, m_boxStart.x), std::min(mouse.y, m_boxStart.y));
    ImVec2 hi(std::max(mouse.x, m_boxStart.x), std::max(mouse.y, m_boxStart.y));
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (isBox) {
            dl->AddRectFilled(lo, hi, IM_COL32(255, 255, 255, 25));
            dl->AddRect(lo, hi, IM_COL32(255, 255, 255, 170), 0, 0, 1.0f);
        }
        return;
    }
    m_boxing = false;

    // Shift adds / toggles, Ctrl takes away, a plain click starts over.
    int how = io.KeyCtrl ? 2 : io.KeyShift ? 1 : 0;
    if (how == 0) std::fill(ms.sel.begin(), ms.sel.end(), 0);
    auto setVerts = [&](const std::vector<uint32_t>& vs, bool toggle) {
        bool allOn = true;
        for (uint32_t v : vs) if (!ms.sel[v]) allOn = false;
        char val = how == 2 ? 0 : (toggle && how == 1 && allOn ? 0 : 1);
        for (uint32_t v : vs) ms.sel[v] = val;
    };
    auto inBox = [&](ImVec2 p) { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y; };

    if (isBox) {
        if (ms.selectMode == 0) {
            for (uint32_t i = 0; i < nv; ++i) if (visibleVert(i) && inBox(screen[i])) setVerts({i}, false);
        } else if (ms.selectMode == 1) {
            for (size_t e = 0; e < edgeList.size(); ++e) {
                auto [a, b] = edgeList[e];
                if ((ms.xray || edgeFront[e]) && onScreen[a] && onScreen[b] && inBox(screen[a]) && inBox(screen[b])) setVerts({a, b}, false);
            }
        } else {
            for (size_t f = 0; f < m.faces.size(); ++f) {
                if (!ms.xray && !front[f]) continue;
                bool inside = true;
                for (uint32_t v : m.faces[f]) if (!onScreen[v] || !inBox(screen[v])) inside = false;
                if (inside) setVerts(m.faces[f], false);
            }
        }
        return;
    }

    // A single click: the nearest thing under the mouse.
    if (ms.selectMode == 0) {
        int best = -1;
        float bestD = 12.0f;
        for (uint32_t i = 0; i < nv; ++i) {
            if (!visibleVert(i)) continue;
            float d = std::hypot(screen[i].x - mouse.x, screen[i].y - mouse.y);
            if (d < bestD) { bestD = d; best = (int)i; }
        }
        if (best >= 0) setVerts({(uint32_t)best}, true);
    } else if (ms.selectMode == 1) {
        int best = -1;
        float bestD = 9.0f;
        for (size_t e = 0; e < edgeList.size(); ++e) {
            auto [a, b] = edgeList[e];
            if (!onScreen[a] || !onScreen[b] || (!ms.xray && !edgeFront[e])) continue;
            float d = segmentDistance(mouse, screen[a], screen[b]);
            if (d < bestD) { bestD = d; best = (int)e; }
        }
        if (best >= 0) setVerts({edgeList[best].first, edgeList[best].second}, true);
    } else {
        glm::vec3 ro, rd;
        mouseRay({mouse.x, mouse.y}, imgMin, imgSize, view, proj, ro, rd);
        int best = -1;
        float bestT = 1e30f;
        for (size_t f = 0; f < m.faces.size(); ++f) {
            if (!ms.xray && !front[f]) continue;
            const auto& face = m.faces[f];
            for (size_t k = 1; k + 1 < face.size(); ++k) {
                float t;
                if (rayTriangle(ro, rd, world[face[0]], world[face[k]], world[face[k + 1]], t) && t < bestT) {
                    bestT = t;
                    best = (int)f;
                }
            }
        }
        if (best >= 0) setVerts(m.faces[best], true);
    }
}
