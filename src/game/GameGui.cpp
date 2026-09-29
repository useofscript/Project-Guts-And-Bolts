#include "GameGui.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../renderer/Textures.h"

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
        if (c->isGui() && c->gui.type == t && (t != GuiType::UIStroke || c->enabled)) return c.get();
    return nullptr;
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
        float radius = 0.0f;
        if (const SceneNode* c = childOfType(n, GuiType::UICorner))
            radius = std::clamp(c->gui.corner.xs * std::min(w, h) + c->gui.corner.xo, 0.0f, std::min(w, h) * 0.5f);

        // Background (buttons get darker when you point at or press them).
        glm::vec3 bg = g.bg;
        if (n->isGuiButton() && g.autoButtonColor && input) {
            if (input->pressed == n->id && input->hovered == n->id) bg *= 0.7f;
            else if (input->hovered == n->id) bg *= 0.87f;
        }
        if (g.bgTransparency < 1.0f) dl->AddRectFilled(it.a, it.b, rgba(bg, g.bgTransparency), radius);

        // A picture.
        if ((g.type == GuiType::ImageLabel || g.type == GuiType::ImageButton) && !g.image.empty() && g.imageTransparency < 1.0f)
            if (unsigned tex = Textures::get(g.image)) {
                glm::vec3 tint = g.imageColor;
                if (n->isGuiButton() && g.autoButtonColor && input && input->hovered == n->id && g.bgTransparency >= 1.0f)
                    tint *= input->pressed == n->id ? 0.7f : 0.87f;
                dl->AddImageRounded((ImTextureID)(intptr_t)tex, it.a, it.b, ImVec2(0, 1), ImVec2(1, 0),
                                    rgba(tint, g.imageTransparency), radius);
            }

        drawText(dl, it, g, radius);

        // The edge: a UIStroke inside it, else the border.
        if (const SceneNode* s = childOfType(n, GuiType::UIStroke)) {
            if (s->gui.thickness > 0 && s->gui.bgTransparency < 1.0f)
                dl->AddRect(it.a, it.b, rgba(s->gui.borderColor, s->gui.bgTransparency), radius, 0, s->gui.thickness);
        } else if (g.border > 0 && g.bgTransparency < 1.0f) {
            dl->AddRect(ImVec2(it.a.x - g.border * 0.5f, it.a.y - g.border * 0.5f),
                        ImVec2(it.b.x + g.border * 0.5f, it.b.y + g.border * 0.5f),
                        rgba(g.borderColor, g.bgTransparency), radius, 0, (float)g.border);
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
