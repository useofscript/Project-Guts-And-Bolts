// Create > Library in the Player: everything people made public (models, decals,
// audio, plugins), and a page for each one, found by its ID. Listen to audio, look
// at decals and copy the "gb:..." ID to paste into a game (the website has the same
// pages at #/library/<id>).

#include "PlayerApp.h"
#include "SiteUi.h"
#include "SocialUi.h"
#include "../core/Audio.h"
#include "../core/Paths.h"
#include "../online/AssetCache.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"
#include "../renderer/Textures.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <ctime>
#include <fstream>

using json = nlohmann::json;
using namespace Site;

namespace {
const char* const kLibKinds[]  = {"model", "decal", "audio", "plugin"};
const char* const kLibTitles[] = {"Models", "Decals", "Audio", "Plugins"};

// A picture fitted into a box, keeping its shape (on white).
void fitPicture(ImVec2 p, float w, float h, unsigned tex, int tw, int th) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, 255));
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(200, 200, 205, 255));
    if (!tex || tw <= 0 || th <= 0) return;
    float s = std::min(w / (float)tw, h / (float)th);
    float dw = tw * s, dh = th * s;
    ImVec2 a(p.x + (w - dw) * 0.5f, p.y + (h - dh) * 0.5f);
    dl->AddImage((ImTextureID)(intptr_t)tex, a, ImVec2(a.x + dw, a.y + dh), ImVec2(0, 1), ImVec2(1, 0));
}

// A speaker (audio has no picture).
void speaker(ImVec2 c, float s, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(c.x - s * 0.45f, c.y - s * 0.15f), ImVec2(c.x - s * 0.25f, c.y + s * 0.15f), col);
    ImVec2 cone[] = {ImVec2(c.x - s * 0.25f, c.y - s * 0.15f), ImVec2(c.x, c.y - s * 0.35f), ImVec2(c.x, c.y + s * 0.35f), ImVec2(c.x - s * 0.25f, c.y + s * 0.15f)};
    dl->AddConvexPolyFilled(cone, 4, col);
    dl->PathArcTo(ImVec2(c.x, c.y), s * 0.2f, -0.8f, 0.8f);  dl->PathStroke(col, 0, s * 0.06f);
    dl->PathArcTo(ImVec2(c.x, c.y), s * 0.38f, -0.8f, 0.8f); dl->PathStroke(col, 0, s * 0.06f);
}

std::string dateText(long long t) {
    std::time_t tt = (std::time_t)t;
    char buf[32] = "?";
    if (std::tm* tm = std::gmtime(&tt)) std::strftime(buf, sizeof buf, "%b %d, %Y", tm);
    return buf;
}
} // namespace

// The picture for a Library thing: decals are their own picture, models have a snapshot.
// 0 until it has arrived.
unsigned PlayerApp::libraryPicture(const json& a, int& w, int& h) {
    const std::string id = a.value("id", std::string()), kind = a.value("kind", std::string());
    w = h = 0;
    if (kind == "decal") {
        const std::string texId = "gb:" + id;
        unsigned tex = Textures::get(texId);
        if (!tex && m_askedDownloads.insert(id).second) Online::download(id);
        if (tex) Textures::size(texId, w, h);
        return tex;
    }
    if (kind == "model" && a.value("thumb", 0LL) > 0) {
        const std::filesystem::path f = Paths::downloadsFolder() / ("thumb-" + id + ".png");
        if (m_askedDownloads.insert("thumb:" + id).second && !std::filesystem::exists(f))
            Online::request("thumb.get", {{"id", id}}, [f](const json& r) {
                std::string bytes;
                if (!r.value("ok", false) || !Online::base64Decode(r.value("data", std::string()), bytes) || bytes.empty()) return;
                std::ofstream(f, std::ios::binary).write(bytes.data(), (std::streamsize)bytes.size());
            });
        if (!std::filesystem::exists(f)) return 0;
        unsigned tex = Textures::get(f.string());
        if (tex) Textures::size(f.string(), w, h);
        return tex;
    }
    return 0;
}

