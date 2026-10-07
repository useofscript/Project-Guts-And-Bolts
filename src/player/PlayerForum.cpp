// The Forum: boards (Help, Scripting Helpers, Game Ads...), each with threads and
// replies. Same as the website's Forum page; it all lives on the Guts&Bolts server.
#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/Account.h"
#include "../game/Badges.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>

using namespace Site;
using json = nlohmann::json;

#include "SocialUi.h"
using namespace Social;

// Go to the list of boards (both empty), a board, or a thread (page -1 = its last page).
void PlayerApp::openForum(const std::string& board, const std::string& thread, int page) {
    m_page = Page::Forum;
    if (m_loaded.find("forum ") == std::string::npos) m_loaded += "forum ";
    m_forumBoard = board;
    m_forumThread = thread;
    m_forumPage = page;
    m_forumMsg.clear();
    m_forumWriting = false;
    m_forum = json::object();
    const std::string op = !thread.empty() ? "forum.thread" : !board.empty() ? "forum.list" : "forum.boards";
    json args = json::object();
    if (!thread.empty()) args["thread"] = thread;
    if (!board.empty()) args["board"] = board;
    args["page"] = page;
    const std::string want = board + "/" + thread;
    Online::request(op, args, [this, want](const json& r) {
        if (m_forumBoard + "/" + m_forumThread != want) return;   // they went somewhere else meanwhile
        if (!r.value("ok", false)) { m_forumMsg = r.value("error", std::string("Couldn't load the forum.")); return; }
        m_forum = r;
        m_forumPage = r.value("page", 0);
        if (r.contains("thread")) m_forumBoard = r["thread"].value("board", m_forumBoard);
    });
}

namespace {
// "< 1 2 3 >": returns the page clicked, or -1.
int pager(int page, int pages) {
    if (pages <= 1) return -1;
    int go = -1;
    ImGui::PushID("pager");
    ImGui::TextDisabled("Page");
    for (int i = 0; i < pages; ++i) {
        if (pages > 9 && i > 1 && i < pages - 2 && std::abs(i - page) > 2) {   // 1 2 ... 7 8 9 ... 20 21
            if (i == 2 || i == page + 3) { ImGui::SameLine(); ImGui::TextDisabled("..."); }
            continue;
        }
        ImGui::SameLine();
        ImGui::PushID(i);
        std::string n = std::to_string(i + 1);
        if (i == page) ImGui::TextUnformatted(n.c_str());
        else if (ImGui::SmallButton(n.c_str())) go = i;
        ImGui::PopID();
    }
    ImGui::PopID();
    return go;
}

// Light table headers (the default dark ones don't fit the site's cream pages).
void headers() {
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.98f, 0.93f, 0.85f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.12f, 0.1f, 1));
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor(2);
}

void title(const char* text) {
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.0f);
}
} // namespace

