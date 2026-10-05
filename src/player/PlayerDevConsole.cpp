// The Developer Console, like Roblox's: F9 (or typing /devconsole in the chat)
// opens it. "Client" is what happened on this computer. "Server" is the game's
// server log plus a command bar that runs Lua on the server, and only the game's
// owner gets that tab (the host checks who they are, so nobody else sees it).

#include "PlayerApp.h"
#include "../core/Log.h"
#include "../game/GameSession.h"
#include "../net/NetGame.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

namespace {

void drawLines(const std::vector<Log::Entry>& lines, const std::string& filter, bool& follow) {
    ImGui::BeginChild("##lines", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const Log::Entry& e : lines) {
        if (!filter.empty() && e.text.find(filter) == std::string::npos) continue;
        ImVec4 col = e.level == Log::Level::Error ? ImVec4(1.0f, 0.36f, 0.33f, 1)
                   : e.level == Log::Level::Warn  ? ImVec4(1.0f, 0.75f, 0.25f, 1)
                   : e.level == Log::Level::System ? ImVec4(0.6f, 0.65f, 0.72f, 1)
                                                   : ImVec4(0.92f, 0.92f, 0.92f, 1);
        ImGui::TextDisabled("%s", e.time.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(e.text.c_str());
        ImGui::PopStyleColor();
    }
    if (follow && ImGui::GetScrollY() < ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;   // scrolled up to read: stop following
    ImGui::EndChild();
}

} // namespace

bool PlayerApp::devServerAccess() const {
    if (m_server) return m_server->iAmOwner();
    if (m_client) return m_client->devOwner();
    return true;   // playing alone: it's all yours
}

void PlayerApp::drawDevConsole() {
    if (!m_devConsole) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.1f, vp->WorkPos.y + vp->WorkSize.y * 0.1f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.8f, vp->WorkSize.y * 0.7f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.11f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.92f, 0.92f, 1));
    const bool open = ImGui::Begin("Developer Console (F9)", &m_devConsole, ImGuiWindowFlags_NoCollapse);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());   // above the game's own UI
    if (open) {
        const bool server = devServerAccess();
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##filter", "Search", &m_devFilter);
        ImGui::SameLine();
        if (ImGui::Button("Clear") && m_devTab == 0) Log::clear();
        if (ImGui::BeginTabBar("##devtabs")) {
            if (ImGui::BeginTabItem("Client")) {
                m_devTab = 0;
                drawLines(Log::entries(), m_devFilter, m_devFollow);
                ImGui::TextDisabled("What happened on your computer.");
                ImGui::EndTabItem();
            }
            if (server && ImGui::BeginTabItem("Server")) {
                m_devTab = 1;
                // Hosting (or alone), this computer *is* the server; joined, the host sends us its log.
                drawLines(m_client ? m_client->serverLog() : Log::entries(), m_devFilter, m_devFollow);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputTextWithHint("##cmd", "Run a command on the server (Lua), then press Enter", &m_devCommand,
                                             ImGuiInputTextFlags_EnterReturnsTrue) && !m_devCommand.empty()) {
                    if (m_client) m_client->devCommand(m_devCommand);
                    else if (m_server) m_server->devCommand(m_devCommand);
                    else {
                        Log::system("> " + m_devCommand);
                        std::string err;
                        if (!m_session->scripts().runCommand(m_devCommand, err)) Log::error(err);
                    }
                    m_devCommand.clear();
                    m_devFollow = true;
                    ImGui::SetKeyboardFocusHere(-1);
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}
