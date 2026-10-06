#include "GameGui.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../renderer/Textures.h"

#include <imgui_internal.h>   // ImGui::ShadeVertsLinearUV (pictures in odd shapes)

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace GameGui {

namespace {

// One object to draw: where it goes and what it's clipped to.
struct Item {
    SceneNode* node;
    ImVec2     a, b;        // its rectangle
    ImVec2     clipA, clipB;
};

ImU32 rgba(const glm::vec3& c, float transparency) {
    float alpha = std::clamp(1.0f - transparency, 0.0f, 1.0f);
    return IM_COL32((int)(std::clamp(c.r, 0.0f, 1.0f) * 255), (int)(std::clamp(c.g, 0.0f, 1.0f) * 255),
                    (int)(std::clamp(c.b, 0.0f, 1.0f) * 255), (int)(alpha * 255));
}

const SceneNode* childOfType(const SceneNode* n, GuiType t) {
    for (auto& c : n->children)
        if (c->isGui() && c->gui.type == t && (t == GuiType::UICorner || c->enabled)) return c.get();
    return nullptr;
}

// How round each corner is (pixels): top-left, top-right, bottom-right, bottom-left.
struct Radii {
    float r[4] = {0, 0, 0, 0};
    bool same() const { return r[0] == r[1] && r[1] == r[2] && r[2] == r[3]; }
};

Radii radiiOf(const SceneNode* n, float w, float h) {
    Radii out;
    const SceneNode* c = childOfType(n, GuiType::UICorner);
    if (!c) return out;
    const GuiProps& k = c->gui;
    const float most = std::max(0.0f, std::min(w, h) * 0.5f);
    for (int i = 0; i < 4; ++i) {
        const float v = k.corners[i] < 0 ? k.corner.xs * std::min(w, h) + k.corner.xo
                                         : k.cornerScales[i] * std::min(w, h) + k.corners[i];
        out.r[i] = std::clamp(v, 0.0f, most);
    }
    return out;
}

// The outline of a box with its own roundness at each corner, as ImGui's current path
// (clockwise from the top-left, like ImGui's own rounded boxes).
void pathRounded(ImDrawList* dl, ImVec2 a, ImVec2 b, const Radii& r) {
    const float pi = 3.14159265f;
    const ImVec2 c[4] = {ImVec2(a.x + r.r[0], a.y + r.r[0]), ImVec2(b.x - r.r[1], a.y + r.r[1]),
                         ImVec2(b.x - r.r[2], b.y - r.r[2]), ImVec2(a.x + r.r[3], b.y - r.r[3])};
    dl->PathClear();
    for (int i = 0; i < 4; ++i) {
        if (r.r[i] < 0.5f) dl->PathLineTo(c[i]);
        else dl->PathArcTo(c[i], r.r[i], pi + i * pi * 0.5f, pi + (i + 1) * pi * 0.5f);
    }
}

void fillRounded(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, const Radii& r) {
    if (r.same()) { dl->AddRectFilled(a, b, col, r.r[0]); return; }
    pathRounded(dl, a, b, r);
    dl->PathFillConvex(col);
}

void strokeRounded(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, const Radii& r, float thickness) {
    if (r.same()) { dl->AddRect(a, b, col, r.r[0], 0, thickness); return; }
    pathRounded(dl, a, b, r);
    dl->PathStroke(col, ImDrawFlags_Closed, thickness);
}

// A picture stretched over [a, b] (uv0..uv1 across it), cut to the rounded shape.
void imageRounded(ImDrawList* dl, unsigned tex, ImVec2 a, ImVec2 b, ImVec2 uv0, ImVec2 uv1, ImU32 col, const Radii& r) {
    const ImTextureID id = (ImTextureID)(intptr_t)tex;
    if (r.same()) { dl->AddImageRounded(id, a, b, uv0, uv1, col, r.r[0]); return; }
    dl->PushTexture(id);
    const int start = dl->VtxBuffer.Size;
    pathRounded(dl, a, b, r);
    dl->PathFillConvex(col);
    ImGui::ShadeVertsLinearUV(dl, start, dl->VtxBuffer.Size, a, b, uv0, uv1, true);
    dl->PopTexture();
}

// Points around a rounded box, kArc + 1 per corner, so two boxes always have the same
// number (the shadow joins an inner box to an outer one, point by point).
constexpr int kArc = 8;
void roundedPoints(ImVec2 a, ImVec2 b, const float r[4], ImVec2* out) {
    const float pi = 3.14159265f;
    const ImVec2 c[4] = {ImVec2(a.x + r[0], a.y + r[0]), ImVec2(b.x - r[1], a.y + r[1]),
                         ImVec2(b.x - r[2], b.y - r[2]), ImVec2(a.x + r[3], b.y - r[3])};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k <= kArc; ++k) {
            const float ang = pi + (i + (float)k / kArc) * pi * 0.5f;
            *out++ = ImVec2(c[i].x + std::cos(ang) * r[i], c[i].y + std::sin(ang) * r[i]);
        }
}

// UIShadow: a soft shadow under the box. One mesh: solid in the middle, fading to
// nothing over Blur pixels at the edge (no pictures, no extra passes).
void drawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, const Radii& radii, const GuiProps& s) {
    if (s.bgTransparency >= 1.0f) return;
    const float spread = s.shadowSpread, half = std::max(0.0f, s.shadowBlur) * 0.5f;
    a = ImVec2(a.x - spread + s.shadowOffset.x, a.y - spread + s.shadowOffset.y);
    b = ImVec2(b.x + spread + s.shadowOffset.x, b.y + spread + s.shadowOffset.y);
    if (b.x <= a.x || b.y <= a.y) return;
    ImVec2 ia(a.x + half, a.y + half), ib(b.x - half, b.y - half);   // where it's fully dark
    if (ib.x < ia.x) ia.x = ib.x = (a.x + b.x) * 0.5f;
    if (ib.y < ia.y) ia.y = ib.y = (a.y + b.y) * 0.5f;
    const ImVec2 oa(a.x - half, a.y - half), ob(b.x + half, b.y + half);   // where it's gone
    float ri[4], ro[4];
    const float innerMost = std::min(ib.x - ia.x, ib.y - ia.y) * 0.5f, outerMost = std::min(ob.x - oa.x, ob.y - oa.y) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        const float r = std::max(0.0f, radii.r[i] + spread);
        ri[i] = std::clamp(r - half, 0.0f, innerMost);
        ro[i] = std::clamp(r + half, 0.0f, outerMost);
    }
    constexpr int kPts = 4 * (kArc + 1);
    ImVec2 in[kPts], out[kPts];
    roundedPoints(ia, ib, ri, in);
    roundedPoints(oa, ob, ro, out);
    const ImU32 col = rgba(s.bg, s.bgTransparency), clear = col & ~IM_COL32_A_MASK;
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    // The middle (a fan from the first point) and the fading ring around it.
    dl->PrimReserve((kPts - 2) * 3 + kPts * 6, kPts * 2);
    const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    for (int i = 0; i < kPts; ++i) dl->PrimWriteVtx(in[i], uv, col);
    for (int i = 0; i < kPts; ++i) dl->PrimWriteVtx(out[i], uv, clear);
    for (int i = 1; i + 1 < kPts; ++i) {
        dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base + i)); dl->PrimWriteIdx((ImDrawIdx)(base + i + 1));
    }
    for (int i = 0; i < kPts; ++i) {
        const int j = (i + 1) % kPts;
        const ImDrawIdx i0 = (ImDrawIdx)(base + i), i1 = (ImDrawIdx)(base + j);
        const ImDrawIdx o0 = (ImDrawIdx)(base + kPts + i), o1 = (ImDrawIdx)(base + kPts + j);
        dl->PrimWriteIdx(i0); dl->PrimWriteIdx(o0); dl->PrimWriteIdx(o1);
        dl->PrimWriteIdx(i0); dl->PrimWriteIdx(o1); dl->PrimWriteIdx(i1);
    }
}

