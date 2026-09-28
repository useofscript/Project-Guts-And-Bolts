// The site's People (user search and profiles) and Groups pages. Both live on
// the Guts&Bolts server, so they need one to be connected.
#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/Account.h"
#include "../game/Badges.h"
#include "../game/Bolts.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <ctime>

using namespace Site;
using json = nlohmann::json;

#include "SocialUi.h"
using namespace Social;

namespace {

// A group row: emblem, name, owner, members. True when clicked.
bool groupRow(const json& g, int index) {
    ImGui::PushID(index);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    bool clicked = ImGui::InvisibleButton("##g", ImVec2(w, 64));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 64), IM_COL32(40, 120, 230, 20), 6);
    dl->AddRect(p, ImVec2(p.x + w, p.y + 64), IM_COL32(200, 205, 215, 255), 6);
    emblem(dl, ImVec2(p.x + 8, p.y + 8), 48, g.value("color", 0x3A7BD5), g.value("name", std::string()));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 66, p.y + 7));
    ImGui::BeginGroup();
    ImGui::TextColored(Classic::kLink, "%s", g.value("name", std::string()).c_str());
    if (g.contains("role")) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", g.value("role", std::string()).c_str()); }
    ImGui::TextDisabled("by %s  -  %lld member%s%s", g.value("ownerName", std::string()).c_str(), g.value("members", 0LL),
                        g.value("members", 0LL) == 1 ? "" : "s", g.value("open", true) ? "" : "  -  ask to join");
    std::string shout = g.value("shout", std::string());
    if (!shout.empty()) {
        if (shout.size() > 80) shout = shout.substr(0, 80) + "...";
        ImGui::TextDisabled("\"%s\"", shout.c_str());
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 70));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
    return clicked;
}

} // namespace

bool PlayerApp::needsServer(const char* what) {
    if (Online::online()) return false;
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s live on a Guts&Bolts server, so you need to be connected to one.", what);
    ImGui::PopTextWrapPos();
    if (Classic::button("Pick a server", Classic::kBlue, ImVec2(160, 30))) {
        m_serverInput = Online::serverAddress();
        m_showServer = true;
    }
    return true;
}

void PlayerApp::openProfile(const std::string& id) {
    m_profileId = id;
    m_profile = json::object();
    m_page = Page::Profile;
    m_socialMsg.clear();
    if (!Online::online()) return;   // drawProfile asks again once we're connected
    Online::request("profile", {{"id", id}}, [this, id](const json& r) {
        if (id != m_profileId) return;
        if (r.value("ok", false)) m_profile = r;
        else m_socialMsg = r.value("error", std::string());
    });
}

void PlayerApp::openGroup(const std::string& id) {
    if (id != m_groupId) { m_group = json::object(); m_editingGroup = false; }
    m_groupId = id;
    m_page = Page::Group;
    if (!Online::online()) return;   // drawGroup asks again once we're connected
    Online::request("groups.get", {{"id", id}}, [this, id](const json& r) {
        if (id != m_groupId) return;
        if (r.value("ok", false)) m_group = r;
        else { m_socialMsg = r.value("error", std::string()); m_group = json::object(); }
    });
}

// ---------------------------------------------------------------------------
// People
// ---------------------------------------------------------------------------

