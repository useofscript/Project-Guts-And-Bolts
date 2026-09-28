#include "Badges.h"
#include "Profile.h"
#include "../core/Account.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Badges {

namespace {
const Info kInfo[] = {
    {"admin",    "Administrator",    "Runs Guts&Bolts. Only the official staff account has this badge.", false},
    {"tester",   "Tester",           "Helped test Guts&Bolts before everyone else.",                      true},
    {"bughunter","Bug Hunter",       "Found and reported a real bug.",                                    true},
    {"featured", "Featured Creator", "Made a game the staff picked as a favourite.",                     true},
};

std::string grantMessage(const std::string& key, const std::string& accountId) {
    return "gb-badge:" + key + ":" + accountId;
}

ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// A shield outline, used by every badge.
void shield(ImDrawList* dl, ImVec2 c, float s, ImU32 fill, ImU32 rim) {
    float w = s * 0.42f, top = c.y - s * 0.45f, mid = c.y + s * 0.12f, bot = c.y + s * 0.48f;
    ImVec2 pts[] = {
        {c.x - w, top}, {c.x + w, top}, {c.x + w, mid},
        {c.x + w * 0.55f, c.y + s * 0.34f}, {c.x, bot}, {c.x - w * 0.55f, c.y + s * 0.34f}, {c.x - w, mid}};
    dl->AddConvexPolyFilled(pts, 7, fill);
    dl->AddPolyline(pts, 7, rim, ImDrawFlags_Closed, std::max(1.5f, s * 0.07f));
}

// A hex bolt head, the Guts&Bolts logo mark.
void bolt(ImDrawList* dl, ImVec2 c, float r, ImU32 col, ImU32 hole) {
    ImVec2 p[6];
    for (int i = 0; i < 6; ++i) {
        float a = 3.14159265f / 3.0f * i + 3.14159265f / 6.0f;
        p[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
    }
    dl->AddConvexPolyFilled(p, 6, col);
    dl->AddCircleFilled(c, r * 0.42f, hole, 16);
}
} // namespace

const Info& info(Id id) { return kInfo[(int)id]; }

bool fromKey(const std::string& key, Id& out) {
    for (int i = 0; i < (int)Id::Count; ++i)
        if (key == kInfo[i].key) { out = (Id)i; return true; }
    return false;
}

std::vector<Id> verified(const std::string& accountId, const std::vector<Grant>& grants) {
    std::vector<Id> out;
    if (Account::isOfficial(accountId)) out.push_back(Id::Administrator);
    std::string official = Account::officialId();
    if (official.empty()) return out;
    for (const auto& [key, sig] : grants) {
        Id id;
        if (!fromKey(key, id) || !info(id).grantable) continue;
        bool dup = false;
        for (Id o : out) if (o == id) dup = true;
        if (!dup && Account::verify(official, grantMessage(key, accountId), sig)) out.push_back(id);
    }
    return out;
}

std::string makeCode(Id id, const std::string& accountId, std::string& error) {
    if (!Account::iAmStaff()) { error = "Only the staff account can give out badges."; return ""; }
    if (!info(id).grantable) { error = "That badge can't be given to anyone."; return ""; }
    std::string who;
    for (char c : accountId) if (!std::isspace((unsigned char)c)) who += (char)std::tolower((unsigned char)c);
    if (who.size() != 64) { error = "An account ID is 64 letters and numbers long (from their Avatar page)."; return ""; }
    std::string key = info(id).key;
    return key + ":" + Account::sign(grantMessage(key, who));
}

bool redeem(const std::string& codeIn, std::string& message) {
    std::string code;
    for (char c : codeIn) if (!std::isspace((unsigned char)c)) code += c;
    auto colon = code.find(':');
    Id id;
    if (colon == std::string::npos || !fromKey(code.substr(0, colon), id)) {
        message = "That doesn't look like a badge code.";
        return false;
    }
    Grant g{code.substr(0, colon), code.substr(colon + 1)};
    auto ok = verified(Account::id(), {g});
    if (ok.empty() || ok.back() != id) {
        message = "That code isn't valid for your account (codes only work for the account they were made for).";
        return false;
    }
    Profile& me = Profile::get();
    for (auto& old : me.grants) if (old.first == g.first) { message = "You already have that badge!"; return false; }
    me.grants.push_back(g);
    me.save();
    message = std::string("You got the ") + info(id).name + " badge!";
    return true;
}

void drawIcon(ImDrawList* dl, ImVec2 c, float s, Id id) {
    switch (id) {
    case Id::Administrator: {
        // Red shield, gold rim, silver bolt: the staff badge.
        shield(dl, c, s, rgb(200, 30, 36), rgb(255, 205, 70));
        bolt(dl, ImVec2(c.x, c.y - s * 0.03f), s * 0.24f, rgb(225, 228, 235), rgb(200, 30, 36));
        break;
    }
    case Id::Tester: {
        shield(dl, c, s, rgb(40, 110, 200), rgb(200, 225, 255));
        // A flask.
        float w = s * 0.13f;
        ImVec2 neckA(c.x - w * 0.5f, c.y - s * 0.28f), neckB(c.x + w * 0.5f, c.y - s * 0.1f);
        dl->AddRectFilled(neckA, neckB, rgb(235, 240, 250));
        dl->AddTriangleFilled(ImVec2(c.x - w * 0.5f, c.y - s * 0.1f), ImVec2(c.x + w * 0.5f, c.y - s * 0.1f),
                              ImVec2(c.x, c.y - s * 0.1f), rgb(235, 240, 250));
        ImVec2 body[] = {{c.x - w * 0.5f, c.y - s * 0.1f}, {c.x + w * 0.5f, c.y - s * 0.1f},
                         {c.x + s * 0.22f, c.y + s * 0.2f}, {c.x - s * 0.22f, c.y + s * 0.2f}};
        dl->AddConvexPolyFilled(body, 4, rgb(120, 230, 120));
        break;
    }
    case Id::BugHunter: {
        shield(dl, c, s, rgb(40, 140, 60), rgb(190, 240, 160));
        dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.02f), s * 0.16f, rgb(30, 30, 30), 20);
        dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.17f), s * 0.08f, rgb(30, 30, 30), 16);
        for (int i = -1; i <= 1; ++i) {
            float y = c.y + s * (0.02f + 0.08f * i);
            dl->AddLine(ImVec2(c.x - s * 0.28f, y + s * 0.03f * i), ImVec2(c.x + s * 0.28f, y + s * 0.03f * i),
                        rgb(30, 30, 30), std::max(1.0f, s * 0.04f));
        }
        dl->AddLine(ImVec2(c.x, c.y - s * 0.12f), ImVec2(c.x, c.y + s * 0.18f), rgb(200, 60, 50), std::max(1.0f, s * 0.03f));
        break;
    }
    case Id::FeaturedCreator: {
        shield(dl, c, s, rgb(120, 60, 170), rgb(255, 215, 90));
        ImVec2 star[10];
        for (int i = 0; i < 10; ++i) {
            float a = -3.14159265f / 2 + i * 3.14159265f / 5;
            float r = (i % 2 == 0) ? s * 0.26f : s * 0.11f;
            star[i] = ImVec2(c.x + std::cos(a) * r, c.y - s * 0.02f + std::sin(a) * r);
        }
        for (int i = 0; i < 10; ++i)
            dl->AddTriangleFilled(ImVec2(c.x, c.y - s * 0.02f), star[i], star[(i + 1) % 10], rgb(255, 215, 90));
        break;
    }
    default: break;
    }
}

void icon(Id id, float size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    drawIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size, id);
    if (ImGui::IsItemHovered()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));   // readable on the dark tooltip
        ImGui::SetTooltip("%s\n%s", info(id).name, info(id).description);
        ImGui::PopStyleColor();
    }
}

} // namespace Badges
