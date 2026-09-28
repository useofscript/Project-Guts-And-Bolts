#include "Hud.h"
#include "../scene/Scene.h"
#include "../scene/Player.h"
#include "../scripting/ScriptEngine.h"
#include "Badges.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Hud {

namespace {
void shadowText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text) {
    dl->AddText(font, size, ImVec2(pos.x + 2, pos.y + 2), IM_COL32(0, 0, 0, 160), text);
    dl->AddText(font, size, pos, col, text);
}
} // namespace

void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const GuiState& gui, float topOffset, bool character) {
    ImFont* font = ImGui::GetFont();
    float   base = ImGui::GetFontSize();

    // --- Health bar (top-right) ---
    if (Player* p = character ? scene.player() : nullptr) {
        const Humanoid& h = p->humanoid();
        float frac = h.maxHealth > 0 ? std::clamp(h.health / h.maxHealth, 0.0f, 1.0f) : 0.0f;
        ImVec2 size(180, 16);
        ImVec2 a(max.x - size.x - 16, min.y + 16), b(a.x + size.x, a.y + size.y);
        dl->AddRectFilled(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), IM_COL32(0, 0, 0, 140), 6);
        ImU32 col = frac > 0.5f ? IM_COL32(80, 200, 90, 255)
                  : frac > 0.2f ? IM_COL32(230, 190, 50, 255) : IM_COL32(220, 60, 60, 255);
        dl->AddRectFilled(a, ImVec2(a.x + size.x * frac, b.y), col, 4);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f / %.0f", h.health, h.maxHealth);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(a.x + (size.x - ts.x) * 0.5f, a.y + (size.y - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 230), buf);

        if (p->isDead()) {
            const char* oof = "You died!";
            float big = base * 3.0f;
            ImVec2 s = font->CalcTextSizeA(big, FLT_MAX, 0, oof);
            ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.4f);
            shadowText(dl, font, big, ImVec2(c.x - s.x * 0.5f, c.y - s.y * 0.5f), IM_COL32(255, 90, 80, 255), oof);
            char r[48];
            std::snprintf(r, sizeof(r), "Respawning in %.0f...", std::max(0.0f, p->respawnIn()) + 0.49f);
            ImVec2 rs = ImGui::CalcTextSize(r);
            shadowText(dl, font, base, ImVec2(c.x - rs.x * 0.5f, c.y + s.y * 0.6f), IM_COL32(255, 255, 255, 230), r);
        }
    }

    // --- Script labels (top-left) ---
    float y = min.y + 14 + topOffset;
    for (const auto& [key, text] : gui.labels) {
        ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        dl->AddRectFilled(ImVec2(min.x + 12, y - 4), ImVec2(min.x + 24 + ts.x, y + ts.y + 4),
                          IM_COL32(0, 0, 0, 130), 5);
        dl->AddText(ImVec2(min.x + 18, y), IM_COL32(255, 255, 255, 240), text.c_str());
        y += ts.y + 12;
    }

    // --- Big message (centre) ---
    if (!gui.message.empty()) {
        float big = base * 2.0f;
        ImVec2 s = font->CalcTextSizeA(big, FLT_MAX, 0, gui.message.c_str());
        // Touch screens: a bit lower, clear of the buttons and the player list.
        ImVec2 c((min.x + max.x) * 0.5f, min.y + (max.y - min.y) * (topOffset > 0.0f ? 0.34f : 0.22f));
        dl->AddRectFilled(ImVec2(c.x - s.x * 0.5f - 18, c.y - s.y * 0.5f - 10),
                          ImVec2(c.x + s.x * 0.5f + 18, c.y + s.y * 0.5f + 10), IM_COL32(0, 0, 0, 140), 8);
        shadowText(dl, font, big, ImVec2(c.x - s.x * 0.5f, c.y - s.y * 0.5f), IM_COL32(255, 255, 255, 255),
                   gui.message.c_str());
    }
}