void PlayerApp::drawPeople() {
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("People");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Find other Guts&Bolts players.");
    ImGui::Spacing();
    if (needsServer("Profiles")) return;

    ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x - 190));
    bool enter = ImGui::InputTextWithHint("##people", "Search by name", &m_peopleQuery, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool first = m_loaded.find("people ") == std::string::npos;   // show recent people when the page opens
    if (first) m_loaded += "people ";
    if (Classic::button("Search", Classic::kBlue) || enter || first) {
        m_socialMsg.clear();
        Online::request("users.search", {{"query", m_peopleQuery}}, [this](const json& r) {
            if (r.value("ok", false)) {
                m_peopleResults = r["users"];
                if (m_peopleResults.empty()) m_socialMsg = m_peopleQuery.empty() ? "Nobody here yet." : "Nobody by that name yet.";
            } else {
                m_socialMsg = r.value("error", std::string());
            }
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("My profile")) openProfile(Account::id());
    if (!m_socialMsg.empty()) ImGui::TextDisabled("%s", m_socialMsg.c_str());
    else if (m_peopleQuery.empty() && !m_peopleResults.empty()) ImGui::TextDisabled("Recently online");
    ImGui::Spacing();

    // A grid of people cards.
    const float cw = 180.0f, ch = 150.0f;
    int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 12) / (cw + 12)));
    for (size_t i = 0; i < m_peopleResults.size(); ++i) {
        const json& u = m_peopleResults[i];
        if (i % perRow != 0) ImGui::SameLine(0, 12);
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##card", ImVec2(cw, ch))) openProfile(u.value("id", std::string()));
        bool hover = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + ch), IM_COL32(255, 255, 255, 255), 6);
        dl->AddRect(p, ImVec2(p.x + cw, p.y + ch), hover ? IM_COL32(40, 120, 230, 255) : IM_COL32(190, 195, 205, 255), 6,
                    0, hover ? 2.0f : 1.0f);
        std::string name = u.value("name", std::string());
        avatarCircle(dl, ImVec2(p.x + cw * 0.5f, p.y + 48), 34, u.value("id", std::string()), name);
        ImVec2 ts = ImGui::CalcTextSize(name.c_str());
        bool ver = u.value("verified", false);
        float nx = p.x + (cw - ts.x - (ver ? 18 : 0)) * 0.5f;
        dl->AddText(ImVec2(nx, p.y + 92), ImGui::ColorConvertFloat4ToU32(Classic::kLink), name.c_str());
        if (ver) Badges::drawCheck(dl, ImVec2(nx + ts.x + 10, p.y + 92 + ts.y * 0.5f), 14);
        const char* tag = u.value("official", false) ? "Guts&Bolts staff" : u.value("staff", false) ? "Staff" : nullptr;
        if (tag) {
            ImVec2 tt = ImGui::CalcTextSize(tag);
            dl->AddText(ImVec2(p.x + (cw - tt.x) * 0.5f, p.y + 114), IM_COL32(200, 40, 40, 255), tag);
        }
        ImGui::PopID();
    }
}

void PlayerApp::drawProfile() {
    if (backLink()) m_page = Page::People;
    if (needsServer("Profiles")) return;
    if (m_loaded.find("profile ") == std::string::npos) { m_loaded += "profile "; if (m_profile.empty()) openProfile(m_profileId); }
    if (!m_profile.contains("user")) { ImGui::TextDisabled("%s", m_socialMsg.empty() ? "Loading..." : m_socialMsg.c_str()); return; }
    const json& u = m_profile["user"];
    std::string id = u.value("id", std::string()), name = u.value("name", std::string());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    avatarCircle(dl, ImVec2(p.x + 50, p.y + 50), 46, id, name);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 112, p.y + 8));
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    if (u.value("verified", false)) { ImGui::SameLine(0, 6); Badges::check(22.0f); }
    if (u.value("official", false)) ImGui::TextColored(ImVec4(0.8f, 0.15f, 0.15f, 1), "Guts&Bolts staff");
    else if (u.value("staff", false)) ImGui::TextColored(ImVec4(0.15f, 0.3f, 0.6f, 1), "Staff");
    long long created = u.value("created", 0LL);
    std::time_t tt = (std::time_t)created;
    char since[32];
    std::strftime(since, sizeof(since), "%b %d, %Y", std::localtime(&tt));
    ImGui::TextDisabled("Joined %s  -  ID %s...", since, id.substr(0, 8).c_str());
    if (u.value("banned", false)) ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "This account is banned.");
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(p.y + 104, ImGui::GetItemRectMax().y + 8)));
    ImGui::Dummy(ImVec2(0, 0));
    {
        std::string fs = m_profile.value("friendship", std::string("none"));
        long long count = m_profile.value("friendCount", 0LL);
        if (fs != "self") { friendButton(id, fs); ImGui::SameLine(0, 16); }
        ImGui::TextDisabled("%lld friend%s", count, count == 1 ? "" : "s");
    }

    // Badges
    ImGui::SeparatorText("Badges");
    int shown = 0;
    if (u.contains("badges"))
        for (const auto& k : u["badges"]) {
            Badges::Id bid;
            if (!k.is_string() || !Badges::fromKey(k.get<std::string>(), bid)) continue;
            if (shown++) ImGui::SameLine(0, 14);
            ImGui::BeginGroup();
            Badges::icon(bid, 48.0f);
            ImGui::TextDisabled("%s", Badges::info(bid).name);
            ImGui::EndGroup();
        }
    if (!shown) ImGui::TextDisabled("No badges yet.");

    // Groups
    ImGui::SeparatorText("Groups");
    const json& groups = m_profile.contains("groups") ? m_profile["groups"] : json::array();
    if (groups.empty()) ImGui::TextDisabled("Not in any groups.");
    for (size_t i = 0; i < groups.size(); ++i)
        if (groupRow(groups[i], (int)i)) openGroup(groups[i].value("id", std::string()));

    // Creations
    ImGui::SeparatorText("Creations");
    const json& made = m_profile.contains("creations") ? m_profile["creations"] : json::array();
    if (made.empty()) ImGui::TextDisabled("Nothing published yet.");
    for (size_t i = 0; i < made.size(); ++i) {
        const json& a = made[i];
        ImGui::BulletText("%s", a.value("name", std::string()).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Online::kindTitle(a.value("kind", std::string())));
        long long price = a.value("price", 0LL);
        if (price > 0) { ImGui::SameLine(); Bolts::amount(price); }
    }
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