unsigned g_backdrop[8] = {};
int      g_backdropCount = 0;

bool anyBlur(const SceneNode* n) {
    for (auto& c : n->children) {
        if (!c->isGui()) continue;
        if (c->gui.type == GuiType::UIBlur && c->enabled && c->gui.blurSize > 0) return true;
        if (c->isGuiObject() && c->visible && anyBlur(c.get())) return true;
    }
    return false;
}

// Lay out one object and everything inside it, in drawing order.
void layout(SceneNode* n, ImVec2 pa, ImVec2 pb, ImVec2 clipA, ImVec2 clipB, std::vector<Item>& out) {
    if (!n->isGuiObject() || !n->visible) return;
    const GuiProps& g = n->gui;
    const float pw = pb.x - pa.x, ph = pb.y - pa.y;
    const float w = pw * g.size.xs + g.size.xo, h = ph * g.size.ys + g.size.yo;
    const float x = pa.x + pw * g.pos.xs + g.pos.xo - g.anchor.x * w;
    const float y = pa.y + ph * g.pos.ys + g.pos.yo - g.anchor.y * h;
    Item it{n, ImVec2(x, y), ImVec2(x + w, y + h), clipA, clipB};
    n->gui.absPos = {x, y};
    n->gui.absSize = {w, h};
    out.push_back(it);
    ImVec2 ca = clipA, cb = clipB;
    if (g.clips) { ca = ImVec2(std::max(ca.x, it.a.x), std::max(ca.y, it.a.y)); cb = ImVec2(std::min(cb.x, it.b.x), std::min(cb.y, it.b.y)); }
    std::vector<SceneNode*> kids;
    for (auto& c : n->children) if (c->isGuiObject()) kids.push_back(c.get());
    std::stable_sort(kids.begin(), kids.end(), [](SceneNode* l, SceneNode* r) { return l->gui.zIndex < r->gui.zIndex; });
    for (SceneNode* c : kids) layout(c, it.a, it.b, ca, cb, out);
}

