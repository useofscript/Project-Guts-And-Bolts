// Studio's modes (Build / Modeling / Simulate / Play) and the Modeling-mode
// mesh tools. See EditorState.h for what each mode is.
#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "../scene/Scene.h"
#include "../scene/EditMesh.h"
#include "../scene/Serializer.h"
#include "../core/Log.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

void Editor::setMode(StudioMode mode) {
    StudioMode cur = m_state.mode;
    if (mode == cur) return;
    if (cur == StudioMode::Modeling) exitModeling();
    if (cur == StudioMode::Simulate || cur == StudioMode::Play) togglePlay();   // back to Build first
    switch (mode) {
        case StudioMode::Build:    break;
        case StudioMode::Modeling: enterModeling(); break;
        case StudioMode::Simulate: startPlay(2); break;
        case StudioMode::Play:     startPlay(0); break;
    }
}

void Editor::enterModeling() {
    SceneNode* n = m_scene->selected();
    if (!n || !n->isPart() || !canEdit(n) || m_scene->isCharacterPart(n)) {
        Log::warn("Modeling: select one part first, then press Tab (or pick Modeling in the mode menu).");
        return;
    }
    if (n->primitiveType != PrimitiveType::Mesh) {
        // A Plane is paper-thin: keep its box thin too so it still collides like one.
        if (n->primitiveType == PrimitiveType::Plane) n->transform.scale.y = 0.04f;
        MeshEdit::attach(*n, MeshEdit::fromPrimitive(n->primitiveType));
        Log::info("'" + n->name + "' is now an editable mesh (a MeshPart). Ctrl+Z turns it back.");
    }
    int pickMode = m_state.modeling.selectMode;
    bool xray = m_state.modeling.xray;
    m_state.modeling = ModelingState{};
    m_state.modeling.node = n->id;
    m_state.modeling.selectMode = pickMode;
    m_state.modeling.xray = xray;
    m_state.modeling.sel.assign(n->editMesh->verts.size(), 0);
    m_state.mode = StudioMode::Modeling;
    m_state.connectTool = -1;
    m_ribbonTab = 5;   // MESH
    m_viewport->focus();
}

void Editor::exitModeling() {
    if (SceneNode* n = m_scene->findById(m_state.modeling.node)) MeshEdit::fit(*n);
    m_state.modeling.node = 0;
    m_state.modeling.sel.clear();
    if (m_state.mode == StudioMode::Modeling) m_state.mode = StudioMode::Build;
    if (m_ribbonTab == 5) m_ribbonTab = 0;
}

// Leave Modeling mode if the part went away (deleted, undone) or something
// else got selected in the Explorer.
void Editor::checkModeling() {
    if (m_state.mode != StudioMode::Modeling) return;
    SceneNode* n = m_scene->findById(m_state.modeling.node);
    if (!n || !n->editMesh) { exitModeling(); return; }
    if (m_scene->selected() != n) exitModeling();
}