void PlayerApp::drawForum() {
    if (needsServer("The Forum")) return;
    if (m_loaded.find("forum ") == std::string::npos) {   // just arrived from the nav bar
        m_loaded += "forum ";
        openForum(m_forumBoard, m_forumThread, m_forumPage);
    }
    if (!m_forumThread.empty()) { drawForumThread(); return; }
    const bool loaded = !m_forum.empty();

    // --- All the boards ---
    if (m_forumBoard.empty()) {
        title("Forum");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Ask for help, show off your games and talk to other players. Be kind: the forum follows the same rules as chat.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (!loaded) { ImGui::TextDisabled("%s", m_forumMsg.empty() ? "Loading..." : m_forumMsg.c_str()); return; }
        const bool narrow = ImGui::GetContentRegionAvail().x < 560;
        if (!ImGui::BeginTable("##boards", narrow ? 2 : 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH)) return;
        ImGui::TableSetupColumn("Board", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        if (!narrow) {
            ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Posts", ImGuiTableColumnFlags_WidthFixed, 50);
        }
        ImGui::TableSetupColumn("Last post", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        headers();
        std::string goBoard, goThread;
        const json& boards = m_forum["boards"];
        for (size_t i = 0; i < boards.size(); ++i) {
            const json& b = boards[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
            if (ImGui::Selectable(b.value("name", std::string()).c_str(), false, ImGuiSelectableFlags_None)) goBoard = b.value("id", std::string());
            ImGui::PopStyleColor();
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("%s", b.value("about", std::string()).c_str());
            ImGui::PopTextWrapPos();
            if (narrow) ImGui::TextDisabled("%lld threads, %lld posts", b.value("threads", 0LL), b.value("posts", 0LL));
            if (!narrow) {
                ImGui::TableNextColumn();
                ImGui::Text("%lld", b.value("threads", 0LL));
                ImGui::TableNextColumn();
                ImGui::Text("%lld", b.value("posts", 0LL));
            }
            ImGui::TableNextColumn();
            if (b.contains("last") && b["last"].is_object()) {
                const json& last = b["last"];
                ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
                ImGui::PushTextWrapPos(0);
                if (ImGui::Selectable(last.value("title", std::string()).c_str(), false)) goThread = last.value("thread", std::string());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::TextDisabled("%s by %s", when(last.value("at", 0LL)).c_str(),
                                    last.contains("by") ? last["by"].value("name", std::string("?")).c_str() : "?");
            } else {
                ImGui::TextDisabled("Nothing yet");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!goBoard.empty()) openForum(goBoard, "", 0);
        else if (!goThread.empty()) openForum("", goThread, -1);
        return;
    }

    // --- One board: its threads ---
    if (backLink()) { openForum("", "", 0); return; }
    if (!loaded) { ImGui::TextDisabled("%s", m_forumMsg.empty() ? "Loading..." : m_forumMsg.c_str()); return; }
    const json& board = m_forum["board"];
    title(board.value("name", std::string()).c_str());
    ImGui::TextDisabled("%s", board.value("about", std::string()).c_str());
    ImGui::Spacing();
    const bool canPost = m_forum.value("canPost", false);
    if (canPost && !m_forumWriting) {
        if (Classic::button("New thread", Classic::kPlay, ImVec2(140, 30))) { m_forumWriting = true; m_forumMsg.clear(); }
    } else if (!canPost) {
        ImGui::TextDisabled("%s", board.value("staffOnly", false) ? "Only Guts&Bolts staff post here."
                                                                   : "Sign up to start a thread.");
    }
    if (m_forumWriting) {
        float w = std::min(560.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetNextItemWidth(w);
        ImGui::InputTextWithHint("##ftitle", "Title", &m_forumTitle);
        if (m_forumTitle.size() > 80) m_forumTitle.resize(80);
        ImGui::InputTextMultiline("##ftext", &m_forumText, ImVec2(w, 140));
        if (m_forumText.size() > 3000) m_forumText.resize(3000);
        ImGui::BeginDisabled(m_busy);
        if (Classic::button(m_busy ? "Posting..." : "Post thread", Classic::kPlay, ImVec2(130, 30))) {
            m_busy = true;
            Online::request("forum.post", {{"board", m_forumBoard}, {"title", m_forumTitle}, {"text", m_forumText}}, [this](const json& r) {
                m_busy = false;
                if (!r.value("ok", false)) { m_forumMsg = r.value("error", std::string()); return; }
                m_forumTitle.clear();
                m_forumText.clear();
                openForum(m_forumBoard, r["thread"].value("id", std::string()), 0);
            });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 30))) { m_forumWriting = false; m_forumMsg.clear(); }
    }
    if (!m_forumMsg.empty()) ImGui::TextColored(ImVec4(0.8f, 0.15f, 0.1f, 1), "%s", m_forumMsg.c_str());
    ImGui::Spacing();

    const json& threads = m_forum["threads"];
    if (threads.empty()) { ImGui::TextDisabled("No threads yet. Start the first one!"); return; }
    const bool narrow = ImGui::GetContentRegionAvail().x < 560;
    std::string go;
    if (ImGui::BeginTable("##threads", narrow ? 1 : 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        if (!narrow) {
            ImGui::TableSetupColumn("Replies", ImGuiTableColumnFlags_WidthFixed, 56);
            ImGui::TableSetupColumn("Views", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Last post", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        }
        headers();
        for (size_t i = 0; i < threads.size(); ++i) {
            const json& th = threads[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string label = std::string(th.value("pinned", false) ? "[Pinned] " : "") + (th.value("locked", false) ? "[Locked] " : "") +
                                th.value("title", std::string());
            ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
            if (ImGui::Selectable(label.c_str(), false)) go = th.value("id", std::string());
            ImGui::PopStyleColor();
            ImGui::TextDisabled("by %s", th.contains("by") ? th["by"].value("name", std::string("?")).c_str() : "?");
            if (narrow) ImGui::TextDisabled("%lld replies, last %s", th.value("replies", 0LL), when(th.value("last", 0LL)).c_str());
            if (!narrow) {
                ImGui::TableNextColumn();
                ImGui::Text("%lld", th.value("replies", 0LL));
                ImGui::TableNextColumn();
                ImGui::Text("%lld", th.value("views", 0LL));
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s by %s", when(th.value("last", 0LL)).c_str(),
                                    th.contains("lastBy") ? th["lastBy"].value("name", std::string("?")).c_str() : "?");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!go.empty()) { openForum(m_forumBoard, go, 0); return; }
    if (int p = pager(m_forumPage, m_forum.value("pages", 1)); p >= 0) openForum(m_forumBoard, "", p);
}

void PlayerApp::drawForumThread() {
    if (backLink()) { openForum(m_forumBoard, "", 0); return; }
    if (m_forum.empty() || !m_forum.contains("thread")) {
        ImGui::TextDisabled("%s", m_forumMsg.empty() ? "Loading..." : m_forumMsg.c_str());
        return;
    }
    const json th = m_forum["thread"];
    const std::string id = th.value("id", std::string());
    ImGui::SameLine(0, 10);
    ImGui::TextDisabled("Forum > %s", th.value("boardName", std::string()).c_str());
    ImGui::PushTextWrapPos(0);
    title(th.value("title", std::string()).c_str());
    ImGui::PopTextWrapPos();
    if (th.value("pinned", false) || th.value("locked", false))
        ImGui::TextDisabled("%s%s", th.value("pinned", false) ? "Pinned. " : "", th.value("locked", false) ? "Locked: no more replies." : "");

    // What staff (and post owners) can do; the server sends back the page as it is now.
    auto act = [this, id](const std::string& op, json args) {
        args["thread"] = id;
        args["page"] = m_forumPage;
        Online::request(op, args, [this, id](const json& r) {
            if (m_forumThread != id) return;
            if (!r.value("ok", false)) { m_forumMsg = r.value("error", std::string()); return; }
            if (r.value("gone", false)) { openForum(r.value("board", m_forumBoard), "", 0); return; }
            m_forum = r;
            m_forumPage = r.value("page", 0);
            m_forumMsg.clear();
        });
    };
    if (m_forum.value("canModerate", false)) {
        if (ImGui::SmallButton(th.value("pinned", false) ? "Unpin" : "Pin to top")) act("forum.mod", {{"pinned", !th.value("pinned", false)}});
        ImGui::SameLine();
        if (ImGui::SmallButton(th.value("locked", false) ? "Unlock" : "Lock")) act("forum.mod", {{"locked", !th.value("locked", false)}});
    }
    ImGui::Spacing();
    const int pages = m_forum.value("pages", 1);
    if (int p = pager(m_forumPage, pages); p >= 0) { openForum(m_forumBoard, id, p); return; }

    const json& posts = m_forum["posts"];
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float side = ImGui::GetContentRegionAvail().x < 480 ? 70.0f : 110.0f;
    for (size_t i = 0; i < posts.size(); ++i) {
        const json& p = posts[i];
        const json by = p.value("by", json::object());
        ImGui::PushID((int)i);
        ImVec2 top = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        // Left: who wrote it.
        ImGui::BeginGroup();
        avatarCircle(dl, ImVec2(top.x + side * 0.5f, top.y + 26), 20, by.value("id", std::string()), by.value("name", std::string()));
        ImGui::Dummy(ImVec2(side, 50));
        ImVec2 nameSize = ImGui::CalcTextSize(by.value("name", std::string("?")).c_str());
        ImGui::SetCursorScreenPos(ImVec2(top.x + std::max(4.0f, (side - nameSize.x) * 0.5f), ImGui::GetCursorScreenPos().y));
        if (nameLink(by, "author")) openProfile(by.value("id", std::string()));
        if (by.value("staff", false)) {
            ImGui::SetCursorScreenPos(ImVec2(top.x + side * 0.5f - 18, ImGui::GetCursorScreenPos().y));
            ImGui::TextColored(ImVec4(0.75f, 0.1f, 0.1f, 1), "Staff");
        }
        ImGui::EndGroup();
        // Right: what they wrote.
        ImGui::SetCursorScreenPos(ImVec2(top.x + side + 10, top.y + 8));
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", when(p.value("at", 0LL)).c_str());
        if (p.value("canDelete", false)) {
            ImGui::SameLine();
            if (ImGui::SmallButton(p.value("first", false) ? "Delete thread" : "Delete")) ImGui::OpenPopup("##delpost");
            if (ImGui::BeginPopup("##delpost")) {
                ImGui::TextUnformatted(p.value("first", false) ? "Delete the whole thread, replies and all?" : "Delete this post?");
                if (ImGui::Button("Yes, delete it")) { act("forum.delete", {{"post", p.value("id", std::string())}}); ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
        }
        if (canReport() && by.value("id", std::string()) != Account::id()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Report")) openReport("forum", id + ":" + p.value("id", std::string()), by.value("name", std::string()));
        }
        ImGui::PushTextWrapPos(top.x + w - 10);
        ImGui::TextUnformatted(p.value("text", std::string()).c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        float bottom = std::max(ImGui::GetItemRectMax().y, top.y + 50 + ImGui::GetTextLineHeightWithSpacing() * 2) + 10;
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(top, ImVec2(top.x + w, bottom), IM_COL32(255, 252, 245, 255), 4);
        dl->AddRectFilled(top, ImVec2(top.x + side, bottom), IM_COL32(246, 240, 228, 255), 4, ImDrawFlags_RoundCornersLeft);
        dl->AddRect(top, ImVec2(top.x + w, bottom), IM_COL32(215, 200, 175, 255), 4);
        dl->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(top.x, bottom + 6));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    if (int p = pager(m_forumPage, pages); p >= 0) { openForum(m_forumBoard, id, p); return; }
    if (!m_forumMsg.empty()) ImGui::TextColored(ImVec4(0.8f, 0.15f, 0.1f, 1), "%s", m_forumMsg.c_str());

    // Reply box.
    ImGui::Spacing();
    if (!m_forum.value("canReply", false)) {
        ImGui::TextDisabled("%s", th.value("locked", false) ? "This thread is locked." : "Sign up to reply.");
        return;
    }
    float bw = std::min(560.0f, ImGui::GetContentRegionAvail().x);
    ImGui::InputTextMultiline("##freply", &m_forumReply, ImVec2(bw, 90));
    if (m_forumReply.size() > 3000) m_forumReply.resize(3000);
    ImGui::BeginDisabled(m_busy || m_forumReply.empty());
    if (Classic::button(m_busy ? "Posting..." : "Post reply", Classic::kPlay, ImVec2(130, 30))) {
        m_busy = true;
        Online::request("forum.reply", {{"thread", id}, {"text", m_forumReply}}, [this, id](const json& r) {
            m_busy = false;
            if (!r.value("ok", false)) { m_forumMsg = r.value("error", std::string()); return; }
            m_forumReply.clear();
            if (m_forumThread != id) return;
            m_forum = r;
            m_forumPage = r.value("page", 0);
            m_forumMsg.clear();
        });
    }
    ImGui::EndDisabled();
}