void drawBubbles(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const glm::mat4& viewProj,
                 const std::unordered_map<std::string, std::pair<std::string, float>>& bubbles) {
    if (bubbles.empty()) return;
    auto drawFor = [&](SceneNode* root) {
        if (!root) return;
        auto it = bubbles.find(root->name);
        if (it == bubbles.end()) return;
        SceneNode* head = root->findChild("Head");
        glm::vec3 p = head ? glm::vec3(head->worldMatrix()[3]) : root->transform.position + glm::vec3(0, 2.4f, 0);
        glm::vec4 c = viewProj * glm::vec4(p + glm::vec3(0, 1.1f, 0), 1.0f);
        if (c.w <= 0.1f) return;
        glm::vec2 ndc = glm::vec2(c) / c.w;
        ImVec2 sp(min.x + (ndc.x * 0.5f + 0.5f) * (max.x - min.x), min.y + (0.5f - ndc.y * 0.5f) * (max.y - min.y));
        const std::string& text = it->second.first;
        float wrap = 220.0f;
        ImVec2 ts = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
        float alpha = std::min(1.0f, it->second.second);
        ImVec2 a(sp.x - ts.x * 0.5f - 10, sp.y - ts.y - 16), b(sp.x + ts.x * 0.5f + 10, sp.y - 6);
        dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (int)(235 * alpha)), 10.0f);
        dl->AddTriangleFilled(ImVec2(sp.x - 7, b.y), ImVec2(sp.x + 7, b.y), ImVec2(sp.x, b.y + 8),
                              IM_COL32(255, 255, 255, (int)(235 * alpha)));
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + 10, a.y + 5),
                    IM_COL32(20, 20, 25, (int)(255 * alpha)), text.c_str(), nullptr, wrap);
    };
    if (Player* p = scene.player()) drawFor(p->root());
    for (auto& rc : scene.remotes()) drawFor(scene.findById(rc.rootId));
}

namespace {
constexpr float kSlot = 58.0f, kGap = 6.0f;
ImVec2 hotbarStart(ImVec2 min, ImVec2 max, int n) {
    float w = n * kSlot + (n - 1) * kGap;
    return ImVec2(std::floor((min.x + max.x - w) * 0.5f), max.y - kSlot - 14.0f);
}
int hotbarCount(Scene& scene) {
    Player* p = scene.player();
    return p && !p->isDead() ? (int)p->tools().size() : 0;
}
} // namespace

bool overHotbar(ImVec2 min, ImVec2 max, Scene& scene, ImVec2 p) {
    int n = hotbarCount(scene);
    if (!n) return false;
    ImVec2 a = hotbarStart(min, max, n);
    return p.x >= a.x && p.x <= a.x + n * (kSlot + kGap) && p.y >= a.y && p.y <= a.y + kSlot;
}