void Editor::meshOp(MeshOp op) {
    SceneNode* n = m_scene->findById(m_state.modeling.node);
    if (!n || !n->editMesh) return;
    ModelingState& ms = m_state.modeling;
    // Work on a copy so a tool that goes wrong can't leave a broken mesh.
    EditMesh work = *n->editMesh;
    MeshEdit::Selection sel = ms.sel;
    sel.resize(work.verts.size(), 0);
    bool ok = false;
    const char* why = "";

    switch (op) {
    case MeshOp::Extrude: {
        // Which way is "out": the picked faces' direction (or the faces next to picked edges).
        glm::mat4 M = n->worldMatrix();
        glm::mat3 normalM = glm::transpose(glm::inverse(glm::mat3(M)));
        glm::vec3 dir(0.0f);
        auto faces = MeshEdit::selectedFaces(work, sel);
        if (faces.empty()) {
            for (size_t f = 0; f < work.faces.size(); ++f) {
                const auto& face = work.faces[f];
                for (size_t i = 0; i < face.size(); ++i)
                    if (sel[face[i]] && sel[face[(i + 1) % face.size()]]) { faces.push_back((int)f); break; }
            }
        }
        for (int f : faces) dir += normalM * MeshEdit::faceNormal(work, work.faces[f]);
        ok = MeshEdit::extrude(work, sel);
        why = "Extrude: pick some faces or edges first.";
        if (ok && glm::length(dir) > 1e-6f) {
            glm::vec3 d = glm::vec3(glm::inverse(M) * glm::vec4(glm::normalize(dir) * 1.0f, 0.0f));   // 1 stud out
            for (size_t i = 0; i < sel.size(); ++i) if (sel[i]) work.verts[i] += d;
        }
        break;
    }
    case MeshOp::Inset:     ok = MeshEdit::inset(work, sel, 0.25f); why = "Inset: pick some faces first."; break;
    case MeshOp::Subdivide: ok = MeshEdit::subdivide(work, sel); break;
    case MeshOp::Delete:
        ok = MeshEdit::remove(work, sel, ms.selectMode);
        why = ms.selectMode == 2 ? "Delete: pick some faces first." : ms.selectMode == 1 ? "Delete: pick some edges first."
                                                                    : "Delete: pick some vertices first.";
        break;
    case MeshOp::Merge:  ok = MeshEdit::merge(work, sel); why = "Merge: pick two or more vertices."; break;
    case MeshOp::Fill:   ok = MeshEdit::fill(work, sel);  why = "Fill: pick three or more vertices around a hole."; break;
    case MeshOp::Flip:   ok = MeshEdit::flip(work, sel); break;
    case MeshOp::Smooth: work.smooth = !work.smooth; ok = true; break;
    case MeshOp::SelectAll: {
        bool all = MeshEdit::countSelected(sel) == (int)sel.size();
        std::fill(sel.begin(), sel.end(), all ? 0 : 1);
        ms.sel = sel;
        return;
    }
    case MeshOp::SelectNone: std::fill(ms.sel.begin(), ms.sel.end(), 0); return;
    case MeshOp::Invert:     for (char& c : ms.sel) c = !c; return;
    }
    if (ok && work.faces.empty()) { ok = false; why = "That would leave nothing - to remove the part, delete it in Build mode."; }
    if (!ok) { if (*why) Log::warn(why); return; }
    MeshEdit::own(*n) = std::move(work);
    ms.sel = std::move(sel);
    MeshEdit::fit(*n);
    MeshEdit::refresh(*n);
}

// Keys in Modeling mode (Blender-style). Returns after handling so the
// Build-mode keys (Delete removes the part, ...) don't also fire.
void Editor::handleModelingKeys() {
    ImGuiIO& io = ImGui::GetIO();
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (io.KeyCtrl) {
        if (pressed(ImGuiKey_Z)) { io.KeyShift ? redo() : undo(); }
        if (pressed(ImGuiKey_Y)) redo();
        if (pressed(ImGuiKey_S)) save();
        if (pressed(ImGuiKey_I)) meshOp(MeshOp::Invert);
        if (pressed(ImGuiKey_L)) m_state.gizmoLocal = !m_state.gizmoLocal;
        if (pressed(ImGuiKey_1)) m_state.tool = GizmoTool::Select;
        if (pressed(ImGuiKey_2)) m_state.tool = GizmoTool::Translate;
        if (pressed(ImGuiKey_3)) m_state.tool = GizmoTool::Scale;
        if (pressed(ImGuiKey_4)) m_state.tool = GizmoTool::Rotate;
        return;
    }
    if (io.KeyAlt) {
        if (pressed(ImGuiKey_A)) meshOp(MeshOp::SelectNone);
        if (pressed(ImGuiKey_Z)) m_state.modeling.xray = !m_state.modeling.xray;
        return;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) return;   // flying the camera with WASDQE
    if (pressed(ImGuiKey_1)) { m_state.modeling.selectMode = 0; }
    if (pressed(ImGuiKey_2)) { m_state.modeling.selectMode = 1; }
    if (pressed(ImGuiKey_3)) { m_state.modeling.selectMode = 2; }
    if (pressed(ImGuiKey_A)) meshOp(MeshOp::SelectAll);
    if (pressed(ImGuiKey_E)) meshOp(MeshOp::Extrude);
    if (pressed(ImGuiKey_I)) meshOp(MeshOp::Inset);
    if (pressed(ImGuiKey_X) || pressed(ImGuiKey_Delete)) meshOp(MeshOp::Delete);
    if (pressed(ImGuiKey_M)) meshOp(MeshOp::Merge);
    if (pressed(ImGuiKey_F)) meshOp(MeshOp::Fill);
    if (pressed(ImGuiKey_Tab) || pressed(ImGuiKey_Escape)) setMode(StudioMode::Build);
}

