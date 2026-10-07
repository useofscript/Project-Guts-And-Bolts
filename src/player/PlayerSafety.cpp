// Reporting and blocking in the Player app, the same as on the website: a Report
// button on profiles, messages, games, items and groups, a Block button on
// profiles, the Blocked people list (Avatar > Your account) and the staff's
// Reports list (worker/server.js safetyOp, src/server/ServerSafety.cpp).
#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/Account.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <ctime>

using json = nlohmann::json;
using namespace Site;

namespace {
// What players pick from when they report something (the ban reasons' keys, in plainer words).
struct ReportReason { const char* key; const char* title; };
const ReportReason kReportReasons[] = {
    {"harassment", "Bullying or harassment"}, {"hate", "Hate speech"}, {"threats", "Threats"},
    {"scam", "Scam or phishing"}, {"personal", "Sharing personal info"}, {"sexual", "Sexual content"},
    {"extremism", "Violent extremism"}, {"selfharm", "Encouraging self-harm"}, {"exploit", "Cheating or exploiting"},
    {"spam", "Spam"}, {"impersonation", "Pretending to be someone else"}, {"inappropriate", "Inappropriate content"},
    {"underage", "Someone under 18"}, {"other", "Something else"},
};
const char* reasonTitle(const std::string& key) {
    for (const ReportReason& r : kReportReasons) if (key == r.key) return r.title;
    return key.c_str();
}
std::string agoShort(long long t) {
    const long long s = std::max(0LL, (long long)std::time(nullptr) - t);
    if (s < 60) return "just now";
    if (s < 3600) return std::to_string(s / 60) + " min ago";
    if (s < 86400) return std::to_string(s / 3600) + " h ago";
    return std::to_string(s / 86400) + " days ago";
}
} // namespace

bool PlayerApp::canReport() const {
    return Online::online() && !Online::isGuest() && Online::me().value("userId", 0LL) > 0;
}

void PlayerApp::openReport(const std::string& kind, const std::string& id, const std::string& name, const std::string& blockUser) {
    m_reportKind = kind;
    m_reportId = id;
    m_reportName = name;
    m_reportBlockUser = blockUser;
    m_reportReason = 0;
    m_reportNote.clear();
    m_reportMsg.clear();
    m_reportAlsoBlock = false;
    m_reportWanted = true;
}

void PlayerApp::drawReportDialog() {
    if (m_reportWanted) { ImGui::OpenPopup("Report##safety"); m_reportWanted = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(480), 0));
    if (!ImGui::BeginPopupModal("Report##safety", nullptr, ImGuiWindowFlags_NoResize)) return;
    if (tappedOutside()) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    const std::string what = m_reportKind == "user"    ? m_reportName
                           : m_reportKind == "message" ? "this message from " + m_reportName
                           : m_reportKind == "game"    ? "the game \"" + m_reportName + "\""
                           : m_reportKind == "group"   ? "the group \"" + m_reportName + "\""
                           : m_reportKind == "comment" ? "this comment by " + m_reportName
                                                       : "\"" + m_reportName + "\"";
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextWrapped("Report %s", what.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Guts&Bolts staff will look at it. The person you report isn't told who sent it.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::TextUnformatted("What's wrong?");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##reportReason", kReportReasons[m_reportReason].title)) {
        for (int i = 0; i < (int)std::size(kReportReasons); ++i)
            if (ImGui::Selectable(kReportReasons[i].title, i == m_reportReason)) m_reportReason = i;
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted("Tell staff what happened (optional)");
    ImGui::InputTextMultiline("##reportNote", &m_reportNote, ImVec2(-1, 90));
    if (m_reportNote.size() > 500) m_reportNote.resize(500);
    if (!m_reportBlockUser.empty()) ImGui::Checkbox("Block them too", &m_reportAlsoBlock);
    if (!m_reportMsg.empty()) ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "%s", m_reportMsg.c_str());
    ImGui::Spacing();
    if (Classic::button("Send report", ImVec4(0.78f, 0.2f, 0.2f, 1), ImVec2(130, 30))) {
        const std::string block = m_reportAlsoBlock ? m_reportBlockUser : "";
        Online::request("report.send", {{"kind", m_reportKind}, {"id", m_reportId}, {"reason", kReportReasons[m_reportReason].key},
                                        {"note", m_reportNote}},
                        [this, block](const json& r) {
                            if (!r.value("ok", false)) { m_reportMsg = r.value("error", std::string("That didn't work.")); m_reportWanted = true; return; }
                            if (block.empty()) { m_notice = "Thanks. We got your report and staff will look at it."; m_noticePlain = true; return; }
                            Online::request("block.add", {{"user", block}}, [this](const json&) {
                                m_notice = "Thanks. We got your report, and you blocked them.";
                                m_noticePlain = true;
                                m_messagesAt = -100.0;   // their messages leave your inbox
                                if (!m_profileId.empty()) openProfile(m_profileId);
                            });
                        });
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 30))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void PlayerApp::setBlocked(const std::string& user, bool on) {
    Online::request(on ? "block.add" : "block.remove", {{"user", user}}, [this](const json& r) {
        if (!r.value("ok", false)) { m_notice = r.value("error", std::string("That didn't work.")); m_noticePlain = true; return; }
        m_blockedAt = -100.0;
        m_messagesAt = -100.0;
        if (!m_profileId.empty()) openProfile(m_profileId);
    });
}

