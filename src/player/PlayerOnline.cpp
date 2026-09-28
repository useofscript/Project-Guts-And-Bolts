// The site's online side: talking to a Guts&Bolts server for Bolts, the
// catalog, uploads (the Create page), published games and staff tools.
#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/Account.h"
#include "../core/Paths.h"
#include "../game/Badges.h"
#include "../game/Bolts.h"
#include "../game/Profile.h"
#include "../online/AssetCache.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"
#include "../scene/Scene.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>

using namespace Site;
using json = nlohmann::json;

namespace {

// "by Creator [check]" on one line.
void byLine(const json& a) {
    ImGui::TextDisabled("by");
    ImGui::SameLine(0, 4);
    ImGui::TextColored(Classic::kLink, "%s", a.value("creatorName", std::string("?")).c_str());
    if (a.value("creatorVerified", false)) { ImGui::SameLine(0, 3); Badges::check(); }
}

std::string ago(long long t) {
    long long d = Online::unixNow() - t;
    if (d < 120) return "just now";
    if (d < 7200) return std::to_string(d / 60) + " minutes ago";
    if (d < 172800) return std::to_string(d / 3600) + " hours ago";
    return std::to_string(d / 86400) + " days ago";
}

// A title card for a game with no picture yet: a colourful gradient from its id.
void gameCard(ImDrawList* dl, ImVec2 a, ImVec2 b, const std::string& id, const std::string& title) {
    unsigned h = 2166136261u;
    for (char c : id) h = (h ^ (unsigned char)c) * 16777619u;
    auto col = [&](int shift, int base) { return (int)(base + ((h >> shift) & 0x5F)); };
    ImU32 top = IM_COL32(col(0, 40), col(8, 60), col(16, 110), 255), bot = IM_COL32(col(4, 20), col(12, 30), col(20, 60), 255);
    dl->AddRectFilledMultiColor(a, b, top, top, bot, bot);
    ImFont* f = ImGui::GetFont();
    float size = ImGui::GetFontSize() * 1.3f;
    ImVec2 ts = f->CalcTextSizeA(size, b.x - a.x - 16, b.x - a.x - 16, title.c_str());
    dl->AddText(f, size, ImVec2(a.x + (b.x - a.x - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f),
                IM_COL32(255, 255, 255, 235), title.c_str(), nullptr, b.x - a.x - 16);
}

} // namespace

// ---------------------------------------------------------------------------
// Connecting
// ---------------------------------------------------------------------------

void PlayerApp::drawServerButton(ImVec2 at) {
    // A little pill on the banner: green = online, grey = offline, red = trouble.
    Online::Status st = Online::status();
    std::string label = st == Online::Status::Online ? Online::serverInfo().value("name", std::string("Online"))
                      : st == Online::Status::Connecting ? "Connecting..."
                      : st == Online::Status::Failed ? "Server offline" : "Offline";
    ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    ImVec2 b(at.x + ts.x + 30, at.y + 22);
    ImGui::SetCursorScreenPos(at);
    if (ImGui::InvisibleButton("##server", ImVec2(b.x - at.x, b.y - at.y))) {
        m_serverInput = Online::serverAddress();
        m_serverMsg.clear();
        m_showServer = true;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool hover = ImGui::IsItemHovered();
    dl->AddRectFilled(at, b, hover ? IM_COL32(255, 255, 255, 235) : IM_COL32(255, 255, 255, 200), 11.0f);
    ImU32 dot = st == Online::Status::Online ? IM_COL32(40, 190, 80, 255)
              : st == Online::Status::Failed ? IM_COL32(220, 60, 50, 255)
              : st == Online::Status::Connecting ? IM_COL32(240, 180, 40, 255) : IM_COL32(150, 150, 160, 255);
    dl->AddCircleFilled(ImVec2(at.x + 12, at.y + 11), 5.0f, dot);
    dl->AddText(ImVec2(at.x + 22, at.y + 11 - ts.y * 0.5f), IM_COL32(30, 40, 60, 255), label.c_str());
    if (hover) ImGui::SetTooltip("%s\nClick to pick a Guts&Bolts server", Online::statusText().c_str());
}

void PlayerApp::drawServerDialog() {
    if (m_showServer) { ImGui::OpenPopup("Guts&Bolts Server"); m_showServer = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(560), 0));
    if (!ImGui::BeginPopupModal("Guts&Bolts Server", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("A Guts&Bolts server keeps your Bolts, badges and everything people upload (clothes, "
                           "audio, plugins and games) in one place, so everyone sees the same site.");
    ImGui::Spacing();
    ImGui::TextDisabled("Run GutsAndBoltsServer on a computer, then type its address here, like 192.168.1.20 or "
                        "myserver.com:7780. Leave it empty to play offline.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputTextWithHint("##addr", "server address", &m_serverInput, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();
    if (bigButton("Connect", kGreen, ImVec2(130, 32)) || enter) {
        Online::setServerAddress(m_serverInput);
        m_loaded.clear();
        m_serverMsg = m_serverInput.empty() ? "Playing offline." : "Connecting...";
    }
    ImGui::SameLine();
    if (ImGui::Button("Go offline", ImVec2(120, 32))) {
        m_serverInput.clear();
        Online::setServerAddress("");
        m_loaded.clear();
        m_serverMsg = "Playing offline.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 32))) ImGui::CloseCurrentPopup();
    ImGui::Spacing();
    ImVec4 col = Online::online() ? ImVec4(0.3f, 0.85f, 0.4f, 1) : Online::status() == Online::Status::Failed
                 ? ImVec4(1.0f, 0.45f, 0.4f, 1) : ImVec4(0.8f, 0.8f, 0.85f, 1);
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(col, "%s", Online::configured() ? Online::statusText().c_str() : "Offline (no server)");
    ImGui::PopTextWrapPos();
    if (Online::online()) {
        const json& me = Online::me();
        ImGui::Text("Signed in as %s", me.value("name", std::string()).c_str());
        if (me.value("verified", false)) { ImGui::SameLine(0, 4); Badges::check(); }
        ImGui::SameLine();
        ImGui::TextDisabled("(%s Bolts)", Bolts::format(Online::bolts()).c_str());
    }
    ImGui::EndPopup();
}

// Fetch a list from the server (once per visit to a page, or again on request).
void PlayerApp::refreshOnline(const std::string& what) {
    if (!Online::online()) return;
    if (what == "catalog") {
        Online::request("list", {{"kind", "clothing"}, {"limit", 100}}, [this](const json& r) {
            if (r.value("ok", false)) m_onlineItems = r["assets"];
        });
    } else if (what == "games") {
        Online::request("list", {{"kind", "game"}, {"sort", "popular"}, {"limit", 30}}, [this](const json& r) {
            if (r.value("ok", false)) m_onlineGames = r["assets"];
        });
    } else if (what == "mine") {
        Online::request("list", {{"creator", Account::id()}, {"limit", 100}}, [this](const json& r) {
            if (r.value("ok", false)) m_myCreations = r["assets"];
        });
    } else if (what == "history") {
        Online::request("bolts.history", json::object(), [this](const json& r) {
            if (r.value("ok", false)) m_onlineHistory = r["history"];
        });
    }
    if (m_loaded.find(what) == std::string::npos) m_loaded += what + " ";
}

// Bolts for playing, from the server (it checks the timing and the daily cap).
void PlayerApp::onlinePlayTick(float dt) {
    if (dt <= 0 || dt > 5) return;
    m_onlinePlaySeconds += dt;
    if (m_onlinePlaySeconds < Bolts::kPlaySeconds) return;
    m_onlinePlaySeconds = 0;
    Online::request("bolts.play", json::object(), [this](const json& r) {
        if (!r.value("ok", false)) return;
        m_boltsToast = "+" + std::to_string(r.value("got", 0)) + " Bolts for playing!";
        m_boltsToastUntil = ImGui::GetTime() + 4.0;
    });
}

// ---------------------------------------------------------------------------
// Catalog
// ---------------------------------------------------------------------------

void PlayerApp::drawOnlineCatalog() {
    if (m_loaded.find("catalog") == std::string::npos) refreshOnline("catalog");
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Catalog");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Hats, shirts and pants made by the Guts&Bolts community.");
    ImGui::Spacing();
    const char* tabs[] = {"All", "Hats", "Shirts", "Pants"};
    const char* kinds[] = {"", "hat", "shirt", "pants"};
    float tabW = std::min(90.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f);
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        bool on = m_itemType == i - 1;
        if (on ? Classic::button(tabs[i], Classic::kBlue, ImVec2(tabW, 28)) : ImGui::Button(tabs[i], ImVec2(tabW, 28)))
            m_itemType = i - 1;
    }
    if (portraitScreen()) ImGui::Spacing(); else ImGui::SameLine(ImGui::GetContentRegionMax().x - 140);
    if (Classic::button("Create", Classic::kPlay, ImVec2(140, 28))) m_page = Page::Create;
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<int> list;
    for (int i = 0; i < (int)m_onlineItems.size(); ++i) {
        std::string k = m_onlineItems[i].value("kind", std::string());
        if (m_itemType < 0 || k == kinds[m_itemType + 1]) list.push_back(i);
    }
    if (list.empty()) {
        ImGui::Dummy(ImVec2(0, 30));
        ImGui::TextDisabled(Online::pending() ? "Loading..." : "Nothing here yet - be the first to make something on the Create page!");
        return;
    }
    const float tile = 150.0f;
    int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 14) / (tile + 14)));
    for (size_t k = 0; k < list.size(); ++k) {
        const json& a = m_onlineItems[list[k]];
        Catalog::Item it = Catalog::fromServer(a);
        if (k % perRow != 0) ImGui::SameLine(0, 14);
        ImGui::PushID(list[k]);
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##item", ImVec2(tile, tile))) { m_openOnlineItem = list[k]; m_onlineMsg.clear(); }
        bool hover = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + tile, p.y + tile), IM_COL32(255, 255, 255, 255));
        dl->AddRect(p, ImVec2(p.x + tile, p.y + tile), hover ? IM_COL32(40, 120, 230, 255) : IM_COL32(160, 165, 175, 255),
                    0, 0, hover ? 2.0f : 1.0f);
        drawItemIcon(dl, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile * 0.8f, it);
        if (Catalog::isWearing(it)) dl->AddText(ImVec2(p.x + 6, p.y + 4), IM_COL32(20, 140, 60, 255), "Wearing");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile);
        ImGui::TextColored(Classic::kLink, "%s", it.name.c_str());
        ImGui::PopTextWrapPos();
        byLine(a);
        if (Online::owns(it.id) && it.price > 0) ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "Owned");
        else if (it.price > 0) Bolts::amount(it.price);
        else ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "Free");
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void PlayerApp::drawOnlineItemDialog() {
    if (m_openOnlineItem >= (int)m_onlineItems.size()) m_openOnlineItem = -1;
    if (m_openOnlineItem >= 0 && !ImGui::IsPopupOpen("Item##online")) ImGui::OpenPopup("Item##online");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(540), 0));
    if (!ImGui::BeginPopupModal("Item##online", nullptr, ImGuiWindowFlags_NoResize)) return;
    if (m_openOnlineItem < 0) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    const json a = m_onlineItems[m_openOnlineItem];
    Catalog::Item it = Catalog::fromServer(a);

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(170, 170));
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 170, p.y + 170), IM_COL32(245, 246, 250, 255), 6);
    drawItemIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + 85, p.y + 85), 140, it);
    ImGui::SameLine(0, 18);
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted(it.name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("%s", Online::kindTitle(a.value("kind", std::string())));
    byLine(a);
    const bool owned = Online::owns(it.id);
    if (it.price == 0) ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "Free");
    else if (owned) ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "You own this");
    else Bolts::amount(it.price, 20.0f);
    ImGui::TextDisabled("%lld sold  -  made %s", a.value("sales", 0LL), ago(a.value("created", 0LL)).c_str());
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(it.description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Spacing();

    auto wearIt = [this, it]() {
        Catalog::applyLook(it);
        if (Player* pl = m_avatarScene->player()) Profile::get().applyTo(*pl);
    };
    if (!owned) {
        std::string label = it.price > 0 ? "Buy for " + Bolts::format(it.price) : std::string("Get it");
        ImGui::BeginDisabled(m_busy || Online::bolts() < it.price);
        if (bigButton(label.c_str(), kGreen, ImVec2(170, 34))) {
            m_busy = true;
            Online::request("buy", {{"id", it.id}}, [this, wearIt](const json& r) {
                m_busy = false;
                if (r.value("ok", false)) { wearIt(); m_onlineMsg = "It's yours!"; }
                else m_onlineMsg = r.value("error", std::string("Couldn't buy it."));
            });
        }
        ImGui::EndDisabled();
    } else {
        bool wearing = Catalog::isWearing(it);
        ImGui::BeginDisabled(wearing);
        if (bigButton(wearing ? "Wearing" : "Wear", kGreen, ImVec2(140, 34))) wearIt();
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 34))) { m_openOnlineItem = -1; ImGui::CloseCurrentPopup(); }
    if (a.value("creator", std::string()) == Account::id() || Online::staff()) {
        ImGui::SameLine();
        if (bigButton("Delete", ImVec4(0.75f, 0.25f, 0.25f, 1), ImVec2(100, 34))) {
            Online::request("delete", {{"id", it.id}}, [this](const json& r) {
                m_catalogMsg = r.value("ok", false) ? "Deleted." : r.value("error", std::string());
                refreshOnline("catalog");
            });
            m_openOnlineItem = -1;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::TextDisabled("You have");
    ImGui::SameLine();
    Bolts::amount(Online::bolts());
    if (!m_onlineMsg.empty()) ImGui::TextWrapped("%s", m_onlineMsg.c_str());
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Create: upload hats, shirts, pants, audio and plugins
// ---------------------------------------------------------------------------

void PlayerApp::drawCreate() {
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Create");
    ImGui::SetWindowFontScale(1.0f);
    if (!Online::online()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Uploading needs a Guts&Bolts server, so everyone can see what you make.");
        ImGui::PopTextWrapPos();
        if (Classic::button("Pick a server", Classic::kBlue, ImVec2(160, 30))) {
            m_serverInput = Online::serverAddress();
            m_showServer = true;
        }
        return;
    }
    if (m_loaded.find("mine") == std::string::npos) refreshOnline("mine");
    const bool verified = Online::verified();
    const json& me = Online::me();

    // What being Verified means here.
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float boxH = portraitScreen() ? 78.0f : 52.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + boxH), verified ? IM_COL32(225, 240, 255, 255) : IM_COL32(245, 245, 248, 255), 6);
    dl->AddRect(p, ImVec2(p.x + w, p.y + boxH), verified ? IM_COL32(29, 155, 240, 255) : IM_COL32(190, 195, 205, 255), 6);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 10, p.y + 8));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(p.x + w - 10);
    if (verified) {
        Badges::check();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.05f, 0.35f, 0.7f, 1), "You're Verified!");
        ImGui::TextDisabled("Uploading is free, there's no daily limit, and you can sell what you make.");
    } else {
        ImGui::TextUnformatted("Uploading costs a few Bolts (clothes 10, audio 20, plugins 20).");
        ImGui::TextDisabled("%d uploads left today. Verified creators upload for free, with no limit, and can sell "
                            "their creations - ask the staff!", me.value("uploadsLeft", 0));
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + boxH + 10));

    const char* tabs[] = {"Hat", "Shirt", "Pants", "Audio", "Plugin"};
    const char* kinds[] = {"hat", "shirt", "pants", "audio", "plugin"};
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ImGui::SameLine();
        float tw = std::min(84.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / (5 - i));
        bool on = m_createKind == i;
        if (on ? Classic::button(tabs[i], Classic::kBlue, ImVec2(tw, 28)) : ImGui::Button(tabs[i], ImVec2(tw, 28))) {
            m_createKind = i;
            m_createMsg.clear();
        }
    }
    ImGui::Spacing();
    const std::string kind = kinds[m_createKind];
    const bool clothing = Online::isClothing(kind);
    float fieldW = std::min(360.0f, ImGui::GetContentRegionAvail().x - 110);

    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputTextWithHint("Name", "Give it a name", &m_createName);
    ImGui::InputTextMultiline("Description", &m_createDesc, ImVec2(fieldW, 60));
    if (clothing) {
        if (kind == "hat") {
            const char* styles[] = {"Top Hat", "Cap", "Crown"};
            int s = std::clamp(m_createStyle - 1, 0, 2);
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::Combo("Style", &s, styles, 3)) m_createStyle = s + 1;
        }
        ImGui::SetNextItemWidth(fieldW);
        ImGui::ColorEdit3("Colour", &m_createColor.x);
    } else {
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputTextWithHint("File", kind == "audio" ? "C:/music/song.mp3" : "C:/plugins/myplugin.lua", &m_createPath);
        ImGui::TextDisabled(kind == "audio" ? "An .mp3, .wav, .ogg or .flac file (up to 6 MB)."
                                            : "A Lua plugin for Studio (see the README for how plugins work).");
    }
    if (verified) {
        ImGui::SetNextItemWidth(fieldW);
        if (ImGui::InputInt("Price (Bolts)", &m_createPrice, 5, 50)) m_createPrice = std::clamp(m_createPrice, 0, 1000000);
        ImGui::TextDisabled("0 = free. You get %d%% of every sale.", Online::kCreatorSharePercent);
    }
    ImGui::EndGroup();
    if (clothing && !portraitScreen()) {
        ImGui::SameLine(0, 24);
        Catalog::Item preview;
        preview.type = kind == "hat" ? Catalog::Type::Hat : kind == "shirt" ? Catalog::Type::Shirt : Catalog::Type::Pants;
        preview.hat = (HatStyle)m_createStyle;
        preview.color = m_createColor;
        ImVec2 q = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(150, 150));
        ImGui::GetWindowDrawList()->AddRectFilled(q, ImVec2(q.x + 150, q.y + 150), IM_COL32(255, 255, 255, 255), 6);
        drawItemIcon(ImGui::GetWindowDrawList(), ImVec2(q.x + 75, q.y + 75), 120, preview);
    }

    ImGui::Spacing();
    long long fee = verified ? 0 : Online::uploadFee(kind);
    std::string label = fee > 0 ? "Upload for " + std::to_string(fee) + " Bolts" : std::string("Upload (free)");
    ImGui::BeginDisabled(m_busy);
    if (Classic::button(m_busy ? "Uploading..." : label.c_str(), Classic::kPlay, ImVec2(200, 34))) {
        json args = {{"kind", kind}, {"name", m_createName}, {"description", m_createDesc}, {"price", verified ? m_createPrice : 0}};
        bool ok = true;
        if (clothing) {
            args["meta"] = {{"color", {(int)std::lround(m_createColor.r * 255), (int)std::lround(m_createColor.g * 255),
                                       (int)std::lround(m_createColor.b * 255)}}};
            if (kind == "hat") args["meta"]["style"] = m_createStyle;
            args["data"] = "";
        } else {
            std::string path = m_createPath;
            if (path.size() > 1 && path.front() == '"' && path.back() == '"') path = path.substr(1, path.size() - 2);
            std::ifstream f(path, std::ios::binary);
            if (!f) { m_createMsg = "Couldn't open that file. Check the path."; ok = false; }
            else {
                std::stringstream buf;
                buf << f.rdbuf();
                args["data"] = Online::base64Encode(buf.str());
                if (kind == "audio") {
                    std::string ext = std::filesystem::path(path).extension().string();
                    if (!ext.empty()) ext.erase(0, 1);
                    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
                    args["meta"] = {{"ext", ext}};
                }
            }
        }
        if (ok) {
            m_busy = true;
            m_createMsg = "Uploading...";
            Online::request("upload", args, [this](const json& r) {
                m_busy = false;
                if (r.value("ok", false)) {
                    m_createMsg = "Uploaded \"" + r["asset"].value("name", std::string()) + "\"!";
                    long long paid = r.value("fee", 0LL);
                    if (paid > 0) m_createMsg += " (" + std::to_string(paid) + " Bolts)";
                    m_createName.clear(); m_createDesc.clear(); m_createPath.clear();
                    refreshOnline("mine");
                    refreshOnline("catalog");
                } else {
                    m_createMsg = r.value("error", std::string("The upload didn't work."));
                }
            }, 120);
        }
    }
    ImGui::EndDisabled();
    if (!m_createMsg.empty()) { ImGui::SameLine(); ImGui::TextWrapped("%s", m_createMsg.c_str()); }
    ImGui::TextDisabled("Games are published from Studio (File > Publish to Guts&Bolts).");

    ImGui::SeparatorText("My creations");
    if (m_myCreations.empty()) ImGui::TextDisabled("Nothing yet.");
    for (size_t i = 0; i < m_myCreations.size(); ++i) {
        const json& a = m_myCreations[i];
        ImGui::PushID((int)i);
        ImGui::TextUnformatted(a.value("name", std::string()).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Online::kindTitle(a.value("kind", std::string())));
        ImGui::SameLine();
        long long price = a.value("price", 0LL);
        if (price > 0) Bolts::amount(price); else ImGui::TextDisabled("free");
        ImGui::SameLine();
        if (a.value("kind", std::string()) == "game") ImGui::TextDisabled("- %lld plays", a.value("plays", 0LL));
        else ImGui::TextDisabled("- %lld sold", a.value("sales", 0LL));
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            Online::request("delete", {{"id", a.value("id", std::string())}}, [this](const json&) {
                refreshOnline("mine");
                refreshOnline("catalog");
            });
        }
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------------
// Games published to the server
// ---------------------------------------------------------------------------

