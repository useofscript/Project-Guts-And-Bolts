#include "Hud.h"
#include "../scene/Scene.h"
#include "../scene/Player.h"
#include "../scripting/ScriptEngine.h"
#include "Badges.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
        y += rowH;
    }
}

} // namespace Hud