void findScreens(SceneNode* n, std::vector<SceneNode*>& out) {
    for (auto& c : n->children) {
        if (c->isGui()) {
            if (c->gui.type == GuiType::ScreenGui) out.push_back(c.get());
            continue;   // a ScreenGui doesn't hold other ScreenGuis
        }
        findScreens(c.get(), out);
    }
}

// Every visible UI object, in the order they're drawn (so the last one is on top).
std::vector<Item> items(Scene& scene, ImVec2 min, ImVec2 max) {
    std::vector<SceneNode*> screens;
    findScreens(scene.root(), screens);
    std::stable_sort(screens.begin(), screens.end(),
                     [](SceneNode* a, SceneNode* b) { return a->gui.displayOrder < b->gui.displayOrder; });
    std::vector<Item> out;
    for (SceneNode* s : screens) {
        if (!s->enabled) continue;
        s->gui.absPos = {min.x, min.y};
        s->gui.absSize = {max.x - min.x, max.y - min.y};
        std::vector<SceneNode*> kids;
        for (auto& c : s->children) if (c->isGuiObject()) kids.push_back(c.get());
        std::stable_sort(kids.begin(), kids.end(), [](SceneNode* l, SceneNode* r) { return l->gui.zIndex < r->gui.zIndex; });
        for (SceneNode* c : kids) layout(c, min, max, min, max, out);
    }
    return out;
}

ImVec2 g_lastMin(0, 0), g_lastMax(1280, 720);   // the screen we last drew on

bool inside(const Item& it, ImVec2 p) {
    ImVec2 a(std::max(it.a.x, it.clipA.x), std::max(it.a.y, it.clipA.y));
    ImVec2 b(std::min(it.b.x, it.clipB.x), std::min(it.b.y, it.clipB.y));
    return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y;
}

// Does this object catch the pointer (so clicks don't go through to the world)?
bool blocks(const SceneNode* n) {
    if (n->isGuiButton()) return true;
    return n->gui.bgTransparency < 0.95f || ((n->gui.type == GuiType::ImageLabel) && !n->gui.image.empty());
}

