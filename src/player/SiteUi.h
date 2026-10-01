#pragma once
#include "../renderer/Textures.h"
// The Guts&Bolts site's look (2011-style): colours, buttons and drawings shared
// by the site's pages (PlayerApp.cpp, PlayerOnline.cpp).
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include "../game/Catalog.h"
#include "../online/AssetCache.h"
#include <set>

namespace Site {

inline const ImVec4 kAccent   = {0.26f, 0.55f, 0.96f, 1.0f};
inline const ImVec4 kGreen    = {0.20f, 0.68f, 0.32f, 1.0f};
inline const ImVec4 kCardBg   = {0.16f, 0.17f, 0.20f, 1.0f};
inline const ImVec4 kTopBarBg = {0.086f, 0.094f, 0.114f, 1.0f};

inline bool bigButton(const char* label, ImVec4 col, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, col);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x + 0.08f, col.y + 0.08f, col.z + 0.08f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(col.x - 0.05f, col.y - 0.05f, col.z - 0.05f, 1));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return r;
}

// Inside a popup: was it just tapped / clicked outside? (Phones have no Esc key, so
// tapping the dark area around a popup should close it.) Only for the top popup,
// and not on the frame it opened (that click is the one that opened it).
inline bool tappedOutside() {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (ImGui::IsWindowAppearing() || g.OpenPopupStack.empty() || g.OpenPopupStack.back().Window != w) return false;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
    ImVec2 m = ImGui::GetIO().MousePos;
    return m.x < w->Pos.x || m.y < w->Pos.y || m.x > w->Pos.x + w->Size.x || m.y > w->Pos.y + w->Size.y;
}

// Tall (portrait) phone screen?
inline bool portraitScreen() { const ImVec2 d = ImGui::GetIO().DisplaySize; return d.y > d.x; }
// A dialog width that still fits on a narrow phone screen.
inline float fitWidth(float want) { return std::min(want, ImGui::GetIO().DisplaySize.x - 24.0f); }


// --- 2011-style look -----------------------------------------------------------
namespace Classic {
inline const ImU32  kSkyTop    = IM_COL32(22, 70, 148, 255);
inline const ImU32  kSkyBottom = IM_COL32(110, 170, 232, 255);
inline const ImU32  kNavTop    = IM_COL32(64, 146, 232, 255);
inline const ImU32  kNavBottom = IM_COL32(16, 96, 186, 255);
inline const ImU32  kStripeA   = IM_COL32(255, 255, 255, 255);
inline const ImU32  kStripeB   = IM_COL32(236, 239, 244, 255);
inline const ImVec4 kInk       = {0.16f, 0.17f, 0.20f, 1.0f};
inline const ImVec4 kInkDim    = {0.42f, 0.44f, 0.50f, 1.0f};
inline const ImVec4 kLink      = {0.02f, 0.33f, 0.74f, 1.0f};
inline const ImVec4 kPlay      = {0.02f, 0.66f, 0.30f, 1.0f};
inline const ImVec4 kBlue      = {0.10f, 0.45f, 0.82f, 1.0f};

// Dark text and light widgets for the white striped panel.
inline void pushLight() {
    ImGui::PushStyleColor(ImGuiCol_Text, kInk);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, kInkDim);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.93f, 0.96f, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.88f, 0.93f, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.93f, 0.93f, 0.94f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.91f, 0.98f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.86f, 0.97f, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.72f, 0.74f, 0.78f, 1));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.78f, 0.8f, 0.84f, 1));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, kBlue);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.9f, 0.91f, 0.93f, 1));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.7f, 0.72f, 0.76f, 1));
    // Popups, dropdowns and tooltips opened on the light pages are light too (the
    // dark default boxes with this dark text were unreadable).
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1, 1, 1, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.86f, 0.91f, 0.98f, 1));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.80f, 0.88f, 0.98f, 1));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.72f, 0.83f, 0.97f, 1));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.06f, 0.38f, 0.73f, 1));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.10f, 0.45f, 0.82f, 1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, kBlue);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
}
inline void popLight() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(22);
}

inline bool button(const char* label, ImVec4 col, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(col.x * 0.7f, col.y * 0.7f, col.z * 0.7f, 1));
    bool r = bigButton(label, col, size);
    ImGui::PopStyleColor(2);
    return r;
}