void PlayerApp::drawGroups() {
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Groups");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Communities for people who like the same things.");
    ImGui::Spacing();
    if (needsServer("Groups")) return;

    const char* tabs[] = {"My Groups", "Browse", "Create a Group"};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine();
        bool on = m_groupsTab == i;
        float tw = std::min(140.0f, (ImGui::GetContentRegionAvail().x - 16) / (3 - i));
        if (on ? Classic::button(tabs[i], Classic::kBlue, ImVec2(tw, 28)) : ImGui::Button(tabs[i], ImVec2(tw, 28))) {
            m_groupsTab = i;
            m_socialMsg.clear();
            if (size_t at = m_loaded.find("groups "); at != std::string::npos) m_loaded.erase(at, 7);   // refresh the list
        }
    }
    ImGui::Separator();
    ImGui::Spacing();

    auto loadList = [this]() {
        if (m_groupsTab == 0)
            Online::request("groups.mine", json::object(), [this](const json& r) { if (r.value("ok", false)) m_myGroups = r["groups"]; });
        else
            Online::request("groups.list", {{"query", m_groupQuery}}, [this](const json& r) { if (r.value("ok", false)) m_groupList = r["groups"]; });
    };
    if (m_groupsTab < 2 && m_loaded.find("groups ") == std::string::npos) { m_loaded += "groups "; loadList(); }

    if (m_groupsTab == 0) {
        if (m_myGroups.empty()) {
            ImGui::TextDisabled("You're not in any groups yet. Find one under Browse, or make your own!");
            return;
        }
        for (size_t i = 0; i < m_myGroups.size(); ++i)
            if (groupRow(m_myGroups[i], (int)i)) openGroup(m_myGroups[i].value("id", std::string()));
        return;
    }
    if (m_groupsTab == 1) {
        ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x - 90));
        bool enter = ImGui::InputTextWithHint("##gq", "Search groups", &m_groupQuery, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (Classic::button("Search", Classic::kBlue) || enter) loadList();
        ImGui::Spacing();
        if (m_groupList.empty()) ImGui::TextDisabled("No groups found. Why not make one?");
        for (size_t i = 0; i < m_groupList.size(); ++i)
            if (groupRow(m_groupList[i], (int)i)) openGroup(m_groupList[i].value("id", std::string()));
        return;
    }

    // Create a group.
    const bool verified = Online::verified();
    float fieldW = std::min(360.0f, ImGui::GetContentRegionAvail().x - 110);
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputTextWithHint("Name", "At least 3 letters", &m_newGroupName);
    ImGui::InputTextMultiline("About", &m_newGroupDesc, ImVec2(fieldW, 70));
    ImGui::SetNextItemWidth(fieldW);
    ImGui::ColorEdit3("Colour", &m_newGroupColor.x);
    ImGui::Checkbox("Anyone can join (untick: people ask first)", &m_newGroupOpen);
    ImGui::EndGroup();
    if (!portraitScreen()) {
        ImGui::SameLine(0, 24);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(96, 96));
        int col = ((int)std::lround(m_newGroupColor.r * 255) << 16) | ((int)std::lround(m_newGroupColor.g * 255) << 8) |
                  (int)std::lround(m_newGroupColor.b * 255);
        emblem(ImGui::GetWindowDrawList(), p, 96, col, m_newGroupName.empty() ? "?" : m_newGroupName);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", verified ? "Making a group is free for you (Verified)." : "Making a group costs 50 Bolts (free for Verified people).");
    ImGui::BeginDisabled(m_busy);
    if (Classic::button(m_busy ? "Making..." : "Create group", Classic::kPlay, ImVec2(180, 34))) {
        int col = ((int)std::lround(m_newGroupColor.r * 255) << 16) | ((int)std::lround(m_newGroupColor.g * 255) << 8) |
                  (int)std::lround(m_newGroupColor.b * 255);
        m_busy = true;
        Online::request("groups.create", {{"name", m_newGroupName}, {"description", m_newGroupDesc}, {"color", col},
                                          {"open", m_newGroupOpen}}, [this](const json& r) {
            m_busy = false;
            if (!r.value("ok", false)) { m_socialMsg = r.value("error", std::string()); return; }
            m_newGroupName.clear();
            m_newGroupDesc.clear();
            m_socialMsg.clear();
            m_loaded.clear();
            openGroup(r["group"].value("id", std::string()));
        });
    }
    ImGui::EndDisabled();
    if (!m_socialMsg.empty()) ImGui::TextColored(ImVec4(0.8f, 0.15f, 0.1f, 1), "%s", m_socialMsg.c_str());
}

