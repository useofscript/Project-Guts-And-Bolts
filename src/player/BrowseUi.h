#pragma once
// The old catalog look, shared by the Player's Catalog and Library (the website has the
// same in app.js, browsePage): a search bar with a category box on top, "Browse by
// Category" down the left with filters under it, then the tiles, with a "Sort by" box and
// "Showing 1 - 42 of N results" above them.

#include "SiteUi.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace Browse {

constexpr int kPerPage = 42;

struct Cat {
    const char* key;
    const char* label;
    std::vector<std::pair<const char*, const char*>> kids;   // a group's own categories (shown under it)
    bool lineBefore = false;                                  // a line above it (like the old catalog's)
};

struct State {
    std::string cat, query, typed, creatorName;
    int sort = 0;      // kSorts
    int price = 0;     // kPrices
    int creator = 0;   // 0 everyone, 1 Guts&Bolts staff, 2 one person (creatorName)
    int page = 0;
};

inline const char* const kSorts[]  = {"Relevance", "Most Popular", "Recently Updated", "Price (Low to High)", "Price (High to Low)"};
inline const char* const kPrices[] = {"Any Price", "Free", "1 - 100", "101 - 1,000", "Over 1,000"};

inline std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Does this thing get through the Price and Creators filters?
inline bool passes(const nlohmann::json& a, const State& s) {
    const long long p = a.value("price", 0LL);
    static const long long lo[] = {0, 0, 1, 101, 1001}, hi[] = {-1, 0, 100, 1000, -1};
    if (s.price > 0 && (p < lo[s.price] || (hi[s.price] >= 0 && p > hi[s.price]))) return false;
    if (s.creator == 1 && !a.value("creatorStaff", false)) return false;
    if (s.creator == 2 && !s.creatorName.empty() && lower(a.value("creatorName", std::string())) != lower(s.creatorName)) return false;
    return true;
}

inline void sortList(std::vector<const nlohmann::json*>& list, int sort) {
    auto num = [](const nlohmann::json* a, const char* k) { return a->value(k, 0LL); };
    auto updated = [&](const nlohmann::json* a) { long long u = num(a, "updated"); return u ? u : num(a, "created"); };
    if (sort == 1) std::stable_sort(list.begin(), list.end(), [&](auto x, auto y) { return num(x, "sales") + num(x, "plays") > num(y, "sales") + num(y, "plays"); });
    if (sort == 2) std::stable_sort(list.begin(), list.end(), [&](auto x, auto y) { return updated(x) > updated(y); });
    if (sort == 3) std::stable_sort(list.begin(), list.end(), [&](auto x, auto y) { return num(x, "price") < num(y, "price"); });
    if (sort == 4) std::stable_sort(list.begin(), list.end(), [&](auto x, auto y) { return num(x, "price") > num(y, "price"); });
}

// Is there room for the categories down the side (or do they go on top, on phones)?
inline bool sideBySide() { return ImGui::GetContentRegionAvail().x >= 560; }

// The title, and the search bar with its category box. True when searched.
inline bool top(const char* title, State& s, const std::vector<Cat>& cats) {
    ImGui::SetWindowFontScale(1.7f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);
    const float full = ImGui::GetContentRegionAvail().x;
    const float boxW = std::min(150.0f, full * 0.35f), btnW = 70;
    float inputW = std::min(330.0f, full - boxW - btnW - 16);
    if (full > 700) ImGui::SameLine(ImGui::GetContentRegionMax().x - (inputW + boxW + btnW + 16));
    ImGui::PushID(title);
    ImGui::SetNextItemWidth(inputW);
    bool go = ImGui::InputText("##q", &s.typed, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(0, 4);
    std::string label;
    for (const Cat& c : cats) {
        if (s.cat == c.key) label = c.label;
        for (auto& k : c.kids) if (s.cat == k.first) label = k.second;
    }
    ImGui::SetNextItemWidth(boxW);
    if (ImGui::BeginCombo("##cat", label.c_str())) {
        auto pick = [&](const char* key, const char* l) { if (ImGui::Selectable(l, s.cat == key)) s.cat = key; };
        for (const Cat& c : cats) {
            pick(c.key, c.label);
            for (auto& k : c.kids) pick(k.first, k.second);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 4);
    if (ImGui::Button("Search", ImVec2(btnW, 0))) go = true;
    ImGui::PopID();
    if (go) { s.query = s.typed; s.page = 0; }
    return go;
}

// The "Browse by Category" box and the filters. width: the column's width.
// A line as wide as the side column (ImGui's Separator would cross the whole page).
inline void line(float width) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 3), ImVec2(p.x + width, p.y + 3), ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::Dummy(ImVec2(width, 7));
}