void PlayerApp::drawOnlineGames() {
    if (!Online::online()) return;
    if (m_loaded.find("games") == std::string::npos) refreshOnline("games");
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextUnformatted("Online Games");
    ImGui::SetWindowFontScale(1.0f);
    if (m_onlineGames.empty()) {
        ImGui::TextDisabled(Online::pending() ? "Loading..." : "No games published yet. Publish one from Studio!");
        ImGui::Separator();
        return;
    }
    const float tw = 196.0f, th = 110.0f;
    int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 14) / (tw + 14)));
    for (size_t i = 0; i < m_onlineGames.size() && (int)i < perRow * 2; ++i) {
        const json& g = m_onlineGames[i];
        if (i % perRow != 0) ImGui::SameLine(0, 14);
        ImGui::PushID((int)i);
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##g", ImVec2(tw, th))) { m_openOnlineGame = (int)i; m_onlineMsg.clear(); }
        gameCard(ImGui::GetWindowDrawList(), p, ImVec2(p.x + tw, p.y + th), g.value("id", std::string()),
                 g.value("name", std::string()));
        if (ImGui::IsItemHovered()) ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + tw, p.y + th), IM_COL32(255, 255, 255, 200), 0.0f, 0, 2.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tw);
        ImGui::TextColored(Classic::kLink, "%s", g.value("name", std::string()).c_str());
        ImGui::PopTextWrapPos();
        byLine(g);
        ImGui::TextDisabled("%lld plays", g.value("plays", 0LL));
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::Separator();
}

