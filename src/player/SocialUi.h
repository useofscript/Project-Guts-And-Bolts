#pragma once
// Small drawing helpers shared by the site's People, Groups and Friends pages.
#include "SiteUi.h"
#include "../game/Badges.h"
#include "../online/Protocol.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <cfloat>
#include <ctime>
#include <string>

namespace Social {

using json = nlohmann::json;
using namespace Site;

inline ImU32 colorFromInt(int rgb, int alpha = 255) { return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, alpha); }

inline ImU32 colorFromId(const std::string& id) {
    unsigned h = 2166136261u;
    for (char c : id) h = (h ^ (unsigned char)c) * 16777619u;
    return IM_COL32(60 + (h & 0x7F), 60 + ((h >> 8) & 0x7F), 90 + ((h >> 16) & 0x7F), 255);
}

inline std::string initials(const std::string& name) {
    std::string out;
    bool start = true;
    for (char c : name) {
        if (c == ' ') { start = true; continue; }
        if (start && out.size() < 2) out += (char)std::toupper((unsigned char)c);
        start = false;
    }
    return out.empty() ? "?" : out;
}

// A round picture with someone's initial (we don't have avatar pictures on the server).
inline void avatarCircle(ImDrawList* dl, ImVec2 c, float r, const std::string& id, const std::string& name) {
    dl->AddCircleFilled(c, r, colorFromId(id), 32);
    std::string t = initials(name).substr(0, 1);
    ImFont* f = ImGui::GetFont();
    float size = r * 1.1f;
    ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str());
    dl->AddText(f, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 240), t.c_str());
}

// A group's emblem: a rounded square in its colour with its initials.
inline void emblem(ImDrawList* dl, ImVec2 a, float s, int color, const std::string& name) {
    dl->AddRectFilled(a, ImVec2(a.x + s, a.y + s), colorFromInt(color), s * 0.18f);
    dl->AddRect(a, ImVec2(a.x + s, a.y + s), IM_COL32(0, 0, 0, 60), s * 0.18f, 0, 2.0f);
    std::string t = initials(name);
    ImFont* f = ImGui::GetFont();
    float size = s * 0.42f;
    ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str());
    dl->AddText(f, size, ImVec2(a.x + (s - ts.x) * 0.5f + 1, a.y + (s - ts.y) * 0.5f + 1), IM_COL32(0, 0, 0, 90), t.c_str());
    dl->AddText(f, size, ImVec2(a.x + (s - ts.x) * 0.5f, a.y + (s - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), t.c_str());
}

inline std::string when(long long t) {
    long long d = Online::unixNow() - t;
    if (d < 60) return "just now";
    if (d < 3600) return std::to_string(d / 60) + "m ago";
    if (d < 86400) return std::to_string(d / 3600) + "h ago";
    if (d < 86400 * 30) return std::to_string(d / 86400) + "d ago";
    std::time_t tt = (std::time_t)t;
    char buf[32];
    std::strftime(buf, sizeof(buf), "%b %d, %Y", std::localtime(&tt));
    return buf;
}

// Name + blue check; returns true when clicked.
inline bool nameLink(const json& u, const char* id = nullptr) {
    ImGui::PushID(id ? id : u.value("id", std::string()).c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.06f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    bool clicked = ImGui::SmallButton(u.value("name", std::string("?")).c_str());
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    if (u.value("verified", false)) { ImGui::SameLine(0, 3); Badges::check(); }
    ImGui::PopID();
    return clicked;
}

inline bool backLink() {
    ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    bool r = ImGui::SmallButton("< Back");
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    return r;
}

} // namespace Social