void drawText(ImDrawList* dl, const Item& it, const GuiProps& g, float radius) {
    if (g.text.empty() || g.textTransparency >= 1.0f) return;
    ImFont* font = ImGui::GetFont();
    const float w = it.b.x - it.a.x, h = it.b.y - it.a.y;
    const float pad = std::min(4.0f, radius * 0.5f + 2.0f);
    float wrap = g.textWrapped || g.textScaled ? std::max(1.0f, w - pad * 2) : 0.0f;
    float size = g.textSize;
    if (g.textScaled) {   // as big as fits
        size = std::max(6.0f, h * 0.85f);
        for (int i = 0; i < 24; ++i) {
            ImVec2 s = font->CalcTextSizeA(size, FLT_MAX, g.textWrapped ? wrap : 0.0f, g.text.c_str());
            if (s.x <= w - pad * 2 && s.y <= h) break;
            size *= 0.88f;
        }
        if (!g.textWrapped) wrap = 0.0f;
    }
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, wrap, g.text.c_str());
    float x = g.xAlign == 0 ? it.a.x + pad : g.xAlign == 2 ? it.b.x - pad - ts.x : it.a.x + (w - ts.x) * 0.5f;
    float y = g.yAlign == 0 ? it.a.y : g.yAlign == 2 ? it.b.y - ts.y : it.a.y + (h - ts.y) * 0.5f;
    const ImU32 col = rgba(g.textColor, g.textTransparency);
    if (g.strokeTransparency < 1.0f) {   // an outline around the letters
        const ImU32 sc = rgba(g.strokeColor, std::max(g.strokeTransparency, g.textTransparency));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx || dy) dl->AddText(font, size, ImVec2(x + dx, y + dy), sc, g.text.c_str(), nullptr, wrap);
    }
    dl->AddText(font, size, ImVec2(x, y), col, g.text.c_str(), nullptr, wrap);
    if (g.bold) dl->AddText(font, size, ImVec2(x + 1, y), col, g.text.c_str(), nullptr, wrap);
}

} // namespace

void refresh(Scene& scene) { items(scene, g_lastMin, g_lastMax); }

bool needsBackdrop(Scene& scene) {
    std::vector<SceneNode*> screens;
    findScreens(scene.root(), screens);
    for (SceneNode* s : screens) if (s->enabled && anyBlur(s)) return true;
    return false;
}

void setBackdrop(const unsigned* levels, int count) {
    g_backdropCount = levels ? std::clamp(count, 0, 8) : 0;
    for (int i = 0; i < g_backdropCount; ++i) g_backdrop[i] = levels[i];
}