void PlayerApp::openAsset(const std::string& rawId) {
    std::string id = rawId;
    while (!id.empty() && (id.back() == ' ' || id.back() == '\n')) id.pop_back();
    while (!id.empty() && id.front() == ' ') id.erase(0, 1);
    if (id.rfind("gb:", 0) == 0) id = id.substr(3);
    if (id.empty()) return;
    stopAssetSound();
    m_page = Page::Create;
    m_createKind = kLibraryTab;
    m_assetId = id;
    m_asset = json();
    m_assetMsg = "Loading...";
    Online::request("asset.info", {{"id", id}}, [this, id](const json& r) {
        if (m_assetId != id) return;
        if (!r.value("ok", false)) { m_assetMsg = r.value("error", std::string("Not found.")); return; }
        m_asset = r["asset"];
        m_assetMsg.clear();
    });
}

void PlayerApp::stopAssetSound() {
    if (m_assetSound) Audio::stop(m_assetSound);
    m_assetSound = 0;
}

void PlayerApp::drawLibrary() {
    if (!Online::online()) {
        ImGui::TextDisabled("The Library needs the Guts&Bolts server.");
        return;
    }
    if (!m_assetId.empty()) { drawAsset(); return; }
    stopAssetSound();

    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Everything people made public. Open one to listen to it or look at it, and copy its ID "
                        "to paste into a Decal's Texture or a Sound's File in Studio.");
    ImGui::PopTextWrapPos();
    // The kinds, then search, then "got an ID?".
    // (Own IDs: the Create page's tabs above have buttons with the same names.)
    ImGui::PushID("libkinds");
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        ImGui::PushID(i);
        const bool on = m_libKind == kLibKinds[i];
        if (on ? Classic::button(kLibTitles[i], Classic::kBlue, ImVec2(90, 26)) : ImGui::Button(kLibTitles[i], ImVec2(90, 26))) {
            m_libKind = kLibKinds[i];
            m_libLoaded.clear();
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x - 90));
    const bool enter = ImGui::InputTextWithHint("##libq", "Search the Library", &m_libQuery, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Search") || enter) m_libLoaded.clear();
    ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x - 90));
    const bool go = ImGui::InputTextWithHint("##libid", "Got an ID? (like 123)", &m_libIdInput, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Go") || go) && !m_libIdInput.empty()) { openAsset(m_libIdInput); m_libIdInput.clear(); return; }
    ImGui::Spacing();

    const std::string key = m_libKind + "|" + m_libQuery;
    if (m_libLoaded != key) {
        m_libLoaded = key;
        m_libList = json::array();
        Online::request("list", {{"kind", m_libKind}, {"query", m_libQuery}, {"sort", "popular"}, {"limit", 60}}, [this, key](const json& r) {
            if (m_libLoaded == key && r.value("ok", false)) m_libList = r.value("assets", json::array());
        });
    }
    if (m_libList.empty()) { ImGui::TextDisabled("Nothing here yet."); return; }

    // Tiles: picture (or speaker), name, creator. Click to open.
    const float tile = 132.0f;
    const int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 12) / (tile + 12)));
    for (size_t i = 0; i < m_libList.size(); ++i) {
        const json& a = m_libList[i];
        if (i % perRow) ImGui::SameLine(0, 12);
        ImGui::PushID((int)i);
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##tile", ImVec2(tile, tile))) openAsset(a.value("id", std::string()));
        const bool hover = ImGui::IsItemHovered();
        int w = 0, h = 0;
        unsigned tex = libraryPicture(a, w, h);
        fitPicture(p, tile, tile, tex, w, h);
        const std::string kind = a.value("kind", std::string());
        if (kind == "audio") speaker(ImVec2(p.x + tile * 0.55f, p.y + tile * 0.5f), tile * 0.6f, IM_COL32(90, 100, 115, 255));
        else if (!tex) {
            const char* t = Online::kindTitle(kind);
            ImVec2 ts = ImGui::CalcTextSize(t);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + (tile - ts.x) * 0.5f, p.y + (tile - ts.y) * 0.5f), IM_COL32(120, 125, 135, 255), t);
        }
        if (hover) ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + tile, p.y + tile), IM_COL32(29, 111, 216, 255), 0.0f, 0, 2.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile);
        ImGui::TextColored(Classic::kLink, "%s", a.value("name", std::string()).c_str());
        ImGui::TextDisabled("by %s", a.value("creatorName", std::string("?")).c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void PlayerApp::drawAsset() {
    if (ImGui::Button("< Library")) { m_assetId.clear(); stopAssetSound(); return; }
    if (m_asset.is_null() || !m_asset.is_object()) {
        ImGui::TextDisabled("%s", m_assetMsg.c_str());
        return;
    }
    const json& a = m_asset;
    const std::string id = a.value("id", std::string()), kind = a.value("kind", std::string());
    // Its number (like a Roblox asset ID); older uploads without one keep their old ID.
    const std::string gbId = a.value("num", 0LL) > 0 ? std::to_string(a.value("num", 0LL)) : "gb:" + id;
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted(a.value("name", std::string()).c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Guts&Bolts %s", Online::kindTitle(kind));
    ImGui::Spacing();

    // Left: the picture. Right: who made it, what it is and what to do with it.
    const float pic = std::min(260.0f, ImGui::GetContentRegionAvail().x);
    const bool stacked = ImGui::GetContentRegionAvail().x < pic + 300;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(pic, pic));
    int w = 0, h = 0;
    unsigned tex = libraryPicture(a, w, h);
    fitPicture(p, pic, pic, tex, w, h);
    if (kind == "audio") speaker(ImVec2(p.x + pic * 0.55f, p.y + pic * 0.5f), pic * 0.55f, IM_COL32(90, 100, 115, 255));
    else if (!tex) {
        const char* t = kind == "decal" ? "Loading..." : Online::kindTitle(kind);
        ImVec2 ts = ImGui::CalcTextSize(t);
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + (pic - ts.x) * 0.5f, p.y + (pic - ts.y) * 0.5f), IM_COL32(120, 125, 135, 255), t);
    }
    if (!stacked) ImGui::SameLine(0, 18);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Creator:");
    ImGui::SameLine();
    json creator = {{"id", a.value("creator", std::string())}, {"name", a.value("creatorName", std::string("?"))},
                    {"verified", a.value("creatorVerified", false)}};
    if (Social::nameLink(creator, "creator")) openProfile(creator["id"].get<std::string>());
    ImGui::TextDisabled("Created: %s", dateText(a.value("created", 0LL)).c_str());
    ImGui::Spacing();
    const std::string desc = a.value("description", std::string());
    if (desc.empty()) ImGui::TextDisabled("No description.");
    else ImGui::TextUnformatted(desc.c_str());
    ImGui::Spacing();

    if (kind == "audio") {   // listen to it right here
        const bool playing = m_assetSound && Audio::isPlaying(m_assetSound);
        if (playing) {
            if (Classic::button("Stop", Classic::kBlue, ImVec2(110, 30))) stopAssetSound();
        } else if (m_assetMsg == "Loading sound...") {
            ImGui::TextDisabled("Loading sound...");
        } else if (Classic::button("Listen", Classic::kPlay, ImVec2(110, 30))) {
            m_assetMsg = "Loading sound...";
            Online::download(id, [this, id](bool ok, const std::filesystem::path& f, const json& info) {
                if (m_assetId != id) return;
                if (!ok) { m_assetMsg = info.value("error", std::string("Couldn't get that sound.")); return; }
                m_assetMsg.clear();
                stopAssetSound();
                m_assetSound = Audio::play(f.string(), 0.8f);
                if (!m_assetSound) m_assetMsg = "This computer couldn't play that sound.";
            });
        }
        if (!m_assetMsg.empty() && m_assetMsg != "Loading sound...") ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "%s", m_assetMsg.c_str());
        ImGui::Spacing();
    }

    // The ID, to copy and paste into a game.
    ImGui::TextUnformatted("Asset ID");
    ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x - 100));
    std::string shown = gbId;
    ImGui::InputText("##assetid", &shown, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::SameLine();
    if (Classic::button("Copy ID", Classic::kBlue, ImVec2(90, 0))) {
        ImGui::SetClipboardText(gbId.c_str());
        m_copiedAt = ImGui::GetTime();
    }
    if (ImGui::GetTime() - m_copiedAt < 3.0) ImGui::TextColored(ImVec4(0.05f, 0.5f, 0.2f, 1), "Copied %s", gbId.c_str());
    if (kind == "decal") ImGui::TextDisabled("Paste it into a Decal's (or Part's) Texture in Studio, or in a script: decal.Texture = \"%s\"", gbId.c_str());
    else if (kind == "audio") ImGui::TextDisabled("Paste it into a Sound's File in Studio, or in a script: sound.SoundId = \"%s\"", gbId.c_str());
    else if (kind == "model") ImGui::TextDisabled("Insert it from Studio's Toolbox (the Library tab).");
    else if (kind == "plugin") ImGui::TextDisabled("Install it from Studio's Toolbox (the Plugins tab).");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}