void PlayerApp::drawGroup() {
    if (backLink()) { m_page = Page::Groups; m_loaded.clear(); }
    if (needsServer("Groups")) return;
    if (m_loaded.find("group ") == std::string::npos) { m_loaded += "group "; if (m_group.empty()) openGroup(m_groupId); }
    if (!m_group.contains("group")) { ImGui::TextDisabled("%s", m_socialMsg.empty() ? "Loading..." : m_socialMsg.c_str()); return; }
    const json g = m_group["group"];
    const std::string myRole = m_group.value("myRole", std::string());
    const bool member = !myRole.empty(), owner = myRole == "Owner", manage = owner || myRole == "Admin";
    const bool staff = Online::staff();
    auto act = [this](const std::string& op, json args) {
        args["id"] = m_groupId;
        Online::request(op, args, [this](const json& r) {
            m_socialMsg = r.value("ok", false) ? std::string() : r.value("error", std::string());
            if (r.value("ok", false) && r.value("requested", false)) m_socialMsg = "Asked to join - an admin will let you in.";
            openGroup(m_groupId);   // show the new state
        });
    };

    // --- Header ---
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    emblem(dl, p, 110, g.value("color", 0x3A7BD5), g.value("name", std::string()));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 126, p.y + 2));
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(g.value("name", std::string()).c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Owned by");
    ImGui::SameLine(0, 4);
    if (nameLink({{"id", g.value("owner", std::string())}, {"name", g.value("ownerName", std::string())},
                  {"verified", g.value("ownerVerified", false)}}, "owner"))
        openProfile(g.value("owner", std::string()));
    ImGui::TextDisabled("%lld member%s  -  %s", g.value("members", 0LL), g.value("members", 0LL) == 1 ? "" : "s",
                        g.value("open", true) ? "anyone can join" : "ask to join");
    if (member) ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.2f, 1), "You're %s %s", myRole == "Member" ? "a" : "an", myRole.c_str());
    ImGui::Spacing();
    if (!member) {
        bool requested = m_group.value("requested", false);
        ImGui::BeginDisabled(requested);
        if (Classic::button(requested ? "Asked to join" : g.value("open", true) ? "Join group" : "Ask to join", Classic::kPlay,
                            ImVec2(150, 30)))
            act("groups.join", json::object());
        ImGui::EndDisabled();
    } else if (!owner) {
        if (ImGui::Button("Leave group", ImVec2(120, 28))) act("groups.leave", json::object());
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(p.y + 118, ImGui::GetItemRectMax().y + 8)));
    ImGui::Dummy(ImVec2(0, 0));
    if (!m_socialMsg.empty()) ImGui::TextColored(ImVec4(0.75f, 0.2f, 0.1f, 1), "%s", m_socialMsg.c_str());

    ImGui::PushTextWrapPos(0);
    std::string about = g.value("description", std::string());
    if (!about.empty()) ImGui::TextUnformatted(about.c_str());
    ImGui::PopTextWrapPos();

    // --- Shout: the group's pinned message ---
    const json& shout = m_group.contains("shoutInfo") ? m_group["shoutInfo"] : json::object();
    if (shout.contains("text") || manage) {
        ImVec2 s0 = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(s0, ImVec2(s0.x + w, s0.y + 58), IM_COL32(255, 248, 225, 255), 6);
        dl->AddRect(s0, ImVec2(s0.x + w, s0.y + 58), IM_COL32(225, 190, 90, 255), 6);
        ImGui::SetCursorScreenPos(ImVec2(s0.x + 10, s0.y + 6));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(s0.x + w - 10);
        if (shout.contains("text")) {
            ImGui::TextUnformatted(shout.value("text", std::string()).c_str());
            ImGui::TextDisabled("- %s, %s", shout.value("name", std::string()).c_str(), when(shout.value("time", 0LL)).c_str());
        } else {
            ImGui::TextDisabled("No shout yet. Shouts are a message pinned to the top of the group.");
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(s0.x, s0.y + 64));
    ImGui::Dummy(ImVec2(0, 0));
        if (manage) {
            ImGui::SetNextItemWidth(std::min(400.0f, ImGui::GetContentRegionAvail().x - 90));
            ImGui::InputTextWithHint("##shout", "New shout", &m_shoutInput);
            ImGui::SameLine();
            if (ImGui::Button("Shout")) { act("groups.shout", {{"text", m_shoutInput}}); m_shoutInput.clear(); }
        }
    }

    // Wall / Members / Admin, as simple buttons (they match the rest of the site)
    const json& reqList = m_group.contains("requests") ? m_group["requests"] : json::array();
    std::string adminLabel = reqList.empty() ? std::string("Admin") : "Admin (" + std::to_string(reqList.size()) + ")";
    std::string membersLabel = "Members (" + std::to_string(g.value("members", 0LL)) + ")";
    const char* tabs[] = {"Wall", membersLabel.c_str(), adminLabel.c_str()};
    int tabCount = (manage || staff) ? 3 : 2;
    if (m_groupTab >= tabCount) m_groupTab = 0;
    ImGui::Spacing();
    for (int i = 0; i < tabCount; ++i) {
        if (i) ImGui::SameLine();
        ImGui::PushID(i);
        if (m_groupTab == i ? Classic::button(tabs[i], Classic::kBlue, ImVec2(120, 28)) : ImGui::Button(tabs[i], ImVec2(120, 28)))
            m_groupTab = i;
        ImGui::PopID();
    }
    ImGui::Separator();
    {
        // --- Wall ---
        if (m_groupTab == 0) {
            if (member) {
                ImGui::SetNextItemWidth(std::min(460.0f, ImGui::GetContentRegionAvail().x - 80));
                bool enter = ImGui::InputTextWithHint("##post", "Say something to the group", &m_wallInput,
                                                      ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if ((ImGui::Button("Post") || enter) && !m_wallInput.empty()) {
                    act("groups.post", {{"text", m_wallInput}});
                    m_wallInput.clear();
                }
            } else {
                ImGui::TextDisabled("Join the group to post here.");
            }
            const json& wall = m_group.contains("wall") ? m_group["wall"] : json::array();
            if (wall.empty()) ImGui::TextDisabled("Nothing on the wall yet.");
            for (size_t k = wall.size(); k-- > 0;) {
                const json& post = wall[k];
                ImGui::PushID((int)k);
                ImGui::Separator();
                if (nameLink(post, "poster")) openProfile(post.value("id", std::string()));
                ImGui::SameLine();
                ImGui::TextDisabled("%s", when(post.value("time", 0LL)).c_str());
                if (post.value("id", std::string()) == Account::id() || manage || staff) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("delete"))
                        act("groups.deletePost", {{"time", post.value("time", 0LL)}, {"by", post.value("id", std::string())}});
                }
                ImGui::PushTextWrapPos(0);
                ImGui::TextUnformatted(post.value("text", std::string()).c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopID();
            }
        }
        // --- Members ---
        if (m_groupTab == 1) {
            const json& list = m_group.contains("memberList") ? m_group["memberList"] : json::array();
            for (size_t i = 0; i < list.size(); ++i) {
                const json& m = list[i];
                std::string id = m.value("id", std::string()), role = m.value("role", std::string());
                ImGui::PushID((int)i);
                avatarCircle(ImGui::GetWindowDrawList(), ImVec2(ImGui::GetCursorScreenPos().x + 11, ImGui::GetCursorScreenPos().y + 10),
                             10, id, m.value("name", std::string()));
                ImGui::Dummy(ImVec2(24, 20));
                ImGui::SameLine();
                if (nameLink(m, "member")) openProfile(id);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", role.c_str());
                if (id != Account::id() && role != "Owner") {
                    if (owner) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(role == "Admin" ? "Make member" : "Make admin"))
                            act("groups.member", {{"user", id}, {"action", role == "Admin" ? "member" : "admin"}});
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Give ownership")) ImGui::OpenPopup("##giveaway");
                        if (ImGui::BeginPopup("##giveaway")) {
                            ImGui::Text("Make %s the owner? You'll become an admin.", m.value("name", std::string()).c_str());
                            if (ImGui::Button("Yes, give it to them")) {
                                act("groups.member", {{"user", id}, {"action", "owner"}});
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndPopup();
                        }
                    }
                    if (owner || (manage && role == "Member") || staff) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove")) act("groups.member", {{"user", id}, {"action", "kick"}});
                    }
                }
                ImGui::PopID();
            }
        }
        // --- Admin ---
        if (m_groupTab == 2) {
            ImGui::SeparatorText("Asking to join");
            const json& reqs = reqList;
            if (reqs.empty()) ImGui::TextDisabled("Nobody's waiting.");
            for (size_t i = 0; i < reqs.size(); ++i) {
                const json& u = reqs[i];
                ImGui::PushID((int)i + 1000);
                if (nameLink(u, "req")) openProfile(u.value("id", std::string()));
                ImGui::SameLine();
                if (ImGui::SmallButton("Let in")) act("groups.request", {{"user", u.value("id", std::string())}, {"accept", true}});
                ImGui::SameLine();
                if (ImGui::SmallButton("No")) act("groups.request", {{"user", u.value("id", std::string())}, {"accept", false}});
                ImGui::PopID();
            }
            if (manage) {
                ImGui::SeparatorText("Group settings");
                if (!m_editingGroup) { m_editDesc = g.value("description", std::string()); m_editingGroup = true; }
                ImGui::InputTextMultiline("About##edit", &m_editDesc, ImVec2(std::min(460.0f, ImGui::GetContentRegionAvail().x - 60), 70));
                bool open = g.value("open", true);
                if (ImGui::Checkbox("Anyone can join", &open)) act("groups.edit", {{"open", open}});
                if (ImGui::Button("Save")) act("groups.edit", {{"description", m_editDesc}});
            }
            if (owner || staff) {
                ImGui::SeparatorText("Danger zone");
                if (bigButton("Delete group", ImVec4(0.75f, 0.25f, 0.25f, 1), ImVec2(140, 30))) ImGui::OpenPopup("##delgroup");
                if (ImGui::BeginPopup("##delgroup")) {
                    ImGui::TextUnformatted("Delete this group for everyone? This can't be undone.");
                    if (ImGui::Button("Yes, delete it")) {
                        Online::request("groups.delete", {{"id", m_groupId}}, [this](const json& r) {
                            if (r.value("ok", false)) { m_page = Page::Groups; m_loaded.clear(); m_groupsTab = 0; }
                            else m_socialMsg = r.value("error", std::string());
                        });
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
        }
    }
}
