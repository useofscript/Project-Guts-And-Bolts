#include "Hud.h"
#include "../scene/Scene.h"
#include "../scene/Player.h"
#include "../scripting/ScriptEngine.h"

#include <algorithm>
#include <cstdio>

namespace Hud {

namespace {
void shadowText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text) {
    dl->AddText(font, size, ImVec2(pos.x + 2, pos.y + 2), IM_COL32(0, 0, 0, 160), text);
    dl->AddText(font, size, pos, col, text);
}
} // namespace

void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const GuiState& gui) {
    ImFont* font = ImGui::GetFont();
    float   base = ImGui::GetFontSize();

    // --- Health bar (top-right) ---
    if (Player* p = scene.player()) {
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
    float y = min.y + 14;
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
        ImVec2 c((min.x + max.x) * 0.5f, min.y + (max.y - min.y) * 0.22f);
        dl->AddRectFilled(ImVec2(c.x - s.x * 0.5f - 18, c.y - s.y * 0.5f - 10),
                          ImVec2(c.x + s.x * 0.5f + 18, c.y + s.y * 0.5f + 10), IM_COL32(0, 0, 0, 140), 8);
        shadowText(dl, font, big, ImVec2(c.x - s.x * 0.5f, c.y - s.y * 0.5f), IM_COL32(255, 255, 255, 255),
                   gui.message.c_str());
    }
}

} // namespace Hud