inline void stripes(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    dl->AddRectFilled(a, b, kStripeB);
    dl->PushClipRect(a, b, true);
    float h = b.y - a.y;
    for (float x = a.x - h; x < b.x; x += 18.0f)
        dl->AddLine(ImVec2(x, b.y), ImVec2(x + h, a.y), kStripeA, 9.0f);
    dl->PopClipRect();
    dl->AddRect(a, b, IM_COL32(150, 160, 180, 255));
}

// Big chunky logo text with an outline, like the old logo.
inline void logo(ImDrawList* dl, ImVec2 p, float size, const char* text) {
    ImFont* f = ImGui::GetFont();
    for (int dx = -3; dx <= 3; ++dx)
        for (int dy = -3; dy <= 3; ++dy)
            if (dx * dx + dy * dy >= 4)
                dl->AddText(f, size, ImVec2(p.x + dx, p.y + dy + 2), IM_COL32(40, 10, 10, 255), text);
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            dl->AddText(f, size, ImVec2(p.x + dx, p.y + dy), IM_COL32(255, 255, 255, 255), text);
    dl->AddText(f, size, p, IM_COL32(222, 34, 28, 255), text);
}
} // namespace Classic
// An item's picture ("gb:<id>"): asks the server for it the first time (it shows
// up by itself once it has downloaded).
inline unsigned itemPicture(const std::string& image) {
    if (image.empty()) return 0;
    unsigned tex = Textures::get(image);
    static std::set<std::string> asked;
    if (!tex && image.rfind("gb:", 0) == 0 && asked.insert(image).second) Online::download(image.substr(3));
    return tex;
}

// Shirts and pants made from the template: a grey mannequin seen from the front,
// wearing the real picture (the same boxes of the template the game uses).
inline bool drawClothingOnMannequin(ImDrawList* dl, ImVec2 c, float s, const Catalog::Item& it) {
    const bool shirt = it.type == Catalog::Type::Shirt;
    if (it.image.empty() || (!shirt && it.type != Catalog::Type::Pants)) return false;
    unsigned tex = itemPicture(it.image);
    if (!tex) return false;
    const float W = 585.0f, H = 559.0f;   // the template (see PlayerModel.cpp)
    auto part = [&](ImVec2 a, ImVec2 b, ImU32 body, float x, float y, float w, float h, bool dressed) {
        dl->AddRectFilled(a, b, body);
        // Pictures are loaded bottom row first, so the template's top is v = 1.
        if (dressed) dl->AddImage((ImTextureID)(intptr_t)tex, a, b, ImVec2(x / W, 1.0f - y / H), ImVec2((x + w) / W, 1.0f - (y + h) / H));
    };
    const float u = s * 0.19f;   // half a torso
    const ImU32 grey = IM_COL32(205, 207, 212, 255), legGrey = IM_COL32(190, 192, 198, 255);
    const float top = c.y - s * 0.44f;
    dl->AddRectFilled(ImVec2(c.x - u * 0.6f, top), ImVec2(c.x + u * 0.6f, top + u * 1.1f), grey, u * 0.25f);   // head
    const float ty = top + u * 1.2f;
    part(ImVec2(c.x - u, ty), ImVec2(c.x + u, ty + 2 * u), grey, 231, 74, 128, 128, true);                     // torso
    part(ImVec2(c.x - 2 * u, ty), ImVec2(c.x - u, ty + 2 * u), grey, 217, 355, 64, 128, shirt);               // right arm (on the left as you look)
    part(ImVec2(c.x + u, ty), ImVec2(c.x + 2 * u, ty + 2 * u), grey, 308, 355, 64, 128, shirt);               // left arm
    part(ImVec2(c.x - u, ty + 2 * u), ImVec2(c.x, ty + 4 * u), legGrey, 217, 355, 64, 128, !shirt);          // right leg
    part(ImVec2(c.x, ty + 2 * u), ImVec2(c.x + u, ty + 4 * u), legGrey, 308, 355, 64, 128, !shirt);          // left leg
    return true;
}

