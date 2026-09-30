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
#include "../scene/Physics.h"
#include "../renderer/Textures.h"

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
    if (m_openServer) { ImGui::OpenPopup("Guts&Bolts Server"); m_openServer = false; }
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (!ImGui::BeginPopupModal("Guts&Bolts Server", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::TextWrapped("Studio uses the official Guts&Bolts server for publishing games, plugins, audio, "
                       "Bolts and badges. You can still build and play-test without it.");
    ImGui::Spacing();
    if (ImGui::Button("Reconnect", ImVec2(120, 0))) { Online::connect(); m_marketLoaded = false; }
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

// ---------------------------------------------------------------------------
// File > Publish Selection to Library: share objects as a model, public or private
// ---------------------------------------------------------------------------

void Editor::renderPublishModelDialog() {
    if (m_openPublishModel) {
        ImGui::OpenPopup("Publish to Library");
        m_openPublishModel = false;
        SceneNode* sel = m_scene->selected();
        m_modelName = sel ? sel->name : "Model";
        m_modelDesc.clear();
        m_modelMsg.clear();
    }
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (!ImGui::BeginPopupModal("Publish to Library", nullptr, ImGuiWindowFlags_NoResize)) return;
    std::vector<SceneNode*> items;
    for (SceneNode* n : m_scene->selectionRoots())
        if (n != m_scene->root() && !m_scene->isCharacterPart(n)) items.push_back(n);
    if (!Online::online()) {
        onlineLine();
    } else if (items.empty()) {
        ImGui::TextWrapped("Select the objects you want to share first (a Model, some parts, a scripted tool...).");
    } else {
        ImGui::TextWrapped("Put %d selected object%s in the Guts&Bolts Library so you (or everyone) can insert "
                           "them from the Toolbox.", (int)items.size(), items.size() == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-90);
        ImGui::InputText("Name", &m_modelName);
        ImGui::InputTextMultiline("Description", &m_modelDesc, ImVec2(-90, 60));
        ImGui::Spacing();
        ImGui::TextUnformatted("Who can find it?");
        if (ImGui::RadioButton("Public: everyone can find and use it", m_modelPublic)) m_modelPublic = true;
        if (ImGui::RadioButton("Private: only you", !m_modelPublic)) m_modelPublic = false;
        int left = Online::me().value("publicModelsLeft", -1);
        if (left >= 0)
            ImGui::TextDisabled("You can make %d more model%s public this week (Verified creators have no limit).",
                                left, left == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::BeginDisabled(m_onlineBusy || m_modelName.empty());
        if (ImGui::Button("Publish", ImVec2(140, 30))) {
            // The objects, and a picture framed on them for the Library.
            json nodes = json::array();
            glm::vec3 lo(1e9f), hi(-1e9f);
            for (SceneNode* n : items) {
                nodes.push_back(json::parse(Serializer::nodeToString(*n)));
                std::vector<SceneNode*> stack{n};
                while (!stack.empty()) {
                    SceneNode* c = stack.back(); stack.pop_back();
                    if (c->isPart()) { AABB b = Physics::worldBounds(c); lo = glm::min(lo, b.min); hi = glm::max(hi, b.max); }
                    for (auto& k : c->children) stack.push_back(k.get());
                }
            }
            if (lo.x > hi.x) { lo = glm::vec3(-1.0f); hi = glm::vec3(1.0f); }
            std::string picture = m_viewport->snapshotAround(256, 256, (lo + hi) * 0.5f, glm::length(hi - lo) * 0.5f);
            json model = {{"format", "gbmodel"}, {"version", 1}, {"nodes", nodes}};
            json args = {{"kind", "model"}, {"name", m_modelName}, {"description", m_modelDesc},
                         {"access", m_modelPublic ? "public" : "private"}, {"data", Online::base64Encode(model.dump())}};
            m_onlineBusy = true;
            m_modelMsg = "Publishing...";
            Online::request("upload", args, [this, picture](const json& r) {
                m_onlineBusy = false;
                if (!r.value("ok", false)) { m_modelMsg = r.value("error", std::string("Publishing didn't work.")); return; }
                std::string id = r["asset"].value("id", std::string());
                if (!picture.empty()) Online::request("thumb.set", {{"id", id}, {"data", Online::base64Encode(picture)}}, nullptr, 60);
                m_modelMsg = "Published! Find it in the Toolbox's Library.";
                m_libraryLoaded = false;
                Online::connect();   // refresh "public models left"
            }, 120);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (ImGui::Button("Close", ImVec2(90, 30))) ImGui::CloseCurrentPopup();
    if (!m_modelMsg.empty()) ImGui::TextWrapped("%s", m_modelMsg.c_str());
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// The Toolbox's Library: everyone's public models, decals and audio, with pictures
// ---------------------------------------------------------------------------

void Editor::drawToolboxLibrary() {
    if (!Online::online()) { ImGui::TextDisabled("Connecting to the Library..."); return; }
    static const char* kinds[] = {"model", "decal", "audio"};
    static const char* labels[] = {"Models", "Decals", "Audio"};
    auto load = [this]() {
        m_libraryLoaded = true;
        Online::request("list", {{"kind", kinds[m_libraryKind]}, {"query", m_libraryQuery}, {"sort", "popular"}, {"limit", 60}},
                        [this](const json& r) { if (r.value("ok", false)) m_library = r["assets"]; });
    };
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(labels[i], m_libraryKind == i)) { m_libraryKind = i; m_library = json::array(); load(); }
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##libq", "Search the Library", &m_libraryQuery, ImGuiInputTextFlags_EnterReturnsTrue)) load();
    if (!m_libraryLoaded) load();
    if (!m_libraryMsg.empty()) ImGui::TextWrapped("%s", m_libraryMsg.c_str());
    if (m_library.empty()) { ImGui::TextDisabled("Nothing here yet."); return; }

    // A picture for each: models have a snapshot, decals are pictures themselves.
    auto thumbFor = [this](const json& a) -> unsigned {
        std::string id = a.value("id", std::string()), kind = a.value("kind", std::string());
        auto it = m_libraryThumbs.find(id);
        if (it == m_libraryThumbs.end()) {
            m_libraryThumbs[id] = "";
            if (kind == "decal") {
                Online::download(id, [this, id](bool ok, const std::filesystem::path& f, const json&) { if (ok) m_libraryThumbs[id] = f.string(); });
            } else if (kind == "model" && a.value("thumb", 0LL) > 0) {
                Online::request("thumb.get", {{"id", id}}, [this, id](const json& r) {
                    std::string bytes;
                    if (!r.value("ok", false) || !Online::base64Decode(r.value("data", std::string()), bytes) || bytes.empty()) return;
                    std::filesystem::path f = Paths::downloadsFolder() / ("thumb-" + id + ".png");
                    std::ofstream(f, std::ios::binary).write(bytes.data(), (std::streamsize)bytes.size());
                    m_libraryThumbs[id] = f.string();
                });
            }
            return 0;
        }
        return it->second.empty() ? 0 : Textures::get(it->second);
    };

    const float cell = 76.0f;
    const int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 6) / (cell + 6)));
    for (size_t i = 0; i < m_library.size(); ++i) {
        const json a = m_library[i];
        std::string id = a.value("id", std::string()), name = a.value("name", std::string()), kind = a.value("kind", std::string());
        ImGui::PushID((int)i);
        if (i % perRow) ImGui::SameLine(0, 6);
        ImGui::BeginGroup();
        unsigned tex = thumbFor(a);
        bool clicked;
        if (tex) clicked = ImGui::ImageButton("##t", (ImTextureID)(intptr_t)tex, ImVec2(cell - 8, cell - 8), ImVec2(0, 1), ImVec2(1, 0));   // pictures load bottom row first
        else clicked = ImGui::Button(kind == "audio" ? "Sound" : kind == "decal" ? "Decal" : "Model", ImVec2(cell, cell));
        std::string shortName = name.size() > 11 ? name.substr(0, 10) + "..." : name;
        ImGui::TextDisabled("%s", shortName.c_str());
        ImGui::EndGroup();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nby %s\n%s", name.c_str(), a.value("creatorName", std::string("?")).c_str(),
                              kind == "model" ? "Click to insert" : kind == "decal" ? "Click to put on the selected part" : "Click to add a Sound");
        if (clicked) {
            if (kind == "model") {
                m_libraryMsg = "Getting " + name + "...";
                Online::download(id, [this, name](bool ok, const std::filesystem::path& f, const json& info) {
                    if (!ok) { m_libraryMsg = info.value("error", std::string("Download failed.")); return; }
                    std::ifstream in(f, std::ios::binary);
                    std::stringstream ss; ss << in.rdbuf();
                    json model = json::parse(ss.str(), nullptr, false);
                    if (!model.is_object() || !model.contains("nodes")) { m_libraryMsg = "That model looks broken."; return; }
                    m_scene->deselect();
                    for (const json& nj : model["nodes"]) {
                        auto n = Serializer::nodeFromString(nj.dump(), true);
                        if (n) m_scene->addToSelection(m_scene->insert(std::move(n), m_scene->root()));
                    }
                    m_libraryMsg = "Inserted " + name + ".";
                    Online::fetchSounds(*m_scene);   // its decals and sounds
                });
            } else {
                Online::download(id);
                SceneNode* parent = m_scene->selected();
                if (parent && !parent->isPart()) parent = nullptr;
                if (kind == "decal" && !parent) { m_libraryMsg = "Select a part first, then click the decal."; }
                else {
                    auto n = std::make_unique<SceneNode>(name.empty() ? (kind == "decal" ? "Decal" : "Sound") : name,
                                                         kind == "decal" ? NodeKind::Decal : NodeKind::Sound);
                    if (kind == "decal") n->texture = "gb:" + id;
                    else n->soundId = "gb:" + id;
                    m_scene->insert(std::move(n), parent);
                    m_libraryMsg = "Added " + name + ".";
                }
            }
        }
        ImGui::PopID();
    }
}
