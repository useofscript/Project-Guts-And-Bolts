// Studio's online side: connecting to a Guts&Bolts server, publishing the
// game, and the Marketplace (plugins and audio people uploaded).
#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "Plugins.h"
#include "../core/Account.h"
#include "../core/Audio.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../game/Badges.h"
#include "../game/Bolts.h"
#include "../online/AssetCache.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace {

void byLine(const json& a) {
    ImGui::TextDisabled("by %s", a.value("creatorName", std::string("?")).c_str());
    if (a.value("creatorVerified", false)) { ImGui::SameLine(0, 3); Badges::check(); }
}

// A safe file name for an installed plugin.
std::string fileName(const std::string& name) {
    std::string out;
    for (char c : name) out += (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') ? c : '_';
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? "Plugin" : out;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

void onlineLine() {
    ImVec4 col = Online::online() ? ImVec4(0.4f, 0.85f, 0.45f, 1) : ImVec4(1.0f, 0.5f, 0.45f, 1);
    ImGui::TextColored(col, "%s", Online::configured() ? Online::statusText().c_str() : "Not connected to a server");
}

} // namespace

// ---------------------------------------------------------------------------
// File > Guts&Bolts Server...
// ---------------------------------------------------------------------------

void Editor::renderServerDialog() {
    if (m_openServer) { ImGui::OpenPopup("Guts&Bolts Server"); m_openServer = false; m_serverInput = Online::serverAddress(); }
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (!ImGui::BeginPopupModal("Guts&Bolts Server", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::TextWrapped("A Guts&Bolts server keeps published games, plugins, audio, Bolts and badges for everyone. "
                       "The official one runs on Cloudflare, so it's always on. You can also run your own "
                       "GutsAndBoltsServer on a computer and type its address here (like 192.168.1.20 or "
                       "myserver.com:7780).");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputTextWithHint("##addr", "server address (empty = offline)", &m_serverInput,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Connect", ImVec2(120, 0)) || enter) { Online::setServerAddress(m_serverInput); m_marketLoaded = false; }
    ImGui::SameLine();
    if (ImGui::Button("Official server", ImVec2(130, 0))) {
        m_serverInput = Online::kOfficialServer;
        Online::setServerAddress(m_serverInput);
        m_marketLoaded = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go offline", ImVec2(110, 0))) { m_serverInput.clear(); Online::setServerAddress(""); }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
    ImGui::Spacing();
    onlineLine();
    if (Online::online()) {
        const json& me = Online::me();
        ImGui::Text("Signed in as %s", me.value("name", std::string()).c_str());
        if (me.value("verified", false)) { ImGui::SameLine(0, 4); Badges::check(); }
        ImGui::SameLine();
        ImGui::TextDisabled("(%s Bolts)", Bolts::format(Online::bolts()).c_str());
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// File > Publish to Guts&Bolts...
// ---------------------------------------------------------------------------

void Editor::renderPublishDialog() {
    if (m_openPublish) {
        ImGui::OpenPopup("Publish to Guts&Bolts");
        m_openPublish = false;
        m_publishName = m_scene->info().title;
        m_publishDesc = m_scene->info().description;
        m_publishMsg.clear();
    }
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (!ImGui::BeginPopupModal("Publish to Guts&Bolts", nullptr, ImGuiWindowFlags_NoResize)) return;
    if (!Online::online()) {
        ImGui::TextWrapped("Publishing puts your game on a Guts&Bolts server, where everyone can find and play it "
                           "from the site's home page.");
        onlineLine();
        if (ImGui::Button("Pick a server...")) { ImGui::CloseCurrentPopup(); m_openServer = true; }
        ImGui::SameLine();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::TextWrapped("Everyone on %s will be able to find and play this game. Publishing games is free.",
                       Online::serverInfo().value("name", std::string("the server")).c_str());
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("Name", &m_publishName);
    ImGui::InputTextMultiline("Description", &m_publishDesc, ImVec2(-90, 80));
    const std::string& published = m_scene->info().publishedId;
    auto send = [this](bool update) {
        std::string data = Serializer::saveScene(*m_scene);
        std::string picture = m_viewport->snapshotPng(480, 270);   // for the game's card on the site
        json args = {{"name", m_publishName}, {"description", m_publishDesc}, {"data", Online::base64Encode(data)}};
        if (update) args["id"] = m_scene->info().publishedId;
        else args["kind"] = "game";
        m_onlineBusy = true;
        m_publishMsg = "Publishing...";
        Online::request(update ? "update" : "upload", args, [this, picture](const json& r) {
            m_onlineBusy = false;
            if (!r.value("ok", false)) { m_publishMsg = r.value("error", std::string("Publishing didn't work.")); return; }
            m_scene->info().publishedId = r["asset"].value("id", std::string());
            if (!picture.empty())
                Online::request("thumb.set", {{"id", m_scene->info().publishedId}, {"data", Online::base64Encode(picture)}},
                                [](const json& t) {
                    if (!t.value("ok", false)) Log::warn("The game's picture didn't upload: " + t.value("error", std::string()));
                }, 60);
            m_scene->info().title = m_publishName;
            m_scene->info().description = m_publishDesc;
            m_publishMsg = "Published! It's on the site's home page now. (Save your game to remember it's published.)";
            Log::system("Published \"" + m_publishName + "\" to " + Online::serverInfo().value("name", std::string("the server")));
        }, 120);
    };
    ImGui::BeginDisabled(m_onlineBusy);
    if (!published.empty()) {
        if (ImGui::Button("Update published game", ImVec2(200, 30))) send(true);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replace the version on the server with this one");
        ImGui::SameLine();
        if (ImGui::Button("Publish as a new game", ImVec2(180, 30))) send(false);
    } else {
        if (ImGui::Button("Publish", ImVec2(140, 30))) send(false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(90, 30))) ImGui::CloseCurrentPopup();
    if (!m_publishMsg.empty()) ImGui::TextWrapped("%s", m_publishMsg.c_str());
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// The Marketplace: plugins and audio from the server
// ---------------------------------------------------------------------------

void Editor::renderMarketplace() {
    if (!m_showMarketplace) return;
    ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Marketplace", &m_showMarketplace)) { ImGui::End(); return; }
    onlineLine();
    if (!Online::online()) {
        if (ImGui::Button("Pick a server...")) m_openServer = true;
        ImGui::End();
        return;
    }
    auto load = [this]() {
        m_marketLoaded = true;
        Online::request("list", {{"kind", "plugin"}, {"query", m_marketQuery}}, [this](const json& r) {
            if (r.value("ok", false)) m_marketPlugins = r["assets"];
        });
        Online::request("list", {{"kind", "audio"}, {"query", m_marketQuery}}, [this](const json& r) {
            if (r.value("ok", false)) m_marketAudio = r["assets"];
        });
    };
    if (!m_marketLoaded) load();
    ImGui::SetNextItemWidth(260);
    if (ImGui::InputTextWithHint("##q", "Search", &m_marketQuery, ImGuiInputTextFlags_EnterReturnsTrue)) load();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) load();
    ImGui::SameLine();
    ImGui::TextDisabled("You have");
    ImGui::SameLine();
    Bolts::amount(Online::bolts());
    if (!m_marketMsg.empty()) ImGui::TextWrapped("%s", m_marketMsg.c_str());

    if (ImGui::BeginTabBar("##market")) {
        // --- Plugins ---
        if (ImGui::BeginTabItem("Plugins")) {
            if (m_marketPlugins.empty()) ImGui::TextDisabled("No plugins yet. Be the first - see the Publish tab!");
            for (size_t i = 0; i < m_marketPlugins.size(); ++i) {
                const json a = m_marketPlugins[i];
                std::string id = a.value("id", std::string()), name = a.value("name", std::string());
                ImGui::PushID((int)i);
                ImGui::Separator();
                ImGui::TextUnformatted(name.c_str());
                ImGui::SameLine();
                byLine(a);
                ImGui::PushTextWrapPos(0);
                ImGui::TextDisabled("%s", a.value("description", std::string()).c_str());
                ImGui::PopTextWrapPos();
                long long price = a.value("price", 0LL);
                bool installed = std::filesystem::exists(Plugins::folder() / (fileName(name) + ".lua"));
                bool owned = price == 0 || Online::owns(id) || a.value("creator", std::string()) == Account::id();
                if (!owned) { Bolts::amount(price); ImGui::SameLine(); }
                ImGui::BeginDisabled(m_onlineBusy);
                std::string label = installed ? "Reinstall" : owned ? "Install" : "Buy & install";
                if (ImGui::Button(label.c_str())) {
                    m_onlineBusy = true;
                    auto install = [this, id, name]() {
                        Online::download(id, [this, name](bool ok, const std::filesystem::path& file, const json& info) {
                            m_onlineBusy = false;
                            if (!ok) { m_marketMsg = info.value("error", std::string("Download failed.")); return; }
                            std::error_code ec;
                            std::filesystem::copy_file(file, Plugins::folder() / (fileName(name) + ".lua"),
                                                       std::filesystem::copy_options::overwrite_existing, ec);
                            m_plugins->reload();
                            m_marketMsg = "Installed " + name + " - look in the PLUGINS tab.";
                            m_ribbonTab = 4;
                        }, true);
                    };
                    if (owned) install();
                    else Online::request("buy", {{"id", id}}, [this, install](const json& r) {
                        if (r.value("ok", false)) install();
                        else { m_onlineBusy = false; m_marketMsg = r.value("error", std::string()); }
                    });
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        // --- Audio ---
        if (ImGui::BeginTabItem("Audio")) {
            if (m_marketAudio.empty()) ImGui::TextDisabled("No audio yet. Upload some on the site's Create page!");
            for (size_t i = 0; i < m_marketAudio.size(); ++i) {
                const json a = m_marketAudio[i];
                std::string id = a.value("id", std::string()), name = a.value("name", std::string());
                ImGui::PushID((int)i);
                ImGui::Separator();
                ImGui::TextUnformatted(name.c_str());
                ImGui::SameLine();
                byLine(a);
                long long price = a.value("price", 0LL);
                bool owned = price == 0 || Online::owns(id) || a.value("creator", std::string()) == Account::id();
                if (!owned) { Bolts::amount(price); ImGui::SameLine(); }
                if (ImGui::SmallButton("Preview")) {
                    Online::download(id, [this, id](bool ok, const std::filesystem::path&, const json& info) {
                        if (ok) Audio::play("gb:" + id, 0.8f, 1.0f, false);
                        else m_marketMsg = info.value("error", std::string());
                    });
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(owned ? "Insert" : "Buy & insert")) {
                    auto insert = [this, id, name]() {
                        Online::download(id);
                        SceneNode* parent = m_scene->selected();
                        if (parent && !parent->isPart()) parent = nullptr;
                        auto s = std::make_unique<SceneNode>(name.empty() ? "Sound" : name, NodeKind::Sound);
                        s->soundId = "gb:" + id;
                        SceneNode* raw = m_scene->insert(std::move(s), parent);
                        m_scene->select(raw);
                        m_marketMsg = "Added the sound \"" + name + "\". Scripts can play it with :Play().";
                    };
                    if (owned) insert();
                    else Online::request("buy", {{"id", id}}, [this, insert](const json& r) {
                        if (r.value("ok", false)) insert();
                        else m_marketMsg = r.value("error", std::string());
                    });
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        // --- Publish a plugin ---
        if (ImGui::BeginTabItem("Publish a plugin")) {
            auto& list = m_plugins->list();
            if (list.empty()) {
                ImGui::TextWrapped("Put a .lua plugin in %s first.", Plugins::folder().string().c_str());
            } else {
                m_publishPluginIndex = std::clamp(m_publishPluginIndex, 0, (int)list.size() - 1);
                if (ImGui::BeginCombo("Plugin", list[m_publishPluginIndex]->name.c_str())) {
                    for (int k = 0; k < (int)list.size(); ++k)
                        if (ImGui::Selectable(list[k]->name.c_str(), k == m_publishPluginIndex)) m_publishPluginIndex = k;
                    ImGui::EndCombo();
                }
                const bool verified = Online::verified();
                if (verified) {
                    ImGui::SetNextItemWidth(160);
                    ImGui::InputInt("Price (Bolts)", &m_publishPluginPrice, 5, 50);
                    m_publishPluginPrice = std::clamp(m_publishPluginPrice, 0, 1000000);
                }
                ImGui::TextDisabled(verified ? "You're Verified: publishing is free, and you can sell it."
                                             : "Publishing a plugin costs 20 Bolts (free for Verified creators).");
                ImGui::BeginDisabled(m_onlineBusy);
                if (ImGui::Button("Publish plugin")) {
                    auto& p = *list[m_publishPluginIndex];
                    m_onlineBusy = true;
                    Online::request("upload", {{"kind", "plugin"}, {"name", p.name},
                                               {"price", verified ? m_publishPluginPrice : 0},
                                               {"data", Online::base64Encode(readFile(p.file))}},
                                    [this](const json& r) {
                        m_onlineBusy = false;
                        m_marketMsg = r.value("ok", false) ? "Published! Others can install it from the Marketplace."
                                                           : r.value("error", std::string());
                        m_marketLoaded = false;
                    });
                }
                ImGui::EndDisabled();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Editor::testSnapshot(const std::string& file) {
    std::string png = m_viewport->snapshotPng(480, 270);
    std::ofstream(file, std::ios::binary).write(png.data(), (std::streamsize)png.size());
    Log::system("Snapshot: " + std::to_string(png.size()) + " bytes to " + file);
}