void PlayerApp::drawOnlineGameDialog() {
    if (m_openOnlineGame >= (int)m_onlineGames.size()) m_openOnlineGame = -1;
    if (m_openOnlineGame >= 0 && !ImGui::IsPopupOpen("Game##online")) ImGui::OpenPopup("Game##online");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(520), 0));
    if (!ImGui::BeginPopupModal("Game##online", nullptr, ImGuiWindowFlags_NoResize)) return;
    if (m_openOnlineGame < 0) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    const json g = m_onlineGames[m_openOnlineGame];
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(w, 150));
    gameCard(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + 150), g.value("id", std::string()), g.value("name", std::string()));
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted(g.value("name", std::string()).c_str());
    ImGui::SetWindowFontScale(1.0f);
    byLine(g);
    ImGui::TextDisabled("%lld plays  -  published %s", g.value("plays", 0LL), ago(g.value("created", 0LL)).c_str());
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(g.value("description", std::string()).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::BeginDisabled(m_busy);
    if (bigButton(m_busy ? "Downloading..." : "Play", kGreen, ImVec2(160, 38))) {
        m_busy = true;
        // Always fetch the newest version (the creator may have updated it).
        Online::download(g.value("id", std::string()), [this](bool ok, const std::filesystem::path& file, const json& info) {
            m_busy = false;
            if (!ok) { m_onlineMsg = info.value("error", std::string("Couldn't download it.")); return; }
            m_openOnlineGame = -1;
            joinGame(file);
            Online::fetchSounds(*m_scene);
        }, true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 38))) { m_openOnlineGame = -1; ImGui::CloseCurrentPopup(); }
    if (!m_onlineMsg.empty()) ImGui::TextWrapped("%s", m_onlineMsg.c_str());
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Bolts (online)
// ---------------------------------------------------------------------------