void PlayerApp::drawBlockedList() {
    if (!canReport()) return;
    ImGui::SeparatorText("Blocked people");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("People you block can't message, friend, follow, trade with or join you, and you won't see each "
                        "other online. Block someone from their profile.");
    ImGui::PopTextWrapPos();
    if (ImGui::GetTime() - m_blockedAt > 30.0) {
        m_blockedAt = ImGui::GetTime();
        Online::request("block.list", json::object(), [this](const json& r) {
            if (r.value("ok", false) && r.contains("people")) m_blocked = r["people"];
        });
    }
    if (!m_blocked.is_array() || m_blocked.empty()) { ImGui::TextDisabled("You haven't blocked anyone."); return; }
    for (const json& u : m_blocked) {
        const std::string id = u.value("id", std::string()), name = u.value("username", u.value("name", std::string()));
        ImGui::PushID(id.c_str());
        ImGui::TextUnformatted(name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Unblock")) setBlocked(id, false);
        ImGui::PopID();
    }
}

// Upload review: new decals, sounds and T-shirts from creators who aren't Verified wait here
// (worker/server.js). Nobody else can see or hear them until staff press OK.
void PlayerApp::drawUploadsBox() {
    ImGui::SeparatorText("Uploads to check");
    if (ImGui::GetTime() - m_uploadsAt > 60.0) {
        m_uploadsAt = ImGui::GetTime();
        Online::request("admin.uploads", json::object(), [this](const json& r) {
            if (r.value("ok", false) && r.contains("uploads")) m_uploads = r["uploads"];
        });
    }
    if (!m_uploads.is_array() || m_uploads.empty()) {
        ImGui::TextDisabled("Nothing waiting. New decals, sounds and T-shirts from people who aren't Verified show up here.");
        return;
    }
    for (size_t i = 0; i < m_uploads.size(); ++i) {
        const json a = m_uploads[i];
        const std::string id = a.value("id", std::string()), kind = a.value("kind", std::string());
        ImGui::PushID(id.c_str());
        ImGui::Separator();
        int w = 0, h = 0;
        const unsigned tex = libraryPicture(a, w, h);
        const float box = 72.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(box, box));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(p, ImVec2(p.x + box, p.y + box), IM_COL32(200, 200, 205, 255));
        if (tex && w > 0 && h > 0) {
            const float s = std::min(box / (float)w, box / (float)h), dw = w * s, dh = h * s;
            const ImVec2 q(p.x + (box - dw) * 0.5f, p.y + (box - dh) * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)tex, q, ImVec2(q.x + dw, q.y + dh), ImVec2(0, 1), ImVec2(1, 0));
        } else {
            const char* t = kind == "audio" ? "Sound" : "...";
            const ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(p.x + (box - ts.x) * 0.5f, p.y + (box - ts.y) * 0.5f), IM_COL32(120, 125, 135, 255), t);
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", a.value("name", std::string()).c_str());
        ImGui::TextDisabled("%s by %s  -  %s", Online::kindTitle(kind), a.value("creatorName", std::string("?")).c_str(),
                            agoShort(a.value("created", 0LL)).c_str());
        auto decide = [this, id](bool ok) {
            Online::request("admin.review", {{"id", id}, {"ok", ok}}, [this](const json& r) {
                if (r.value("ok", false) && r.contains("uploads")) m_uploads = r["uploads"];
                else m_staffMsg = r.value("error", std::string());
            });
        };
        if (ImGui::SmallButton(kind == "audio" ? "Listen" : "Look")) openAsset(id);
        ImGui::SameLine();
        if (ImGui::SmallButton("OK")) decide(true);
        ImGui::SameLine();
        if (ImGui::SmallButton("Turn down")) decide(false);
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void PlayerApp::drawReportsBox() {
    ImGui::SeparatorText("Reports");
    if (ImGui::RadioButton("Open", !m_reportsClosed)) { m_reportsClosed = false; m_reportsAt = -100.0; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Closed", m_reportsClosed)) { m_reportsClosed = true; m_reportsAt = -100.0; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) m_reportsAt = -100.0;
    if (ImGui::GetTime() - m_reportsAt > 60.0) {
        m_reportsAt = ImGui::GetTime();
        Online::request("admin.reports", {{"status", m_reportsClosed ? "closed" : "open"}}, [this](const json& r) {
            if (r.value("ok", false) && r.contains("reports")) m_reports = r["reports"];
            else m_staffMsg = r.value("error", std::string());
        });
    }
    if (!m_reports.is_array() || m_reports.empty()) {
        ImGui::TextDisabled("%s", m_reportsClosed ? "No closed reports yet." : "Nothing to look at right now.");
        return;
    }
    auto who = [](const json& u) { return u.is_object() ? u.value("username", u.value("name", std::string("?"))) : std::string("?"); };
    const bool official = Account::iAmStaff();
    for (size_t i = 0; i < m_reports.size(); ++i) {
        const json x = m_reports[i];
        const std::string kind = x.value("kind", std::string()), about = who(x.value("about", json()));
        std::string what = kind == "user" ? "the account " + about
                         : kind == "message" ? "a message from " + about
                         : kind == "game" ? "the game \"" + x.value("name", std::string()) + "\" by " + about
                         : kind == "group" ? "the group \"" + x.value("name", std::string()) + "\" (owner " + about + ")"
                         : kind == "comment" ? "a comment by " + about + " on \"" + x.value("name", std::string()) + "\""
                                           : "\"" + x.value("name", std::string()) + "\" by " + about;
        ImGui::PushID((int)i);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.1f, 0.1f, 1), "%s", reasonTitle(x.value("reason", std::string())));
        ImGui::SameLine(0, 0);
        ImGui::TextWrapped(": %s", what.c_str());
        const long long count = x.value("reports", 1LL);
        ImGui::TextDisabled("from %s  -  %s%s", who(x.value("from", json())).c_str(), agoShort(x.value("at", 0LL)).c_str(),
                            count > 1 ? ("  -  " + std::to_string(count) + " reports").c_str() : "");
        if (!x.value("note", std::string()).empty()) ImGui::TextWrapped("\"%s\"", x.value("note", std::string()).c_str());
        if (x.contains("copy") && x["copy"].is_object()) {
            ImGui::Indent(12);
            ImGui::TextWrapped("%s", x["copy"].value("subject", std::string()).c_str());
            ImGui::TextWrapped("%s", x["copy"].value("body", std::string()).c_str());
            ImGui::Unindent(12);
        }
        if (x.contains("about") && x["about"].is_object()) {
            const json& a = x["about"];
            if (ImGui::SmallButton("Profile")) openProfile(a.value("id", std::string()));
            ImGui::SameLine();
            if (ImGui::SmallButton(official ? "Warn or ban" : "Look up")) {
                m_findQuery = a.value("username", a.value("name", std::string()));
                Online::request("admin.find", {{"query", m_findQuery}}, [this](const json& r) {
                    if (r.value("ok", false)) m_foundUsers = r["users"];
                });
            }
            if (!m_reportsClosed) ImGui::SameLine();
        }
        if (m_reportsClosed) {
            ImGui::TextDisabled("%s by %s %s", x.value("outcome", std::string()) == "dismissed" ? "Dismissed" : "Done",
                                who(x.value("closedBy", json())).c_str(), agoShort(x.value("closedAt", 0LL)).c_str());
        } else {
            auto close = [&](const char* outcome) {
                Online::request("admin.closeReport", {{"id", x.value("id", std::string())}, {"outcome", outcome}}, [this](const json& r) {
                    if (r.value("ok", false) && r.contains("reports")) m_reports = r["reports"];
                    else m_staffMsg = r.value("error", std::string());
                });
            };
            if (ImGui::SmallButton("Done")) close("done");
            ImGui::SameLine();
            if (ImGui::SmallButton("Nothing wrong")) close("dismissed");
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextDisabled("\"Done\" means you did something about it. Closing one closes every report about the same thing.");
}

// The staff action log: who banned, warned, verified, checked or deleted what, newest first.
void PlayerApp::drawStaffLog() {
    ImGui::SeparatorText("Staff action log");
    if (ImGui::GetTime() - m_staffLogAt > 30.0) {
        m_staffLogAt = ImGui::GetTime();
        Online::request("admin.log", json::object(), [this](const json& r) {
            if (r.value("ok", false) && r.contains("log")) m_staffLog = r["log"];
        });
    }
    if (!m_staffLog.is_array() || m_staffLog.empty()) { ImGui::TextDisabled("Nothing yet."); return; }
    ImGui::BeginChild("##stafflog", ImVec2(0, std::min(260.0f, 24.0f * m_staffLog.size() + 10)), ImGuiChildFlags_Borders);
    ImGui::PushTextWrapPos(0);
    for (const auto& x : m_staffLog) {
        const std::string by = x.value("by", json::object()).value("name", std::string("?"));
        ImGui::TextDisabled("%s", agoShort(x.value("at", 0LL)).c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted((by + ": " + x.value("text", std::string())).c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}