inline void side(State& s, const std::vector<Cat>& cats, bool priceFilter, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float headH = 34;
    dl->AddRectFilledMultiColor(p, ImVec2(p.x + width, p.y + headH), IM_COL32(90, 95, 102, 255), IM_COL32(90, 95, 102, 255),
                                IM_COL32(59, 63, 69, 255), IM_COL32(59, 63, 69, 255));
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(p.x + 7, p.y + 3), IM_COL32(255, 255, 255, 255), "Browse by");
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.2f, ImVec2(p.x + 7, p.y + 14), IM_COL32(255, 255, 255, 255), "Category");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Dummy(ImVec2(width, headH));
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_Border));
    auto row = [&](const char* key, const char* label, bool group, float indent) {
        ImGui::PushID(key);
        const bool on = s.cat == key;
        ImVec2 rp = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##row", on, 0, ImVec2(width, 26))) { s.cat = key; s.page = 0; }
        ImGui::GetWindowDrawList()->AddText(ImVec2(rp.x + 8 + indent, rp.y + 5), ImGui::GetColorU32(ImGuiCol_Text), label);
        if (group) ImGui::GetWindowDrawList()->AddText(ImVec2(rp.x + width - 16, rp.y + 5), ImGui::GetColorU32(ImGuiCol_TextDisabled), ">");
        ImGui::PopID();
    };
    for (const Cat& c : cats) {
        if (c.lineBefore) {
            ImVec2 lp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(lp, ImVec2(lp.x + width, lp.y), ImGui::GetColorU32(ImGuiCol_Border));
        }
        row(c.key, c.label, !c.kids.empty(), 0);
        bool open = s.cat == c.key;
        for (auto& k : c.kids) open = open || s.cat == k.first;
        if (open) for (auto& k : c.kids) row(k.first, k.second, false, 12);
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    dl->AddRect(ImVec2(p.x, p.y), ImVec2(p.x + width, ImGui::GetCursorScreenPos().y), IM_COL32(138, 144, 153, 255));

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Filters");
    ImGui::SetWindowFontScale(1.0f);
    if (priceFilter) {
        ImGui::TextUnformatted("Price");
        for (int i = 0; i < 5; ++i) if (ImGui::RadioButton(kPrices[i], s.price == i)) { s.price = i; s.page = 0; }
        line(width);
    }
    ImGui::TextUnformatted("Creators");
    if (ImGui::RadioButton("All Creators", s.creator == 0)) { s.creator = 0; s.page = 0; }
    if (ImGui::RadioButton("Guts&Bolts", s.creator == 1)) { s.creator = 1; s.page = 0; }
    ImGui::SetNextItemWidth(width - 44);
    bool go = ImGui::InputTextWithHint("##creator", "Name", &s.creatorName, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(0, 4);
    if (ImGui::Button("Go##creator", ImVec2(40, 0)) || go) { s.creator = s.creatorName.empty() ? 0 : 2; s.page = 0; }
    line(width);
}

// "MODELS / Showing 1 - 42 of 50 results" on the left, "Sort by" on the right.
inline void head(const char* heading, int total, State& s) {
    std::string caps = heading;
    for (char& c : caps) c = (char)std::toupper((unsigned char)c);
    const float avail = ImGui::GetContentRegionAvail().x;   // (SameLine below counts from the group's left edge)
    ImGui::BeginGroup();
    ImGui::TextUnformatted(caps.c_str());
    s.page = std::clamp(s.page, 0, std::max(0, (total - 1) / kPerPage));
    const int from = s.page * kPerPage, to = std::min(total, from + kPerPage);
    if (total) ImGui::TextDisabled("Showing %d - %d of %d result%s", from + 1, to, total, total == 1 ? "" : "s");
    else ImGui::TextDisabled("No results");
    ImGui::EndGroup();
    const float comboW = 170, sortW = comboW + ImGui::CalcTextSize("Sort by:").x + ImGui::GetStyle().ItemSpacing.x;
    const float textW = ImGui::GetItemRectSize().x;
    if (avail - sortW > textW + 8) ImGui::SameLine(avail - sortW);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Sort by:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##sort", kSorts[s.sort])) {
        for (int i = 0; i < 5; ++i) if (ImGui::Selectable(kSorts[i], s.sort == i)) { s.sort = i; s.page = 0; }
        ImGui::EndCombo();
    }
    ImGui::Spacing();
}

// Previous / Next under the tiles.
inline void pager(int total, State& s) {
    if (total <= kPerPage) return;
    const int pages = (total + kPerPage - 1) / kPerPage;
    ImGui::Spacing();
    ImGui::BeginDisabled(s.page == 0);
    if (ImGui::Button("< Previous")) --s.page;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Page %d of %d", s.page + 1, pages);
    ImGui::SameLine();
    ImGui::BeginDisabled(s.page + 1 >= pages);
    if (ImGui::Button("Next >")) ++s.page;
    ImGui::EndDisabled();
}

// The little box when you point at a tile: who made it, when, how many sold.
inline void tip(const nlohmann::json& a, const std::string& updated, const char* soldWord = "Sales") {
    ImGui::BeginTooltip();
    ImGui::TextDisabled("Creator:"); ImGui::SameLine(); ImGui::TextColored(Site::Classic::kLink, "%s", a.value("creatorName", std::string("?")).c_str());
    ImGui::TextDisabled("Updated: %s", updated.c_str());
    ImGui::TextDisabled("%s: %lld", soldWord, a.value("sales", 0LL));
    ImGui::EndTooltip();
}

} // namespace Browse