void PlayerApp::drawOnlineBolts() {
    if (m_loaded.find("history") == std::string::npos) refreshOnline("history");
    const json& me = Online::me();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    Bolts::drawIcon(dl, ImVec2(p.x + 30, p.y + 30), 58.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 72, p.y + 2));
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Bolts");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Kept safe on %s. Earn them, then spend them in the Catalog.",
                        Online::serverInfo().value("name", std::string("the server")).c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(p.y + 66, ImGui::GetItemRectMax().y + 8)));
    ImGui::SetWindowFontScale(1.6f);
    Bolts::amount(Online::bolts(), 30.0f);
    ImGui::SetWindowFontScale(1.0f);

    ImGui::SeparatorText("Daily reward");
    bool can = me.value("canDaily", false);
    ImGui::BeginDisabled(!can || m_busy);
    if (Classic::button(can ? "Claim 25 Bolts" : "Claimed today", Classic::kPlay, ImVec2(200, 34))) {
        m_busy = true;
        Online::request("bolts.daily", json::object(), [this](const json& r) {
            m_busy = false;
            m_boltsMsg = r.value("ok", false) ? "You got 25 Bolts! Come back tomorrow for more." : r.value("error", std::string());
            refreshOnline("history");
        });
    }
    ImGui::EndDisabled();
    if (!m_boltsMsg.empty()) ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.15f, 1), "%s", m_boltsMsg.c_str());

    ImGui::SeparatorText("Ways to earn");
    ImGui::PushTextWrapPos(0);
    ImGui::Bullet(); ImGui::TextWrapped("Come back every day: 25 Bolts.");
    ImGui::Bullet(); ImGui::TextWrapped("Play games: 5 Bolts for every 5 minutes (up to 50 a day - %lld so far today).",
                                        me.value("playEarnedToday", 0LL));
    ImGui::Bullet(); ImGui::TextWrapped("Sell your creations (Verified creators): you get %d%% of each sale.",
                                        Online::kCreatorSharePercent);
    ImGui::PopTextWrapPos();

    ImGui::SeparatorText("Redeem a Bolts code");
    ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x - 90));
    ImGui::InputTextWithHint("##boltscode", "BOLTS-...", &m_boltsCode);
    ImGui::SameLine();
    if (Classic::button("Redeem", Classic::kBlue)) {
        Online::request("bolts.redeem", {{"code", m_boltsCode}}, [this](const json& r) {
            m_boltsMsg = r.value("ok", false) ? "You got " + Bolts::format(r.value("got", 0LL)) + " Bolts!" : r.value("error", std::string());
            refreshOnline("history");
        });
        m_boltsCode.clear();
    }

    ImGui::SeparatorText("History");
    const bool narrow = ImGui::GetContentRegionAvail().x < 560;
    if (ImGui::BeginTable("##onlinehistory", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, narrow ? 64.0f : 150.0f);
        ImGui::TableSetupColumn("What");
        ImGui::TableSetupColumn("Bolts", ImGuiTableColumnFlags_WidthFixed, narrow ? 64.0f : 90.0f);
        for (size_t k = m_onlineHistory.size(); k-- > 0;) {
            const json& e = m_onlineHistory[k];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::time_t t = (std::time_t)e.value("time", 0LL);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char when[32];
            std::strftime(when, sizeof(when), narrow ? "%b %d" : "%b %d, %H:%M", &tm);
            ImGui::TextDisabled("%s", t ? when : "");
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", e.value("reason", std::string()).c_str());
            ImGui::TableNextColumn();
            long long amt = e.value("amount", 0LL);
            if (amt >= 0) ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "+%s", Bolts::format(amt).c_str());
            else ImGui::TextColored(ImVec4(0.75f, 0.2f, 0.15f, 1), "%s", Bolts::format(amt).c_str());
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------
// Staff tools on the server
// ---------------------------------------------------------------------------

void PlayerApp::drawOnlineStaff() {
    ImGui::SeparatorText("People on the server");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Find someone by name (or the start of their account ID) and verify them right here - "
                        "no codes needed. They get the badge next time they open the site.");
    ImGui::PopTextWrapPos();
    const bool official = Account::iAmStaff();
    ImGui::SetNextItemWidth(std::min(320.0f, ImGui::GetContentRegionAvail().x - 90));
    bool enter = ImGui::InputTextWithHint("##find", "name or ID", &m_findQuery, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (Classic::button("Search", Classic::kBlue) || enter) {
        Online::request("admin.find", {{"query", m_findQuery}}, [this](const json& r) {
            if (r.value("ok", false)) m_foundUsers = r["users"];
            else m_staffMsg = r.value("error", std::string());
        });
    }
    if (official) {
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Bolts to give", &m_giveServerBolts, 25, 100)) m_giveServerBolts = std::clamp(m_giveServerBolts, -1000000, 1000000);
    }
    if (!m_staffMsg.empty()) ImGui::TextWrapped("%s", m_staffMsg.c_str());

    auto updateRow = [this](const json& r) {
        if (!r.value("ok", false)) { m_staffMsg = r.value("error", std::string()); return; }
        if (!r.contains("user")) return;
        for (auto& u : m_foundUsers)
            if (u.value("id", std::string()) == r["user"].value("id", std::string())) u = r["user"];
        m_staffMsg = "Done.";
        if (r.contains("bolts")) m_staffMsg = "Done. They have " + Bolts::format(r["bolts"].get<long long>()) + " Bolts now.";
    };
    for (size_t i = 0; i < m_foundUsers.size(); ++i) {
        const json u = m_foundUsers[i];
        std::string id = u.value("id", std::string());
        ImGui::PushID((int)i);
        ImGui::TextUnformatted(u.value("name", std::string()).c_str());
        if (u.value("verified", false)) { ImGui::SameLine(0, 3); Badges::check(); }
        ImGui::SameLine();
        ImGui::TextDisabled("%s...%s%s", id.substr(0, 8).c_str(), u.value("staff", false) ? "  Staff" : "",
                            u.value("banned", false) ? "  BANNED" : "");
        ImGui::SameLine();
        bool isVerified = u.value("verified", false);
        if (!u.value("official", false)) {
            if (ImGui::SmallButton(isVerified ? "Unverify" : "Verify")) {
                if (isVerified) {
                    Online::request("admin.revoke", {{"to", id}, {"key", "verified"}}, updateRow);
                } else {
                    std::string err;
                    Badges::Grant g = Badges::makeGrant(Badges::Id::Verified, id, err);
                    if (g.first.empty()) m_staffMsg = err;
                    else Online::request("admin.grant", {{"to", id}, {"key", g.first}, {"sig", g.second}}, updateRow);
                }
            }
            if (official) {
                ImGui::SameLine();
                bool staff = u.value("staff", false);
                if (ImGui::SmallButton(staff ? "Remove Staff" : "Make Staff")) {
                    if (staff) Online::request("admin.revoke", {{"to", id}, {"key", "staff"}}, updateRow);
                    else {
                        std::string err;
                        Badges::Grant g = Badges::makeGrant(Badges::Id::Staff, id, err);
                        Online::request("admin.grant", {{"to", id}, {"key", g.first}, {"sig", g.second}}, updateRow);
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Give Bolts"))
                    Online::request("admin.giveBolts", {{"to", id}, {"amount", m_giveServerBolts}}, updateRow);
                ImGui::SameLine();
                bool banned = u.value("banned", false);
                if (ImGui::SmallButton(banned ? "Unban" : "Ban"))
                    Online::request("admin.ban", {{"to", id}, {"on", !banned}}, updateRow);
            }
        }
        ImGui::PopID();
    }
}
