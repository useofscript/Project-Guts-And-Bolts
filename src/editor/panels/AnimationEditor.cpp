#include "AnimationEditor.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {

void allParts(SceneNode* n, std::vector<SceneNode*>& out) {
    for (auto& c : n->children) {
        if (c->kind == NodeKind::Part && c->mesh) out.push_back(c.get());
        if (c->kind == NodeKind::Part || c->kind == NodeKind::Model) allParts(c.get(), out);
    }
}

void findAnimations(SceneNode* n, std::vector<SceneNode*>& out) {
    for (auto& c : n->children) {
        if (c->isAnimation()) out.push_back(c.get());
        findAnimations(c.get(), out);
    }
}

bool moved(const Transform& a, const Transform& b) {
    return glm::length(a.position - b.position) > 1e-4f || glm::length(a.rotation - b.rotation) > 1e-3f;
}

const ImU32 kKeyCol    = IM_COL32(240, 190, 60, 255);
const ImU32 kKeySelCol = IM_COL32(90, 180, 255, 255);
const ImU32 kHeadCol   = IM_COL32(230, 70, 70, 255);

void diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), col);
    dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), IM_COL32(30, 30, 30, 255));
}

} // namespace

AnimationEditor::AnimationEditor(Scene* scene, EditorState* state) : m_scene(scene), m_state(state) {}

SceneNode* AnimationEditor::rig() {
    SceneNode* r = m_rig ? m_scene->findById(m_rig) : nullptr;
    if (m_rig && !r) {   // deleted (or undone away)
        m_rig = 0;
        m_state->animRig = 0;
        m_rest.clear();
        m_applied.clear();
        m_playing = false;
    }
    return r;
}

SceneNode* AnimationEditor::animation() {
    SceneNode* r = rig();
    if (!r) return nullptr;
    SceneNode* a = m_anim ? m_scene->findById(m_anim) : nullptr;
    if (a && a->isAnimation() && r->isAncestorOf(a)) return a;
    std::vector<SceneNode*> all;
    findAnimations(r, all);
    m_anim = all.empty() ? 0 : all.front()->id;
    return all.empty() ? nullptr : all.front();
}

Anim::Clip AnimationEditor::clip() {
    SceneNode* a = animation();
    return a ? Anim::parse(a->source) : Anim::Clip();
}

void AnimationEditor::save(const Anim::Clip& c) {
    if (SceneNode* a = animation()) a->source = Anim::dump(c);
}

void AnimationEditor::ensureRest() {
    SceneNode* r = rig();
    if (!r) return;
    std::vector<SceneNode*> parts;
    allParts(r, parts);
    for (SceneNode* n : parts) {
        if (!n->restPose) {
            // New (or freshly loaded by undo): where it is now is where it really is.
            n->restPose = std::make_shared<Transform>(n->transform);
            m_applied.erase(n->id);
        }
        Transform t = *n->restPose;
        t.scale = n->transform.scale;   // size isn't animated: keep the real one
        m_rest[n->id] = t;
    }
}

void AnimationEditor::editRig(SceneNode* node) {
    close();
    SceneNode* r = node && node->kind == NodeKind::Model && node->parent ? node : Anim::rigOf(node);
    if (!r) return;
    m_rig = r->id;
    m_state->animRig = m_rig;
    m_rest.clear();
    m_applied.clear();
    ensureRest();
    m_anim = 0;
    animation();
    m_time = 0.0f;
    m_hasSel = false;
    m_state->tool = GizmoTool::Rotate;   // posing is mostly turning
    m_state->gizmoLocal = true;
}

void AnimationEditor::close() {
    if (SceneNode* r = m_rig ? m_scene->findById(m_rig) : nullptr) {
        std::vector<SceneNode*> parts;
        allParts(r, parts);
        for (SceneNode* n : parts) {
            if (n->restPose) {
                n->transform.position = n->restPose->position;
                n->transform.rotation = n->restPose->rotation;
                n->restPose.reset();
            }
        }
    }
    m_rig = 0;
    m_state->animRig = 0;
    m_rest.clear();
    m_applied.clear();
    m_playing = false;
    m_dragging = false;
}

float AnimationEditor::snap(float t) const {
    t = std::max(0.0f, t);
    return m_fps > 0 ? std::round(t * (float)m_fps) / (float)m_fps : t;
}

void AnimationEditor::setTime(float t) { m_time = std::max(0.0f, t); }