int drawHotbar(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const ImVec2* tap) {
    Player* p = scene.player();
    int n = hotbarCount(scene);
    if (!n) return -1;
    std::vector<SceneNode*> list = p->tools();
    SceneNode* held = p->equippedTool();
    ImVec2 a = hotbarStart(min, max, n);
    ImVec2 mouse = ImGui::GetIO().MousePos;
    int clicked = -1;
    ImFont* font = ImGui::GetFont();
    for (int i = 0; i < n; ++i) {
        SceneNode* t = list[(size_t)i];
        ImVec2 s0(a.x + i * (kSlot + kGap), a.y), s1(s0.x + kSlot, s0.y + kSlot);
        bool on = t == held;
        bool hover = mouse.x >= s0.x && mouse.x < s1.x && mouse.y >= s0.y && mouse.y < s1.y;
        dl->AddRectFilled(s0, s1, on ? IM_COL32(245, 245, 245, 215) : IM_COL32(20, 22, 28, hover ? 200 : 160), 7.0f);
        dl->AddRect(s0, s1, on ? IM_COL32(40, 140, 255, 255) : IM_COL32(255, 255, 255, 60), 7.0f, 0, on ? 3.0f : 1.0f);
        // A little picture: the Handle's colour as a diagonal stick.
        if (SceneNode* h = t->findChild("Handle")) {
            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(h->color.r, h->color.g, h->color.b, 1.0f));
            dl->AddLine(ImVec2(s0.x + kSlot * 0.3f, s1.y - kSlot * 0.3f), ImVec2(s1.x - kSlot * 0.28f, s0.y + kSlot * 0.26f), col, 6.0f);
        }
        char num[4];
        std::snprintf(num, sizeof(num), "%d", i + 1);
        dl->AddText(ImVec2(s0.x + 5, s0.y + 3), on ? IM_COL32(30, 30, 40, 255) : IM_COL32(255, 255, 255, 200), num);
        // The name along the bottom, cut to fit.
        std::string name = t->name;
        while (name.size() > 1 && ImGui::CalcTextSize(name.c_str()).x > kSlot - 6) name.pop_back();
        ImVec2 ts = ImGui::CalcTextSize(name.c_str());
        dl->AddText(font, ImGui::GetFontSize(), ImVec2(s0.x + (kSlot - ts.x) * 0.5f, s1.y - ts.y - 3),
                    on ? IM_COL32(20, 20, 30, 255) : IM_COL32(255, 255, 255, 235), name.c_str());
        if (hover) {
            const std::string& tip = t->toolTip.empty() ? t->name : t->toolTip;
            ImVec2 tt = ImGui::CalcTextSize(tip.c_str());
            ImVec2 b0(s0.x + (kSlot - tt.x) * 0.5f - 6, s0.y - tt.y - 12);
            dl->AddRectFilled(b0, ImVec2(b0.x + tt.x + 12, b0.y + tt.y + 6), IM_COL32(20, 22, 28, 220), 4.0f);
            dl->AddText(ImVec2(b0.x + 6, b0.y + 3), IM_COL32(255, 255, 255, 255), tip.c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = i;
        }
        if (tap && tap->x >= s0.x && tap->x < s1.x && tap->y >= s0.y && tap->y < s1.y) clicked = i;
    }
    return clicked;
}

void drawPlayerList(ImDrawList* dl, ImVec2 min, ImVec2 max, const std::vector<PlayerEntry>& players) {
    if (players.empty()) return;
    const float rowH = 22.0f, w = 200.0f;
    float y = min.y + 50;
    float x = max.x - w - 16;
    dl->AddRectFilled(ImVec2(x - 4, y - 4), ImVec2(x + w + 4, y + 24 + players.size() * rowH),
                      IM_COL32(0, 0, 0, 120), 6.0f);
    dl->AddText(ImVec2(x + 4, y), IM_COL32(255, 200, 120, 255), "Players");
    y += 24;
    float t = (float)ImGui::GetTime();
    for (size_t i = 0; i < players.size(); ++i) {
        const PlayerEntry& p = players[i];
        float tx = x + 4;
        if (p.admin) {
            // The Administrator badge bobs gently next to the name.
            float bob = std::sin(t * 2.4f + (float)i) * 2.0f;
            ImVec2 c(x + 12, y + 8 + bob);
            dl->AddCircleFilled(c, 11.0f, IM_COL32(255, 60, 60, 40), 20);   // soft glow
            Badges::drawIcon(dl, c, 17.0f, Badges::Id::Administrator);
            tx = x + 26;
        }
        dl->AddText(ImVec2(tx, y), p.admin ? IM_COL32(255, 225, 120, 255) : IM_COL32(255, 255, 255, 230),
                    p.name.c_str());
        if (p.verified || p.admin) {   // blue check after Verified names (staff are always verified)
            float nw = ImGui::CalcTextSize(p.name.c_str()).x;
            Badges::drawCheck(dl, ImVec2(tx + nw + 9, y + ImGui::GetFontSize() * 0.5f + 1), 13.0f);
        }
        y += rowH;
    }
}

} // namespace Hud