void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const Input* input, uint64_t selected) {
    g_lastMin = min;
    g_lastMax = max;
    const std::vector<Item> list = items(scene, min, max);
    for (const Item& it : list) {
        const SceneNode* n = it.node;
        const GuiProps& g = n->gui;
        ImVec2 ca(std::max(it.clipA.x, min.x), std::max(it.clipA.y, min.y));
        ImVec2 cb(std::min(it.clipB.x, max.x), std::min(it.clipB.y, max.y));
        if (cb.x <= ca.x || cb.y <= ca.y) continue;
        dl->PushClipRect(ca, cb, true);
        const float w = it.b.x - it.a.x, h = it.b.y - it.a.y;
        const Radii radii = radiiOf(n, w, h);
        const float radius = std::max(std::max(radii.r[0], radii.r[1]), std::max(radii.r[2], radii.r[3]));

        // A soft shadow under it, then the world behind it blurred (frosted glass).
        if (const SceneNode* s = childOfType(n, GuiType::UIShadow)) drawShadow(dl, it.a, it.b, radii, s->gui);
        if (const SceneNode* b = childOfType(n, GuiType::UIBlur); b && b->gui.blurSize > 0 && g_backdropCount > 0) {
            const float sz = b->gui.blurSize;
            const int level = std::min(g_backdropCount - 1, sz <= 6 ? 0 : sz <= 12 ? 1 : sz <= 24 ? 2 : 3);
            const float sw = max.x - min.x, sh = max.y - min.y;
            if (sw > 0 && sh > 0 && g_backdrop[level]) {   // (the world's picture is upside down, like the scene's)
                const ImVec2 uv0((it.a.x - min.x) / sw, 1.0f - (it.a.y - min.y) / sh);
                const ImVec2 uv1((it.b.x - min.x) / sw, 1.0f - (it.b.y - min.y) / sh);
                imageRounded(dl, g_backdrop[level], it.a, it.b, uv0, uv1, IM_COL32_WHITE, radii);
            }
        }

        // Background (buttons get darker when you point at or press them).
        glm::vec3 bg = g.bg;
        if (n->isGuiButton() && g.autoButtonColor && input) {
            if (input->pressed == n->id && input->hovered == n->id) bg *= 0.7f;
            else if (input->hovered == n->id) bg *= 0.87f;
        }
        if (g.bgTransparency < 1.0f) fillRounded(dl, it.a, it.b, rgba(bg, g.bgTransparency), radii);

        // A picture.
        if ((g.type == GuiType::ImageLabel || g.type == GuiType::ImageButton) && !g.image.empty() && g.imageTransparency < 1.0f)
            if (unsigned tex = Textures::get(g.image)) {
                glm::vec3 tint = g.imageColor;
                if (n->isGuiButton() && g.autoButtonColor && input && input->hovered == n->id && g.bgTransparency >= 1.0f)
                    tint *= input->pressed == n->id ? 0.7f : 0.87f;
                imageRounded(dl, tex, it.a, it.b, ImVec2(0, 1), ImVec2(1, 0), rgba(tint, g.imageTransparency), radii);
            }

        drawText(dl, it, g, radius);

        // The edge: a UIStroke inside it, else the border.
        if (const SceneNode* s = childOfType(n, GuiType::UIStroke)) {
            if (s->gui.thickness > 0 && s->gui.bgTransparency < 1.0f)
                strokeRounded(dl, it.a, it.b, rgba(s->gui.borderColor, s->gui.bgTransparency), radii, s->gui.thickness);
        } else if (g.border > 0 && g.bgTransparency < 1.0f) {
            strokeRounded(dl, ImVec2(it.a.x - g.border * 0.5f, it.a.y - g.border * 0.5f),
                          ImVec2(it.b.x + g.border * 0.5f, it.b.y + g.border * 0.5f),
                          rgba(g.borderColor, g.bgTransparency), radii, (float)g.border);
        }
        dl->PopClipRect();
    }
    if (selected)   // Studio: show which one is selected
        for (const Item& it : list)
            if (it.node->id == selected) {
                dl->AddRect(ImVec2(it.a.x - 2, it.a.y - 2), ImVec2(it.b.x + 2, it.b.y + 2), IM_COL32(40, 150, 255, 255), 0, 0, 2.0f);
                dl->AddRectFilled(ImVec2(it.b.x - 4, it.b.y - 4), ImVec2(it.b.x + 4, it.b.y + 4), IM_COL32(40, 150, 255, 255));
            }
}

SceneNode* pick(Scene& scene, ImVec2 min, ImVec2 max, ImVec2 p, bool buttonsOnly) {
    const std::vector<Item> list = items(scene, min, max);
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (inside(*it, p) && (!buttonsOnly || it->node->isGuiButton())) return it->node;
    return nullptr;
}

bool rectOf(Scene& scene, ImVec2 min, ImVec2 max, const SceneNode* node, ImVec2& a, ImVec2& b) {
    for (const Item& it : items(scene, min, max))
        if (it.node == node) { a = it.a; b = it.b; return true; }
    return false;
}

bool handle(Scene& scene, ImVec2 min, ImVec2 max, ImVec2 pointer, bool in, bool down, bool up, bool tapped,
            Input& input, std::vector<Event>& events) {
    SceneNode* top = nullptr;
    if (in) {
        const std::vector<Item> list = items(scene, min, max);
        for (auto it = list.rbegin(); it != list.rend(); ++it)
            if (inside(*it, pointer) && blocks(it->node)) { top = it->node; break; }
    }
    const uint64_t button = top && top->isGuiButton() ? top->id : 0;
    if (button != input.hovered) {
        if (input.hovered) events.push_back({EventKind::Leave, input.hovered});
        if (button) events.push_back({EventKind::Enter, button});
        input.hovered = button;
    }
    if (tapped) {   // a finger: a tap on a button presses it straight away
        if (button) events.push_back({EventKind::Click, button});
        input.pressed = 0;
    } else {
        if (down) input.pressed = button;
        if (up) {
            if (input.pressed && input.pressed == button) events.push_back({EventKind::Click, button});
            input.pressed = 0;
        }
    }
    return top != nullptr;
}

} // namespace GameGui