void AnimationEditor::keyPart(Anim::Clip& c, SceneNode* part) {
    SceneNode* r = rig();
    if (!r || !part) return;
    std::map<std::string, Anim::Sample> others;
    Anim::sample(c, m_time, others);
    others.erase(part->name);
    Anim::Pose p;
    if (!Anim::poseFromTransform(r, m_rest, others, part, part->transform, p)) return;
    Anim::Keyframe& k = c.addKey(snap(m_time));
    auto old = k.poses.find(part->name);
    if (old != k.poses.end()) { p.easing = old->second.easing; p.dir = old->second.dir; }
    k.poses[part->name] = p;
}

void AnimationEditor::update(float dt, bool gizmoInUse) {
    SceneNode* r = rig();
    if (!r) return;
    m_state->animRig = m_rig;
    ensureRest();
    SceneNode* a = animation();
    if (!a) {
        Anim::restore(r, m_rest);
        m_applied.clear();
        return;
    }
    Anim::Clip c = clip();
    if (gizmoInUse) m_playing = false;
    if (m_playing) {
        float len = c.length();
        m_time += dt;
        if (len <= 0.0f) m_time = 0.0f;
        else if (m_time > len) {
            if (c.loop) m_time = std::fmod(m_time, len);
            else { m_time = len; m_playing = false; }
        }
    }

    // Someone turned or moved one of the rig's parts (Rotate / Move tools,
    // Properties): that's a pose at the playhead.
    SceneNode* sel = m_scene->selected();
    bool selInRig = sel && sel->isPart() && r->isAncestorOf(sel);
    if (selInRig && !m_playing) {
        auto it = m_applied.find(sel->id);
        if (it != m_applied.end() && moved(it->second, sel->transform)) {
            keyPart(c, sel);
            save(c);
            m_hasSel = true;
            m_selTime = snap(m_time);
            m_selPart = sel->name;
        }
    }

    // Show the pose at the playhead.
    Transform keep = selInRig ? sel->transform : Transform();
    std::map<std::string, Anim::Sample> s;
    Anim::sample(c, m_time, s);
    Anim::restore(r, m_rest);
    Anim::applyPoses(r, m_rest, s);
    // (While dragging, the part stays exactly where the tool puts it.)
    if (gizmoInUse && selInRig) { sel->transform.position = keep.position; sel->transform.rotation = keep.rotation; }
    m_applied.clear();
    std::vector<SceneNode*> parts;
    allParts(r, parts);
    for (SceneNode* n : parts) m_applied[n->id] = n->transform;
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

void AnimationEditor::render(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(980, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation Editor", open)) { ImGui::End(); return; }
    SceneNode* r = rig();
    if (!r) {
        ImGui::TextWrapped("Pick something to animate: select a Model made of parts (like a Rig: MODEL tab > Rig, "
                           "or insert \"Rig\" with Ctrl+I), then press Animate.");
        SceneNode* sel = m_scene->selected();
        SceneNode* cand = sel ? (sel->kind == NodeKind::Model && sel->parent ? sel : Anim::rigOf(sel)) : nullptr;
        if (cand && Anim::rigParts(cand).empty()) cand = nullptr;
        ImGui::BeginDisabled(!cand);
        std::string label = cand ? "Animate \"" + cand->name + "\"" : std::string("Animate (select a model)");
        if (ImGui::Button(label.c_str())) editRig(cand);
        ImGui::EndDisabled();
        ImGui::End();
        return;
    }

    // Which rig and which animation.
    ImGui::Text("Rig: %s", r->name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Done")) { close(); ImGui::End(); return; }
    ImGui::SameLine(0, 20);
    std::vector<SceneNode*> anims;
    findAnimations(r, anims);
    SceneNode* a = animation();
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("Animation", a ? a->name.c_str() : "(none)")) {
        for (SceneNode* n : anims) {
            ImGui::PushID((void*)n);
            if (ImGui::Selectable(n->name.c_str(), n == a)) { m_anim = n->id; m_time = 0.0f; m_hasSel = false; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("New")) {
        auto n = std::make_unique<SceneNode>(anims.empty() ? "Animation" : "Animation" + std::to_string(anims.size() + 1),
                                             NodeKind::Animation);
        n->source = Anim::emptyClipText();
        SceneNode* made = m_scene->insert(std::move(n), r);
        m_anim = made->id;
        m_time = 0.0f;
        m_hasSel = false;
        a = made;
    }
    if (a) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        if (ImGui::InputText("Name", &a->name)) {}
    }
    if (!a) {
        ImGui::TextDisabled("No animation in this rig yet: press New.");
        ImGui::End();
        return;
    }

    Anim::Clip c = clip();
    drawToolbar(c);
    ImGui::Separator();
    float poseW = 230.0f;
    ImGui::BeginChild("##timeline", ImVec2(ImGui::GetContentRegionAvail().x - poseW, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    drawTimeline(c);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##pose", ImVec2(0, 0), true);
    drawPosePanel(c);
    ImGui::EndChild();
    ImGui::End();
}

void AnimationEditor::drawToolbar(Anim::Clip& c) {
    float len = c.length();
    if (ImGui::Button("|<")) { setTime(0.0f); m_playing = false; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("To the start");
    ImGui::SameLine();
    if (ImGui::Button(m_playing ? "Pause" : "Play", ImVec2(52, 0))) {
        if (!m_playing && len > 0.0f && m_time >= len - 1e-4f) m_time = 0.0f;
        m_playing = !m_playing;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / pause the animation here (it doesn't start the game)");
    ImGui::SameLine();
    if (ImGui::Button(">|")) { setTime(len); m_playing = false; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("To the last keyframe");
    ImGui::SameLine();
    int frame = (int)std::lround(m_time * 60.0f);
    ImGui::Text("%d:%02d  (%.2f s)", frame / 60, frame % 60, m_time);
    ImGui::SameLine(0, 18);
    bool loop = c.loop;
    if (ImGui::Checkbox("Loop", &loop)) { c.loop = loop; save(c); }
    ImGui::SameLine();
    int pr = (int)c.priority;
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("Priority", &pr, Anim::kPriorityNames, 4)) { c.priority = (Anim::Priority)pr; save(c); }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When two animations move the same part, the higher one wins:\n"
                          "Core < Idle < Movement < Action (walking is below all of them)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::DragFloat("Show (s)", &m_view, 0.05f, 0.5f, 120.0f, "%.1f");
    ImGui::SameLine();
    static const char* snaps[] = {"Off", "1/60 s", "1/30 s", "1/10 s"};
    int snapIdx = m_fps == 60 ? 1 : m_fps == 30 ? 2 : m_fps == 10 ? 3 : 0;
    ImGui::SetNextItemWidth(80);
    if (ImGui::Combo("Snap", &snapIdx, snaps, 4)) m_fps = snapIdx == 1 ? 60 : snapIdx == 2 ? 30 : snapIdx == 3 ? 10 : 0;

    SceneNode* r = rig();
    SceneNode* sel = m_scene->selected();
    bool selInRig = r && sel && sel->isPart() && r->isAncestorOf(sel);
    if (ImGui::Button("+ Keyframe")) {
        // Key everything the animation already moves (or every part, the first time).
        std::vector<SceneNode*> parts = Anim::rigParts(r);
        std::map<std::string, bool> used;
        for (const auto& k : c.keys) for (const auto& [n, p] : k.poses) used[n] = true;
        for (SceneNode* p : parts)
            if (used.empty() || used.count(p->name)) keyPart(c, p);
        save(c);
        m_hasSel = true; m_selTime = snap(m_time); m_selPart.clear();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A keyframe here with every part as it is now");
    ImGui::SameLine();
    ImGui::BeginDisabled(!selInRig);
    if (ImGui::Button("Key part")) { keyPart(c, sel); save(c); m_hasSel = true; m_selTime = snap(m_time); m_selPart = sel->name; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Key just the selected part here (turning or moving it does this too)");
    ImGui::SameLine();
    if (ImGui::Button("Reset part")) {
        Anim::Keyframe& k = c.addKey(snap(m_time));
        k.poses[sel->name] = Anim::Pose();
        save(c);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put the selected part back how it was built, at this time");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_hasSel);
    if (ImGui::Button("Delete")) {
        if (Anim::Keyframe* k = c.keyAt(m_selTime)) {
            if (m_selPart.empty()) k->poses.clear();
            else k->poses.erase(m_selPart);
        }
        c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(), [](const Anim::Keyframe& k) { return k.poses.empty(); }),
                     c.keys.end());
        save(c);
        m_hasSel = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete the picked keyframe (Delete key)");
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
        m_clipboard.clear();
        if (Anim::Keyframe* k = c.keyAt(m_selTime)) {
            if (m_selPart.empty()) m_clipboard = k->poses;
            else if (k->poses.count(m_selPart)) m_clipboard[m_selPart] = k->poses[m_selPart];
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_clipboard.empty());
    if (ImGui::Button("Paste")) {
        Anim::Keyframe& k = c.addKey(snap(m_time));
        for (const auto& [n, p] : m_clipboard) k.poses[n] = p;
        save(c);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paste the copied poses at the playhead");
    ImGui::EndDisabled();
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("Pick a part, turn it with Rotate (Ctrl+3) or move it (Ctrl+2): that keys it at the playhead.");
}

void AnimationEditor::drawTimeline(Anim::Clip& c) {
    SceneNode* r = rig();
    if (!r) return;
    std::vector<SceneNode*> parts = Anim::rigParts(r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float labelW = 150.0f, rulerH = 22.0f, rowH = 20.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float width = std::max(200.0f, ImGui::GetContentRegionAvail().x - labelW - 10.0f);
    int rows = (int)parts.size() + 1;   // "Keyframes" (all) + one per part
    float height = rulerH + rows * rowH;
    m_view = std::max(m_view, c.length() + 0.25f);
    const float x0 = origin.x + labelW;
    auto toX = [&](float t) { return x0 + t / m_view * width; };
    auto toT = [&](float x) { return (x - x0) / width * m_view; };

    // Labels (click a part's name to pick it).
    SceneNode* sel = m_scene->selected();
    for (int i = 0; i < rows; ++i) {
        float y = origin.y + rulerH + i * rowH;
        if (i % 2 == 0) dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(x0 + width, y + rowH), IM_COL32(255, 255, 255, 10));
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 4, y + 2));
        if (i == 0) { ImGui::TextDisabled("All keyframes"); continue; }
        SceneNode* p = parts[i - 1];
        ImGui::PushID((void*)p);
        if (ImGui::Selectable(p->name.c_str(), p == sel, 0, ImVec2(labelW - 8, rowH - 4))) m_scene->select(p);
        ImGui::PopID();
    }

    // Ruler.
    dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x0 + width, origin.y + rulerH), IM_COL32(40, 40, 44, 255));
    float step = m_view <= 2.0f ? 0.1f : m_view <= 8.0f ? 0.5f : m_view <= 30.0f ? 1.0f : 5.0f;
    for (int i = 0; i * step <= m_view + 1e-4f; ++i) {
        float t = i * step, x = toX(t);
        bool major = std::fmod(t + 1e-4f, step * 5.0f) < 2e-3f;
        dl->AddLine(ImVec2(x, origin.y + (major ? 6.0f : 14.0f)), ImVec2(x, origin.y + rulerH), IM_COL32(150, 150, 160, 255));
        if (major) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", t);
            dl->AddText(ImVec2(x + 2, origin.y), IM_COL32(190, 190, 200, 255), buf);
        }
        dl->AddLine(ImVec2(x, origin.y + rulerH), ImVec2(x, origin.y + height), IM_COL32(255, 255, 255, major ? 18 : 8));
    }
    // The end of the animation.
    if (c.length() > 0.0f)
        dl->AddLine(ImVec2(toX(c.length()), origin.y), ImVec2(toX(c.length()), origin.y + height), IM_COL32(120, 200, 120, 160), 2.0f);

    // Keyframes: one diamond per pose, and one on the top row per keyframe.
    struct Hit { float time; std::string part; ImVec2 at; };
    std::vector<Hit> hits;
    for (const Anim::Keyframe& k : c.keys) {
        float x = toX(k.time);
        ImVec2 top(x, origin.y + rulerH + rowH * 0.5f);
        bool pickedAll = m_hasSel && m_selPart.empty() && std::fabs(m_selTime - k.time) < 1e-3f;
        diamond(dl, top, 6.0f, pickedAll ? kKeySelCol : kKeyCol);
        if (!k.name.empty()) dl->AddText(ImVec2(x + 8, top.y - 7), IM_COL32(255, 220, 120, 255), k.name.c_str());
        hits.push_back({k.time, "", top});
        for (size_t i = 0; i < parts.size(); ++i) {
            auto it = k.poses.find(parts[i]->name);
            if (it == k.poses.end()) continue;
            ImVec2 at(x, origin.y + rulerH + (i + 1) * rowH + rowH * 0.5f);
            bool picked = m_hasSel && m_selPart == parts[i]->name && std::fabs(m_selTime - k.time) < 1e-3f;
            diamond(dl, at, 5.0f, picked ? kKeySelCol : it->second.easing == Anim::Easing::Constant ? IM_COL32(200, 200, 200, 255) : kKeyCol);
            hits.push_back({k.time, parts[i]->name, at});
        }
    }
    // The playhead.
    float hx = toX(m_time);
    dl->AddLine(ImVec2(hx, origin.y), ImVec2(hx, origin.y + height), kHeadCol, 2.0f);
    dl->AddTriangleFilled(ImVec2(hx - 6, origin.y), ImVec2(hx + 6, origin.y), ImVec2(hx, origin.y + 8), kHeadCol);

    // One invisible button over the tracks, so dragging counts as one edit (one undo step).
    ImGui::SetCursorScreenPos(ImVec2(x0, origin.y));
    ImGui::InvisibleButton("##tracks", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    ImVec2 m = ImGui::GetMousePos();
    auto hitAt = [&]() -> const Hit* {
        const Hit* best = nullptr;
        float bestD = 8.0f;
        for (const Hit& h : hits) {
            float d = std::max(std::fabs(h.at.x - m.x), std::fabs(h.at.y - m.y));
            if (d < bestD) { bestD = d; best = &h; }
        }
        return best;
    };
    static bool scrubbing = false;
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Hit* h = m.y > origin.y + rulerH ? hitAt() : nullptr;
        if (h) {
            m_hasSel = true; m_selTime = h->time; m_selPart = h->part;
            m_dragging = true; m_dragFrom = h->time;
            setTime(h->time);
            m_playing = false;
            if (!h->part.empty()) if (SceneNode* p = Anim::findPart(r, h->part)) m_scene->select(p);
        } else {
            scrubbing = true;
            if (m.y > origin.y + rulerH) m_hasSel = false;
        }
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        float t = snap(toT(m.x));
        if (scrubbing) { setTime(t); m_playing = false; }
        else if (m_dragging && std::fabs(t - m_selTime) > 1e-4f && !c.keyAt(t, 1e-4f)) {
            // Move the picked keyframe (or just that part's pose) to t.
            Anim::Keyframe* from = c.keyAt(m_selTime);
            if (from) {
                if (m_selPart.empty()) {
                    from->time = t;
                    c.sort();
                } else if (from->poses.count(m_selPart)) {
                    Anim::Pose p = from->poses[m_selPart];
                    from->poses.erase(m_selPart);
                    c.addKey(t).poses[m_selPart] = p;
                    c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(),
                                                [](const Anim::Keyframe& k) { return k.poses.empty(); }), c.keys.end());
                }
                m_selTime = t;
                setTime(t);
                save(c);
            }
        } else if (m_dragging && std::fabs(t - m_selTime) > 1e-4f && c.keyAt(t, 1e-4f) && !m_selPart.empty()) {
            // Onto another keyframe: the pose joins it.
            Anim::Keyframe* from = c.keyAt(m_selTime);
            if (from && from->poses.count(m_selPart)) {
                Anim::Pose p = from->poses[m_selPart];
                from->poses.erase(m_selPart);
                c.keyAt(t, 1e-4f)->poses[m_selPart] = p;
                c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(),
                                            [](const Anim::Keyframe& k) { return k.poses.empty(); }), c.keys.end());
                m_selTime = t;
                setTime(t);
                save(c);
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { scrubbing = false; m_dragging = false; }

    // Right-click a keyframe: easing, name, delete.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (const Hit* h = hitAt()) {
            m_hasSel = true; m_selTime = h->time; m_selPart = h->part;
            Anim::Keyframe* k = c.keyAt(h->time);
            m_newName = k ? k->name : std::string();
            ImGui::OpenPopup("##keymenu");
        }
    }
    if (ImGui::BeginPopup("##keymenu")) {
        Anim::Keyframe* k = c.keyAt(m_selTime);
        if (!k) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        ImGui::TextDisabled("%s at %.2f s", m_selPart.empty() ? "Keyframe" : m_selPart.c_str(), k->time);
        auto forPicked = [&](const std::function<void(Anim::Pose&)>& fn) {
            if (m_selPart.empty()) for (auto& [n, p] : k->poses) fn(p);
            else if (k->poses.count(m_selPart)) fn(k->poses[m_selPart]);
        };
        Anim::Pose* one = !m_selPart.empty() && k->poses.count(m_selPart) ? &k->poses[m_selPart] : nullptr;
        if (ImGui::BeginMenu("Easing style")) {
            for (int i = 0; i < Anim::kEasingCount; ++i)
                if (ImGui::MenuItem(Anim::kEasingNames[i], nullptr, one && (int)one->easing == i)) {
                    forPicked([&](Anim::Pose& p) { p.easing = (Anim::Easing)i; });
                    save(c);
                }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Easing direction")) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::MenuItem(Anim::kEaseDirNames[i], nullptr, one && (int)one->dir == i)) {
                    forPicked([&](Anim::Pose& p) { p.dir = (Anim::EaseDir)i; });
                    save(c);
                }
            ImGui::EndMenu();
        }
        if (m_selPart.empty()) {
            ImGui::SetNextItemWidth(160);
            if (ImGui::InputTextWithHint("##kname", "Name (for KeyframeReached)", &m_newName, ImGuiInputTextFlags_EnterReturnsTrue)) {
                k->name = m_newName;
                save(c);
                ImGui::CloseCurrentPopup();
            }
        }
        if (ImGui::MenuItem("Copy")) {
            m_clipboard.clear();
            if (m_selPart.empty()) m_clipboard = k->poses;
            else if (k->poses.count(m_selPart)) m_clipboard[m_selPart] = k->poses[m_selPart];
        }
        if (ImGui::MenuItem("Delete")) {
            if (m_selPart.empty()) k->poses.clear(); else k->poses.erase(m_selPart);
            c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(), [](const Anim::Keyframe& kk) { return kk.poses.empty(); }),
                         c.keys.end());
            save(c);
            m_hasSel = false;
        }
        ImGui::EndPopup();
    }

    // Delete key removes the picked keyframe.
    if (m_hasSel && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (Anim::Keyframe* k = c.keyAt(m_selTime)) {
            if (m_selPart.empty()) k->poses.clear(); else k->poses.erase(m_selPart);
            c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(), [](const Anim::Keyframe& kk) { return kk.poses.empty(); }),
                         c.keys.end());
            save(c);
        }
        m_hasSel = false;
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height + 4));
    ImGui::Dummy(ImVec2(labelW + width, 1));
}

