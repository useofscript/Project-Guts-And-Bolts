// Game passes in a game: MarketplaceService:PromptGamePassPurchase pops up a Buy
// window like Roblox's. The game's scripts run on the host: when they ask for us,
// the window shows here; when they ask for someone who joined, it's sent to their
// app (NetServer::promptPass) and their answer comes back.

#include "PlayerApp.h"
#include "SiteUi.h"
#include "../game/Bolts.h"
#include "../game/GameSession.h"
#include "../net/NetGame.h"
#include "../online/OnlineClient.h"

#include <imgui.h>

using json = nlohmann::json;
using namespace Site;

void PlayerApp::updatePassPrompts() {
    if (!m_session) return;
    // Our game's scripts asked: for us (player 1) show it here, for a joined player send it to them.
    for (auto& [userId, pass] : m_session->scripts().takePassPrompts()) {
        if (userId == 1) openPassPrompt(pass, false);
        else if (!m_server || !m_server->promptPass(userId, pass)) m_session->scripts().passPromptDone(userId, pass, false);
    }
    // Someone else's game asked us.
    if (m_client)
        if (std::string pass = m_client->takePassPrompt(); !pass.empty()) openPassPrompt(pass, true);
}

void PlayerApp::openPassPrompt(const std::string& pass, bool forHost) {
    if (!m_passPrompt.empty()) finishPassPrompt(false);   // (one at a time)
    m_passPrompt = pass;
    m_passForHost = forHost;
    m_passInfo = json();
    m_passMsg = "Loading...";
    m_passBusy = false;
    if (!Online::online()) { m_passMsg = "You need to be online to buy passes."; return; }
    Online::request("asset.info", {{"id", pass}}, [this, pass](const json& r) {
        if (m_passPrompt != pass) return;
        if (!r.value("ok", false) || !r.contains("asset") || r["asset"].value("kind", std::string()) != "gamepass") {
            m_passMsg = "That game pass doesn't exist.";
            return;
        }
        m_passInfo = r["asset"];
        m_passOwned = r.value("owned", false);
        m_passMsg.clear();
    });
}

void PlayerApp::finishPassPrompt(bool bought) {
    if (m_passPrompt.empty()) return;
    if (m_passForHost) { if (m_client) m_client->passDone(m_passPrompt, bought); }
    else if (m_session) m_session->scripts().passPromptDone(1, m_passPrompt, bought);
    m_passPrompt.clear();
}