// A simple picture of a catalog item, drawn in its colour.
inline void drawItemIcon(ImDrawList* dl, ImVec2 c, float s, const Catalog::Item& it) {
    if (drawClothingOnMannequin(dl, c, s, it)) return;
    ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(it.color.r, it.color.g, it.color.b, 1));
    ImU32 line = IM_COL32(40, 40, 50, 255);
    float t = std::max(1.5f, s * 0.02f);
    switch (it.type) {
    case Catalog::Type::Hat:
        if (it.hat == HatStyle::TopHat) {
            ImVec2 a(c.x - s * 0.2f, c.y - s * 0.3f), b(c.x + s * 0.2f, c.y + s * 0.15f);
            dl->AddRectFilled(a, b, fill); dl->AddRect(a, b, line, 0, 0, t);
            ImVec2 ba(c.x - s * 0.38f, c.y + s * 0.15f), bb(c.x + s * 0.38f, c.y + s * 0.24f);
            dl->AddRectFilled(ba, bb, fill, 4); dl->AddRect(ba, bb, line, 4, 0, t);
        } else if (it.hat == HatStyle::Crown) {
            ImVec2 pts[] = {{c.x - s * 0.34f, c.y + s * 0.2f}, {c.x - s * 0.34f, c.y - s * 0.22f}, {c.x - s * 0.17f, c.y - s * 0.02f},
                            {c.x, c.y - s * 0.3f}, {c.x + s * 0.17f, c.y - s * 0.02f}, {c.x + s * 0.34f, c.y - s * 0.22f},
                            {c.x + s * 0.34f, c.y + s * 0.2f}};
            for (int i = 1; i < 6; ++i) dl->AddTriangleFilled(pts[0], pts[i], pts[i + 1], fill);
            dl->AddPolyline(pts, 7, line, ImDrawFlags_Closed, t);
        } else {   // cap
            dl->PathArcTo(ImVec2(c.x, c.y + s * 0.1f), s * 0.3f, 3.14159f, 6.28318f, 24);
            dl->PathFillConvex(fill);
            dl->PathArcTo(ImVec2(c.x, c.y + s * 0.1f), s * 0.3f, 3.14159f, 6.28318f, 24);
            dl->PathStroke(line, 0, t);
            ImVec2 va(c.x, c.y + s * 0.06f), vb(c.x + s * 0.46f, c.y + s * 0.14f);
            dl->AddRectFilled(va, vb, fill, 3); dl->AddRect(va, vb, line, 3, 0, t);
        }
        break;
    case Catalog::Type::Shirt: {
        ImVec2 pts[] = {{c.x - s * 0.18f, c.y - s * 0.32f}, {c.x + s * 0.18f, c.y - s * 0.32f}, {c.x + s * 0.4f, c.y - s * 0.12f},
                        {c.x + s * 0.3f, c.y + s * 0.0f}, {c.x + s * 0.22f, c.y - s * 0.06f}, {c.x + s * 0.22f, c.y + s * 0.34f},
                        {c.x - s * 0.22f, c.y + s * 0.34f}, {c.x - s * 0.22f, c.y - s * 0.06f}, {c.x - s * 0.3f, c.y + s * 0.0f},
                        {c.x - s * 0.4f, c.y - s * 0.12f}};
        dl->AddRectFilled(ImVec2(c.x - s * 0.22f, c.y - s * 0.32f), ImVec2(c.x + s * 0.22f, c.y + s * 0.34f), fill);
        dl->AddTriangleFilled(pts[1], pts[2], pts[3], fill); dl->AddTriangleFilled(pts[1], pts[3], pts[4], fill);
        dl->AddTriangleFilled(pts[0], pts[9], pts[8], fill); dl->AddTriangleFilled(pts[0], pts[8], pts[7], fill);
        dl->AddPolyline(pts, 10, line, ImDrawFlags_Closed, t);
        break;
    }
    case Catalog::Type::Pants: {
        // Waistband plus two legs.
        ImVec2 w0(c.x - s * 0.25f, c.y - s * 0.34f), w1(c.x + s * 0.25f, c.y - s * 0.22f);
        ImVec2 l0(c.x - s * 0.25f, c.y - s * 0.22f), l1(c.x - s * 0.02f, c.y + s * 0.36f);
        ImVec2 r0(c.x + s * 0.02f, c.y - s * 0.22f), r1(c.x + s * 0.25f, c.y + s * 0.36f);
        ImVec2 mid0(c.x - s * 0.03f, c.y - s * 0.22f), mid1(c.x + s * 0.03f, c.y - s * 0.05f);
        for (auto [a, b] : {std::pair{w0, w1}, std::pair{l0, l1}, std::pair{r0, r1}, std::pair{mid0, mid1}})
            dl->AddRectFilled(a, b, fill);
        dl->AddRect(w0, w1, line, 0, 0, t);
        dl->AddRect(l0, l1, line, 0, 0, t);
        dl->AddRect(r0, r1, line, 0, 0, t);
        break;
    }
    case Catalog::Type::Gear: {   // a little sword, tilted
        const ImU32 blade = IM_COL32(200, 205, 215, 255), hilt = IM_COL32(120, 80, 40, 255), guard = IM_COL32(230, 180, 40, 255);
        ImVec2 tip(c.x + s * 0.3f, c.y - s * 0.3f), base(c.x - s * 0.12f, c.y + s * 0.12f);
        const float bw = s * 0.06f;
        ImVec2 q[] = {{tip.x, tip.y}, {base.x + bw, base.y + bw}, {base.x - bw, base.y - bw}};
        ImVec2 blade4[] = {{tip.x, tip.y}, {base.x + bw, base.y + bw * 0.2f}, {base.x - bw * 0.2f, base.y - bw}};
        dl->AddTriangleFilled(q[0], q[1], q[2], blade);
        dl->AddPolyline(blade4, 3, line, ImDrawFlags_Closed, t);
        dl->AddLine(ImVec2(base.x - s * 0.13f, base.y - s * 0.01f), ImVec2(base.x + s * 0.01f, base.y + s * 0.13f), guard, s * 0.06f);
        dl->AddLine(ImVec2(base.x - s * 0.02f, base.y + s * 0.02f), ImVec2(base.x - s * 0.18f, base.y + s * 0.18f), hilt, s * 0.06f);
        dl->AddCircleFilled(ImVec2(base.x - s * 0.2f, base.y + s * 0.2f), s * 0.045f, guard);
        break;
    }
    case Catalog::Type::TShirt: {   // a white tee with its picture on the front
        ImVec2 pts[] = {{c.x - s * 0.18f, c.y - s * 0.32f}, {c.x + s * 0.18f, c.y - s * 0.32f}, {c.x + s * 0.4f, c.y - s * 0.12f},
                        {c.x + s * 0.3f, c.y + s * 0.0f}, {c.x + s * 0.22f, c.y - s * 0.06f}, {c.x + s * 0.22f, c.y + s * 0.34f},
                        {c.x - s * 0.22f, c.y + s * 0.34f}, {c.x - s * 0.22f, c.y - s * 0.06f}, {c.x - s * 0.3f, c.y + s * 0.0f},
                        {c.x - s * 0.4f, c.y - s * 0.12f}};
        const ImU32 white = IM_COL32(250, 250, 250, 255);
        dl->AddRectFilled(ImVec2(c.x - s * 0.22f, c.y - s * 0.32f), ImVec2(c.x + s * 0.22f, c.y + s * 0.34f), white);
        dl->AddTriangleFilled(pts[1], pts[2], pts[3], white); dl->AddTriangleFilled(pts[1], pts[3], pts[4], white);
        dl->AddTriangleFilled(pts[0], pts[9], pts[8], white); dl->AddTriangleFilled(pts[0], pts[8], pts[7], white);
        dl->AddPolyline(pts, 10, line, ImDrawFlags_Closed, t);
        const ImVec2 a(c.x - s * 0.16f, c.y - s * 0.16f), b(c.x + s * 0.16f, c.y + s * 0.16f);
        if (unsigned tex = itemPicture(it.image))
            dl->AddImage((ImTextureID)(intptr_t)tex, a, b, ImVec2(0, 1), ImVec2(1, 0));
        else dl->AddRectFilled(a, b, IM_COL32(210, 214, 222, 255), 3);
        break;
    }
    case Catalog::Type::Face: {   // a smiley on a yellow head
        dl->AddRectFilled(ImVec2(c.x - s * 0.34f, c.y - s * 0.34f), ImVec2(c.x + s * 0.34f, c.y + s * 0.34f), IM_COL32(245, 211, 59, 255), s * 0.1f);
        dl->AddCircleFilled(ImVec2(c.x - s * 0.12f, c.y - s * 0.08f), s * 0.05f, line);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.12f, c.y - s * 0.08f), s * 0.05f, line);
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.02f), s * 0.18f, 0.5f, 2.64f, 16);
        dl->PathStroke(line, 0, t * 2);
        break;
    }
    case Catalog::Type::Hair: case Catalog::Type::FaceAcc: case Catalog::Type::Neck:
    case Catalog::Type::Shoulder: case Catalog::Type::Waist:   // made in Studio: a gem in its colour
        dl->AddCircleFilled(c, s * 0.3f, fill, 32);
        dl->AddCircle(c, s * 0.3f, line, 32, t);
        dl->AddCircleFilled(c, s * 0.14f, IM_COL32(255, 255, 255, 90), 24);
        break;
    default: break;
    }
}

} // namespace Site