// Test helper: --test-mesh "cube:face-top,extrude,inset" style scripts.
void Editor::testMesh(const std::string& steps) {
    size_t start = 0;
    while (start <= steps.size()) {
        size_t end = steps.find(',', start);
        if (end == std::string::npos) end = steps.size();
        std::string s = steps.substr(start, end - start);
        start = end + 1;
        if (s.empty()) continue;
        if (s == "enter") setMode(StudioMode::Modeling);
        else if (s == "exit") setMode(StudioMode::Build);
        else if (s == "simulate") setMode(StudioMode::Simulate);
        else if (s == "play") setMode(StudioMode::Play);
        else if (s == "vertex" || s == "edge" || s == "face") m_state.modeling.selectMode = s == "vertex" ? 0 : s == "edge" ? 1 : 2;
        else if (s == "all") meshOp(MeshOp::SelectAll);
        else if (s == "none") meshOp(MeshOp::SelectNone);
        else if (s == "extrude") meshOp(MeshOp::Extrude);
        else if (s == "inset") meshOp(MeshOp::Inset);
        else if (s == "subdivide") meshOp(MeshOp::Subdivide);
        else if (s == "delete") meshOp(MeshOp::Delete);
        else if (s == "merge") meshOp(MeshOp::Merge);
        else if (s == "fill") meshOp(MeshOp::Fill);
        else if (s == "flip") meshOp(MeshOp::Flip);
        else if (s == "smooth") meshOp(MeshOp::Smooth);
        else if (s == "xray") m_state.modeling.xray = !m_state.modeling.xray;
        else if (s.rfind("save:", 0) == 0) saveFile(s.substr(5));
        else if (s == "roundtrip") {
            // Save and load the whole place, then report every MeshPart.
            std::string saved = Serializer::saveScene(*m_scene);
            restore(saved);
            m_scene->forEach([](SceneNode* n) {
                if (n->editMesh)
                    Log::info("roundtrip: " + n->name + " has " + std::to_string(n->editMesh->verts.size()) + " corners, " +
                              std::to_string(n->editMesh->faces.size()) + " faces" + (n->mesh ? "" : " (NO GPU MESH)"));
            });
        }
        else if (s.rfind("top", 0) == 0 || s == "bottom") {
            // Pick the vertices on the top (or bottom) of the mesh.
            SceneNode* n = m_scene->findById(m_state.modeling.node);
            if (!n || !n->editMesh) continue;
            auto& ms = m_state.modeling;
            ms.sel.assign(n->editMesh->verts.size(), 0);
            float best = s == "bottom" ? 1e9f : -1e9f;
            for (auto& p : n->editMesh->verts) best = s == "bottom" ? std::min(best, p.y) : std::max(best, p.y);
            for (size_t i = 0; i < ms.sel.size(); ++i)
                if (std::abs(n->editMesh->verts[i].y - best) < 1e-4f) ms.sel[i] = 1;
        } else if (s.rfind("move:", 0) == 0) {
            // move:x:y:z in studs (world), like dragging the gizmo.
            SceneNode* n = m_scene->findById(m_state.modeling.node);
            float x = 0, y = 0, z = 0;
            if (!n || std::sscanf(s.c_str() + 5, "%f:%f:%f", &x, &y, &z) != 3) continue;
            glm::vec3 d = glm::vec3(glm::inverse(n->worldMatrix()) * glm::vec4(x, y, z, 0.0f));
            EditMesh& m = MeshEdit::own(*n);
            for (size_t i = 0; i < m.verts.size() && i < m_state.modeling.sel.size(); ++i)
                if (m_state.modeling.sel[i]) m.verts[i] += d;
            MeshEdit::fit(*n);
            MeshEdit::refresh(*n);
        } else Log::warn("--test-mesh: unknown step '" + s + "'");
    }
}