void PlayerApp::drawPassPrompt() {
    if (m_passPrompt.empty()) return;
    if (!ImGui::IsPopupOpen("Buy Game Pass")) ImGui::OpenPopup("Buy Game Pass");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(380), 0));
    if (!ImGui::BeginPopupModal("Buy Game Pass", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) return;
    if (m_passInfo.is_object()) {
        const long long price = m_passInfo.value("price", 0LL);
        ImGui::SetWindowFontScale(1.3f);
        ImGui::TextWrapped("%s", m_passInfo.value("name", std::string("Game Pass")).c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("Game Pass");
        const std::string desc = m_passInfo.value("description", std::string());
        if (!desc.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(desc.c_str()); ImGui::PopTextWrapPos(); }
        ImGui::Spacing();
        if (m_passOwned) {
            ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "You already own this pass.");
            if (ImGui::Button("OK", ImVec2(110, 32))) { finishPassPrompt(true); ImGui::CloseCurrentPopup(); }
        } else {
            if (price > 0) { ImGui::TextUnformatted("Price:"); ImGui::SameLine(); Bolts::amount(price); }
            else ImGui::TextUnformatted("Free");
            ImGui::TextDisabled("You have"); ImGui::SameLine(); Bolts::amount(Online::bolts());
            ImGui::Spacing();
            const bool canAfford = Online::bolts() >= price;
            ImGui::BeginDisabled(m_passBusy || !canAfford || Online::isGuest());
            if (bigButton(price > 0 ? "Buy Now" : "Get It", kGreen, ImVec2(130, 34))) {
                m_passBusy = true;
                m_passMsg = "Buying...";
                const std::string pass = m_passPrompt, id = m_passInfo.value("id", pass);
                Online::request("buy", {{"id", id}}, [this, pass](const json& r) {
                    if (m_passPrompt != pass) return;
                    m_passBusy = false;
                    if (r.value("ok", false)) { finishPassPrompt(true); m_passMsg.clear(); }
                    else m_passMsg = r.value("error", std::string("Couldn't buy it."));
                });
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 34))) { finishPassPrompt(false); ImGui::CloseCurrentPopup(); }
            if (Online::isGuest()) ImGui::TextDisabled("Sign up to buy passes.");
            else if (!canAfford) ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.15f, 1), "You need %lld more Bolts.", price - Online::bolts());
        }
    } else if (ImGui::Button("Close", ImVec2(100, 30))) {
        finishPassPrompt(false);
        ImGui::CloseCurrentPopup();
    }
    if (!m_passMsg.empty()) ImGui::TextWrapped("%s", m_passMsg.c_str());
    if (m_passPrompt.empty()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// A published game's page: its passes, each with a Buy button (owned ones say so).
void PlayerApp::drawGamePasses(const std::string& gameKey) {
    if (!Online::online() || gameKey.empty() || gameKey.rfind("local:", 0) == 0) return;
    if (gameKey != m_passesKey) {
        m_passesKey = gameKey;
        m_passes = json::array();
        Online::request("pass.list", {{"game", gameKey}}, [this, gameKey](const json& r) {
            if (gameKey == m_passesKey && r.value("ok", false)) m_passes = r.value("passes", json::array());
        });
    }
    std::vector<const json*> shown;
    for (const json& p : m_passes) if (!p.value("offsale", false) || p.value("owned", false)) shown.push_back(&p);
    if (shown.empty()) return;
    ImGui::SeparatorText("Passes");
    const float tile = 150.0f, gap = 12.0f;
    const int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (tile + gap)));
    for (size_t i = 0; i < shown.size(); ++i) {
        const json& p = *shown[i];
        if (i % perRow) ImGui::SameLine(0, gap);
        ImGui::PushID((int)i);
        ImGui::BeginGroup();
        // A ticket (passes' pictures show on the website).
        ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(tile, tile * 0.6f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, ImVec2(a.x + tile, a.y + tile * 0.6f), IM_COL32(255, 255, 255, 255));
        dl->AddRect(a, ImVec2(a.x + tile, a.y + tile * 0.6f), IM_COL32(200, 205, 212, 255));
        ImVec2 t0(a.x + tile * 0.12f, a.y + tile * 0.14f), t1(a.x + tile * 0.88f, a.y + tile * 0.46f);
        dl->AddRectFilled(t0, t1, IM_COL32(47, 127, 209, 255), 6.0f);
        dl->AddCircleFilled(ImVec2(t0.x, (t0.y + t1.y) * 0.5f), 7.0f, IM_COL32(255, 255, 255, 255));
        dl->AddCircleFilled(ImVec2(t1.x, (t0.y + t1.y) * 0.5f), 7.0f, IM_COL32(255, 255, 255, 255));
        dl->AddText(ImVec2(t0.x + 18, (t0.y + t1.y) * 0.5f - 7), IM_COL32(255, 220, 90, 255), "PASS");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile);
        ImGui::TextUnformatted(p.value("name", std::string()).c_str());
        ImGui::PopTextWrapPos();
        const long long price = p.value("price", 0LL);
        if (p.value("owned", false)) ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "Owned");
        else {
            if (price > 0) Bolts::amount(price); else ImGui::TextUnformatted("Free");
            ImGui::SameLine();
            ImGui::BeginDisabled(m_busy || Online::isGuest() || Online::bolts() < price);
            if (Classic::button("Buy", Classic::kPlay, ImVec2(60, 0))) {
                const std::string id = p.value("id", std::string());
                Online::request("buy", {{"id", id}}, [this, gameKey](const json& r) {
                    m_passesKey.clear();   // look again (it's ours now)
                    if (!r.value("ok", false)) m_createMsg = r.value("error", std::string("Couldn't buy it."));
                });
            }
            ImGui::EndDisabled();
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::Spacing();
}