void AnimationEditor::drawPosePanel(Anim::Clip& c) {
    SceneNode* r = rig();
    SceneNode* sel = m_scene->selected();
    if (!r || !sel || !sel->isPart() || !r->isAncestorOf(sel)) {
        ImGui::TextWrapped("Click a part of the rig (in the 3D view or the list) to pose it.");
        ImGui::Spacing();
        ImGui::TextDisabled("Keys: Delete removes the picked keyframe.\nRight-click a keyframe for easing and names.");
        return;
    }
    ImGui::Text("%s", sel->name.c_str());
    float t = snap(m_time);
    Anim::Keyframe* k = c.keyAt(t);
    Anim::Pose* p = k && k->poses.count(sel->name) ? &k->poses[sel->name] : nullptr;
    Anim::Pose shown;
    if (p) shown = *p;
    else {
        std::map<std::string, Anim::Sample> s;
        Anim::sample(c, m_time, s);
        if (auto it = s.find(sel->name); it != s.end()) { shown.pos = it->second.pos; shown.rot = Anim::quatToEuler(it->second.rot); }
    }
    ImGui::TextDisabled(p ? "Keyed at %.2f s" : "Not keyed at %.2f s (in between)", t);
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    ImGui::TextUnformatted("Turn (degrees)");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::DragFloat3("##rot", &shown.rot.x, 0.5f, -360.0f, 360.0f, "%.1f");
    ImGui::TextUnformatted("Move (studs)");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::DragFloat3("##pos", &shown.pos.x, 0.01f, -50.0f, 50.0f, "%.2f");
    int e = (int)shown.easing, d = (int)shown.dir;
    ImGui::SetNextItemWidth(110);
    changed |= ImGui::Combo("Style", &e, Anim::kEasingNames, Anim::kEasingCount);
    ImGui::SetNextItemWidth(110);
    changed |= ImGui::Combo("Direction", &d, Anim::kEaseDirNames, 3);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetTooltip("How it gets from this keyframe to the next one");
    if (changed) {
        shown.easing = (Anim::Easing)e;
        shown.dir = (Anim::EaseDir)d;
        c.addKey(t).poses[sel->name] = shown;
        save(c);
        m_hasSel = true; m_selTime = t; m_selPart = sel->name;
    }
    if (p && ImGui::Button("Remove this key")) {
        k->poses.erase(sel->name);
        c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(), [](const Anim::Keyframe& kk) { return kk.poses.empty(); }),
                     c.keys.end());
        save(c);
    }
}
