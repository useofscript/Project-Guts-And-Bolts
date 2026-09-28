#include "PlayerApp.h"
#include "../core/AppWindow.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../core/Settings.h"
#include "../game/GameSession.h"
#include "../game/Hud.h"
#include "../game/Profile.h"
#include "../game/SettingsWindow.h"
#include "../game/UpdateToast.h"
#include "../renderer/SceneRenderer.h"
#include "../scene/Physics.h"
#include "../scene/Serializer.h"
#include "../net/NetGame.h"
#include "../core/Audio.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cctype>

namespace {

const ImVec4 kAccent   = {0.26f, 0.55f, 0.96f, 1.0f};
const ImVec4 kGreen    = {0.20f, 0.68f, 0.32f, 1.0f};
const ImVec4 kCardBg   = {0.16f, 0.17f, 0.20f, 1.0f};
const ImVec4 kTopBarBg = {0.086f, 0.094f, 0.114f, 1.0f};

bool bigButton(const char* label, ImVec4 col, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, col);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x + 0.08f, col.y + 0.08f, col.z + 0.08f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(col.x - 0.05f, col.y - 0.05f, col.z - 0.05f, 1));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return r;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Put the camera behind / above whatever the spawn point is.
void frameSpawn(Scene& scene, Camera& cam) {
    glm::vec3 target(0.0f, 1.0f, 0.0f);
    if (SceneNode* s = scene.root()->findChild("SpawnLocation", true))
        target = glm::vec3(s->worldMatrix()[3]) + glm::vec3(0, 1.5f, 0);
    cam.pivot    = target;
    cam.yaw      = 45.0f;
    cam.pitch    = 28.0f;
    cam.distance = 22.0f;
}

// --- 2011-style look -----------------------------------------------------------
namespace Classic {
const ImU32  kSkyTop    = IM_COL32(22, 70, 148, 255);
const ImU32  kSkyBottom = IM_COL32(110, 170, 232, 255);
const ImU32  kNavTop    = IM_COL32(64, 146, 232, 255);
const ImU32  kNavBottom = IM_COL32(16, 96, 186, 255);
const ImU32  kStripeA   = IM_COL32(255, 255, 255, 255);
const ImU32  kStripeB   = IM_COL32(236, 239, 244, 255);
const ImVec4 kInk       = {0.16f, 0.17f, 0.20f, 1.0f};
const ImVec4 kInkDim    = {0.42f, 0.44f, 0.50f, 1.0f};
const ImVec4 kLink      = {0.02f, 0.33f, 0.74f, 1.0f};
const ImVec4 kPlay      = {0.02f, 0.66f, 0.30f, 1.0f};
const ImVec4 kBlue      = {0.10f, 0.45f, 0.82f, 1.0f};

// Dark text and light widgets for the white striped panel.
void pushLight() {
    ImGui::PushStyleColor(ImGuiCol_Text, kInk);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, kInkDim);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.93f, 0.96f, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.88f, 0.93f, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.93f, 0.93f, 0.94f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.91f, 0.98f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.86f, 0.97f, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.72f, 0.74f, 0.78f, 1));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.78f, 0.8f, 0.84f, 1));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, kBlue);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.9f, 0.91f, 0.93f, 1));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.7f, 0.72f, 0.76f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
}
void popLight() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(14);
}

bool button(const char* label, ImVec4 col, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(col.x * 0.7f, col.y * 0.7f, col.z * 0.7f, 1));
    bool r = bigButton(label, col, size);
    ImGui::PopStyleColor(2);
    return r;
}

void stripes(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    dl->AddRectFilled(a, b, kStripeB);
    dl->PushClipRect(a, b, true);
    float h = b.y - a.y;
    for (float x = a.x - h; x < b.x; x += 18.0f)
        dl->AddLine(ImVec2(x, b.y), ImVec2(x + h, a.y), kStripeA, 9.0f);
    dl->PopClipRect();
    dl->AddRect(a, b, IM_COL32(150, 160, 180, 255));
}

// Big chunky logo text with an outline, like the old logo.
void logo(ImDrawList* dl, ImVec2 p, float size, const char* text) {
    ImFont* f = ImGui::GetFont();
    for (int dx = -3; dx <= 3; ++dx)
        for (int dy = -3; dy <= 3; ++dy)
            if (dx * dx + dy * dy >= 4)
                dl->AddText(f, size, ImVec2(p.x + dx, p.y + dy + 2), IM_COL32(40, 10, 10, 255), text);
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            dl->AddText(f, size, ImVec2(p.x + dx, p.y + dy), IM_COL32(255, 255, 255, 255), text);
    dl->AddText(f, size, p, IM_COL32(222, 34, 28, 255), text);
}
} // namespace Classic

} // namespace

PlayerApp::PlayerApp(PlayerOptions opts) : m_opts(std::move(opts)) {
    m_window = std::make_unique<AppWindow>("Guts&Bolts Player", 1280, 720, "player_layout.ini");
    m_window->setFixedTimestep(!m_opts.screenshot.empty());
    m_renderer = std::make_unique<SceneRenderer>();
    m_scene    = std::make_unique<Scene>();
    m_session  = std::make_unique<GameSession>(m_scene.get());
    m_soloChat = std::make_unique<ChatLog>();
    buildAvatarStage();
    refreshGames();

    if (m_opts.page == "avatar") m_page = Page::Avatar;
    if (m_opts.page == "games") m_page = Page::Games;
    if (m_opts.page.rfind("game:", 0) == 0) { m_selected = std::atoi(m_opts.page.c_str() + 5); m_page = Page::GameInfo; }
    if (m_opts.page == "settings") m_showSettings = true;
    if (!m_opts.game.empty()) joinGame(m_opts.game, m_opts.host);
    if (!m_opts.join.empty()) joinServer(m_opts.join);
}

PlayerApp::~PlayerApp() {
    m_server.reset();
    m_client.reset();
    if (m_session) m_session->stop();
    m_session.reset();
    m_games.clear();
    m_avatarScene.reset();
    m_scene.reset();
    m_renderer.reset();
    m_view.resize(0, 0);
    m_avatarView.resize(0, 0);
    m_window.reset();
}

void PlayerApp::run() {
    while (!m_window->shouldClose()) {
        ++m_frame;
        float dt = m_window->beginFrame([&] {
            if (!m_opts.holdKey.empty() && m_frame > 3) {
                ImGuiKey k = m_opts.holdKey == "Space" ? ImGuiKey_Space
                           : (ImGuiKey)(ImGuiKey_A + (m_opts.holdKey[0] - 'A'));
                ImGui::GetIO().AddKeyEvent(k, true);
            }
        });
        frame(dt);
        if (!m_opts.say.empty() && m_frame == 90 && m_page == Page::Game) sendChat(m_opts.say);
        bool shoot = !m_opts.screenshot.empty() && m_frame == m_opts.frames;
        m_window->endFrame(shoot ? m_opts.screenshot : std::string());
        if (shoot) m_window->close();
    }
}

// ---------------------------------------------------------------------------
// Games list
// ---------------------------------------------------------------------------

void PlayerApp::refreshGames() {
    m_games.clear();
    for (const auto& path : Paths::listGames()) {
        GameCard card;
        card.path = path;
        std::string text;
        Scene preview;
        if (!Serializer::readFile(path.string(), text) || !Serializer::loadScene(preview, text)) {
            card.broken = true;
            card.info.title = path.stem().string();
            card.info.description = "This game file couldn't be read.";
        } else {
            card.info = preview.info();
            card.gore = preview.world().gore != GoreLevel::Off;
            card.ragdoll = preview.world().deathStyle == DeathStyle::Ragdoll;
            if (card.info.title.empty() || card.info.title == "My Game") card.info.title = path.stem().string();
            // Render a little picture of the game for its card.
            card.thumb = std::make_unique<Framebuffer>();
            card.thumb->resize(384, 216);
            Camera cam;
            cam.resize(384, 216);
            frameSpawn(preview, cam);
            m_renderer->render(preview, cam, *card.thumb, false);
        }
        m_games.push_back(std::move(card));
    }
}

void PlayerApp::joinGame(const std::filesystem::path& path, bool host) {
    std::string text, err;
    if (!Serializer::readFile(path.string(), text) || !Serializer::loadScene(*m_scene, text, &err)) {
        m_status = "Couldn't load " + path.filename().string() + (err.empty() ? "" : ": " + err);
        m_page = Page::Home;
        return;
    }
    Profile& me = Profile::get();
    if (Player* p = m_scene->player()) {
        me.applyTo(*p);
        if (SceneNode* r = p->root()) r->name = me.name;   // like Roblox: the character is named after you
    }
    m_session->scripts().setPlayerName(me.name);
    *m_soloChat = ChatLog{};

    m_currentTitle = m_scene->info().title;
    m_window->setTitle(m_currentTitle + " - Guts&Bolts Player");
    frameSpawn(*m_scene, m_camera);
    m_camera.distance = 12.0f;
    m_camera.pitch = 20.0f;
    m_camera.yaw = 90.0f;        // behind the character, looking the way it faces (-Z)
    m_paused = false;
    m_status.clear();
    Log::clear();
    if (host) {
        m_server = std::make_unique<NetServer>(m_scene.get(), m_session.get());
        std::string herr;
        if (!m_server->start(kDefaultPort, herr)) {
            m_status = "Couldn't host: " + herr;
            m_server.reset();
        }
    }
    m_session->start();
    m_page = Page::Game;
}

void PlayerApp::joinServer(const std::string& address) {
    std::string host = address;
    int port = kDefaultPort;
    size_t colon = address.rfind(':');
    if (colon != std::string::npos) {
        host = address.substr(0, colon);
        port = std::atoi(address.c_str() + colon + 1);
        if (port <= 0) port = kDefaultPort;
    }
    if (host.empty()) host = "127.0.0.1";
    m_client = std::make_unique<NetClient>(m_scene.get(), m_session.get());
    if (!m_client->connect(host, port)) {
        m_status = m_client->error();
        m_client.reset();
        return;
    }
    m_currentTitle = "Joining " + address + "...";
    m_paused = false;
    m_status.clear();
    Log::clear();
    m_page = Page::Game;
}

ChatLog& PlayerApp::chat() {
    if (m_server) return m_server->chat();
    if (m_client) return m_client->chat();
    return *m_soloChat;
}

void PlayerApp::sendChat(const std::string& text) {
    if (text.empty()) return;
    if (m_server)      m_server->say(text);
    else if (m_client) m_client->say(text);
    else               m_soloChat->add(Profile::get().name, text);
}

void PlayerApp::leaveGame() {
    m_server.reset();
    m_client.reset();
    m_session->stop();
    m_session->setRole(GameSession::Role::Solo);
    m_scene->buildDefault();
    m_paused = false;
    m_page = Page::Home;
    m_window->setTitle("Guts&Bolts Player");
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void PlayerApp::frame(float dt) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##PlayerHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    if (m_page == Page::Game) {
        drawGame(dt);
    } else {
        // Sky background with the site in a column down the middle.
        ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                    Classic::kSkyTop, Classic::kSkyTop, Classic::kSkyBottom, Classic::kSkyBottom);
        float width = std::min(1000.0f, size.x - 32.0f);
        ImVec2 col(pos.x + (size.x - width) * 0.5f, pos.y + 10.0f);
        drawTopBar(col, width);

        float top = ImGui::GetCursorScreenPos().y;
        ImGui::SetCursorScreenPos(ImVec2(col.x, top));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 14));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.88f, 0.9f, 0.93f, 1));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.62f, 0.66f, 0.72f, 1));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.5f, 0.56f, 0.66f, 1));
        ImGui::BeginChild("##content", ImVec2(width, pos.y + size.y - top - 10), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImVec2 cpos = ImGui::GetWindowPos(), csize = ImGui::GetWindowSize();
        Classic::stripes(ImGui::GetWindowDrawList(), cpos, ImVec2(cpos.x + csize.x, cpos.y + csize.y));
        Classic::pushLight();
        switch (m_page) {
            case Page::Home:     drawHome(); break;
            case Page::Games:    drawGames(); break;
            case Page::Avatar:   drawAvatar(dt); break;
            case Page::GameInfo: drawGameInfo(); break;
            default: break;
        }
        Classic::popLight();
        ImGui::EndChild();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }

    ImGui::End();
    SettingsWindow::draw(&m_showSettings);
    drawJoinDialog();
    if (m_page != Page::Game && UpdateToast::draw("GutsAndBoltsPlayer")) m_window->close();
}

void PlayerApp::drawJoinDialog() {
    if (m_showJoin) { ImGui::OpenPopup("Join a Friend"); m_showJoin = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0));
    if (ImGui::BeginPopupModal("Join a Friend", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("Ask your friend to click Host on a game, then type the address it shows "
                           "(like 192.168.1.20). On the same computer, use 127.0.0.1.");
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputTextWithHint("##addr", "address, e.g. 192.168.1.20 or host:7777",
                                              &m_joinAddress, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        if (bigButton("Join", kGreen, ImVec2(120, 34)) || enter) {
            ImGui::CloseCurrentPopup();
            joinServer(m_joinAddress);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 34))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void PlayerApp::drawTopBar(ImVec2 pos, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Profile& me = Profile::get();

    // --- Banner: your avatar standing in the sky, with the logo ---
    const float bannerH = 118.0f;
    m_bannerView.resize((int)width, (int)bannerH);
    Camera cam;
    cam.resize((int)width, (int)bannerH);
    cam.fov = 30.0f;
    cam.pivot = {-5.2f, 1.45f, 0.0f};      // look left of the avatar so it stands on the right
    cam.yaw = 90.0f;
    cam.pitch = 3.0f;
    cam.distance = 10.5f;
    m_renderer->render(*m_avatarScene, cam, m_bannerView, false);
    ImVec2 b0 = pos, b1(pos.x + width, pos.y + bannerH);
    dl->AddImageRounded((ImTextureID)(intptr_t)m_bannerView.colorTexture(), b0, b1, ImVec2(0, 1), ImVec2(1, 0),
                        IM_COL32_WHITE, 8.0f, ImDrawFlags_RoundCornersTop);
    Classic::logo(dl, ImVec2(pos.x + 26, pos.y + 24), 64.0f, "GUTS&BOLTS");

    // Account box, top-right of the banner.
    std::string hi = "Hi, " + me.name;
    ImVec2 ts = ImGui::CalcTextSize(hi.c_str());
    ImVec2 a(b1.x - ts.x - 34, pos.y + 10), c(b1.x - 10, pos.y + 10 + 50);
    dl->AddRectFilled(a, c, IM_COL32(255, 255, 255, 215), 5.0f);
    dl->AddRect(a, c, IM_COL32(120, 140, 170, 255), 5.0f);
    dl->AddText(ImVec2(a.x + 12, a.y + 7), IM_COL32(30, 30, 40, 255), hi.c_str());
    ImGui::SetCursorScreenPos(ImVec2(a.x + 12, a.y + 27));
    ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    if (ImGui::SmallButton("Edit avatar")) m_page = Page::Avatar;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    // --- The blue nav bar ---
    const float navH = 34.0f;
    ImVec2 n0(pos.x, b1.y), n1(pos.x + width, b1.y + navH);
    dl->AddRectFilledMultiColor(n0, n1, Classic::kNavTop, Classic::kNavTop, Classic::kNavBottom, Classic::kNavBottom);
    dl->AddLine(ImVec2(n0.x, n1.y - 1), ImVec2(n1.x, n1.y - 1), IM_COL32(10, 60, 130, 255));

    struct Item { const char* label; int action; };
    const Item items[] = {{"Home", 0}, {"Games", 1}, {"Avatar", 2}, {"Join a Friend", 3},
                          {"Develop", 4}, {"Settings", 5}};
    float x = n0.x + 14;
    for (const Item& it : items) {
        ImVec2 sz = ImGui::CalcTextSize(it.label);
        ImVec2 p0(x - 8, n0.y), p1(x + sz.x + 8, n1.y);
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(it.label);
        bool clicked = ImGui::InvisibleButton("##nav", ImVec2(p1.x - p0.x, navH));
        ImGui::PopID();
        bool active = (it.action == 0 && m_page == Page::Home) || (it.action == 1 && m_page == Page::Games) ||
                      (it.action == 2 && m_page == Page::Avatar);
        if (ImGui::IsItemHovered() || active)
            dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, active ? 60 : 35));
        dl->AddText(ImVec2(x + 1, n0.y + (navH - sz.y) * 0.5f + 1), IM_COL32(0, 30, 80, 180), it.label);
        dl->AddText(ImVec2(x, n0.y + (navH - sz.y) * 0.5f), IM_COL32(255, 255, 255, 255), it.label);
        if (clicked) {
            switch (it.action) {
                case 0: m_page = Page::Home; break;
                case 1: m_page = Page::Games; m_category = "all"; break;
                case 2: m_page = Page::Avatar; break;
                case 3: m_showJoin = true; break;
                case 4:
                    if (!Paths::launch(Paths::sibling("GutsAndBolts")))
                        m_status = "Couldn't find the Guts and Bolts editor next to this app.";
                    break;
                case 5: m_showSettings = true; break;
            }
        }
        x += sz.x + 26;
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, n1.y));
}

bool PlayerApp::drawTile(int index) {
    GameCard& g = m_games[index];
    const float w = 150.0f, h = 112.0f;
    ImGui::PushID(index);
    ImGui::BeginGroup();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##tile", ImVec2(w, h));
    bool hover = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g.thumb)   // crop the 16:9 picture to a 4:3 tile
        dl->AddImage((ImTextureID)(intptr_t)g.thumb->colorTexture(), p, ImVec2(p.x + w, p.y + h),
                     ImVec2(0.125f, 1), ImVec2(0.875f, 0));
    else
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(120, 40, 40, 255));
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), hover ? IM_COL32(40, 120, 230, 255) : IM_COL32(160, 165, 175, 255),
                0.0f, 0, hover ? 2.0f : 1.0f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
    std::string title = g.info.title.size() > 34 ? g.info.title.substr(0, 32) + "..." : g.info.title;
    ImGui::TextColored(Classic::kLink, "%s", title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TextDisabled("by %s", g.info.author.c_str());
    ImGui::Dummy(ImVec2(w, 0));
    ImGui::EndGroup();
    if (hover && !g.info.description.empty()) ImGui::SetTooltip("%s", g.info.description.c_str());
    ImGui::PopID();
    if (clicked) { m_selected = index; m_page = Page::GameInfo; }
    return clicked;
}

void PlayerApp::drawRow(const char* title, const std::vector<int>& games, const char* seeAll) {
    if (games.empty()) return;
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);
    if (seeAll) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 70);
        ImGui::PushID(title);
        if (Classic::button("See All", Classic::kBlue, ImVec2(70, 0))) { m_page = Page::Games; m_category = seeAll; }
        ImGui::PopID();
    }
    float avail = ImGui::GetContentRegionAvail().x;
    int fit = std::max(1, (int)((avail + 14) / (150 + 14)));
    for (int i = 0; i < (int)games.size() && i < fit; ++i) {
        if (i > 0) ImGui::SameLine(0, 14);
        drawTile(games[i]);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void PlayerApp::drawHome() {
    Profile& me = Profile::get();
    if (!m_status.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "%s", m_status.c_str());
        ImGui::Spacing();
    }
    if (m_games.empty()) {
        ImGui::TextWrapped("No games yet! Click Develop to open Guts and Bolts Studio, build something and "
                           "save it - it will show up here.");
        ImGui::TextDisabled("Games folder: %s", Paths::gamesFolder().string().c_str());
        return;
    }

    std::vector<int> all, recent, carnage, classic;
    for (int i = 0; i < (int)m_games.size(); ++i) {
        all.push_back(i);
        (m_games[i].gore ? carnage : classic).push_back(i);
    }
    for (const auto& name : me.recent)
        for (int i = 0; i < (int)m_games.size(); ++i)
            if (m_games[i].path.filename().string() == name) recent.push_back(i);

    drawRow("Popular", all, "all");
    drawRow("Recently Played", recent, "recent");
    drawRow("Carnage (ragdolls & gore)", carnage, "carnage");
    drawRow("Classic", classic, "classic");

    ImGui::TextDisabled("Tip: click Host on a game's page to play with friends, then they click Join a Friend.");
}

void PlayerApp::drawGames() {
    const char* names[] = {"all", "recent", "carnage", "classic"};
    const char* titles[] = {"All Games", "Recently Played", "Carnage", "Classic"};
    int cur = 0;
    for (int i = 0; i < 4; ++i) if (m_category == names[i]) cur = i;

    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(titles[cur]);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("##cat", &cur, titles, 4)) m_category = names[cur];
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##search", "Search", &m_search);
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refreshGames();
    ImGui::Spacing();

    std::vector<int> list;
    std::string q = lower(m_search);
    auto matches = [&](int i) {
        const GameCard& g = m_games[i];
        if (!q.empty() && lower(g.info.title).find(q) == std::string::npos &&
            lower(g.info.author).find(q) == std::string::npos) return false;
        if (cur == 2) return g.gore;
        if (cur == 3) return !g.gore;
        return true;
    };
    if (cur == 1) {
        for (const auto& name : Profile::get().recent)
            for (int i = 0; i < (int)m_games.size(); ++i)
                if (m_games[i].path.filename().string() == name && matches(i)) list.push_back(i);
    } else {
        for (int i = 0; i < (int)m_games.size(); ++i) if (matches(i)) list.push_back(i);
    }
    if (list.empty()) ImGui::TextDisabled("Nothing here yet.");

    float avail = ImGui::GetContentRegionAvail().x;
    int perRow = std::max(1, (int)((avail + 14) / (150 + 14)));
    for (size_t k = 0; k < list.size(); ++k) {
        if (k % perRow != 0) ImGui::SameLine(0, 14);
        else if (k > 0) ImGui::Spacing();
        drawTile(list[k]);
    }
}

void PlayerApp::drawGameInfo() {
    if (m_selected < 0 || m_selected >= (int)m_games.size()) { m_page = Page::Home; return; }
    GameCard& g = m_games[m_selected];

    if (ImGui::SmallButton("< Back")) m_page = Page::Home;
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(g.info.title.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("by %s", g.info.author.c_str());
    ImGui::Spacing();

    float picW = std::min(560.0f, ImGui::GetContentRegionAvail().x * 0.6f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(picW, picW * 9.0f / 16.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g.thumb)
        dl->AddImage((ImTextureID)(intptr_t)g.thumb->colorTexture(), p,
                     ImVec2(p.x + picW, p.y + picW * 9.0f / 16.0f), ImVec2(0, 1), ImVec2(1, 0));
    dl->AddRect(p, ImVec2(p.x + picW, p.y + picW * 9.0f / 16.0f), IM_COL32(140, 150, 165, 255));

    ImGui::SameLine(0, 20);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(g.broken);
    if (Classic::button("Play", Classic::kPlay, ImVec2(220, 56))) joinGame(g.path);
    ImGui::Spacing();
    if (Classic::button("Host (play with friends)", Classic::kBlue, ImVec2(220, 34))) joinGame(g.path, true);
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextDisabled("Death: %s", g.ragdoll ? "Ragdoll" : "Classic");
    ImGui::TextDisabled("Gore: %s", g.gore ? "Yes" : "No");
    if (g.gore && !GraphicsSettings::get().allowGore)
        ImGui::TextDisabled("(hidden - you turned gore off)");
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::SeparatorText("Description");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(g.info.description.c_str());
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------------------
// Avatar editor
// ---------------------------------------------------------------------------

void PlayerApp::buildAvatarStage() {
    m_avatarScene = std::make_unique<Scene>();
    // Keep only the floor and the character.
    std::vector<SceneNode*> remove;
    for (auto& c : m_avatarScene->root()->children)
        if (c->name != "Baseplate" && !m_avatarScene->isProtected(c.get())) remove.push_back(c.get());
    for (auto* r : remove) m_avatarScene->removeNode(r);
    if (Player* p = m_avatarScene->player()) {
        p->setSpawn({0, 0, 0});
        p->build();
        Profile::get().applyTo(*p);
    }
    Environment& e = m_avatarScene->environment();
    e.fogEnabled = false;
    e.sunAzimuth = 70.0f;
    e.sunElevation = 40.0f;
    m_avatarCam.pivot = {0, 1.4f, 0};
    m_avatarCam.yaw = 90.0f;
    m_avatarCam.pitch = 8.0f;
    m_avatarCam.distance = 6.5f;
    m_avatarCam.fov = 45.0f;
}

void PlayerApp::drawAvatar(float dt) {
    Profile& me = Profile::get();
    Player* p = m_avatarScene->player();
    bool changed = false;

    ImGui::BeginChild("##avatar", ImVec2(0, 0));

    // Left: 3D preview (drag to spin).
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float previewW = std::max(200.0f, avail.x * 0.55f);
    ImGui::BeginChild("##preview", ImVec2(previewW, 0), ImGuiChildFlags_Borders);
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x > 1 && size.y > 1) {
        m_avatarView.resize((int)size.x, (int)size.y);
        m_avatarCam.resize((int)size.x, (int)size.y);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_avatarCam.yaw += dt * 12.0f;   // slow turntable
        m_renderer->render(*m_avatarScene, m_avatarCam, m_avatarView, false);
        ImGui::Image((ImTextureID)(intptr_t)m_avatarView.colorTexture(), size, ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            m_avatarCam.yaw += ImGui::GetIO().MouseDelta.x * 0.5f;
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 24);

    // Right: options.
    ImGui::BeginChild("##opts", ImVec2(0, 0));
    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("Your Avatar");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("This is how you look in every game.");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(260);
    if (ImGui::InputText("Display name", &me.name) && me.name.size() > 20) me.name.resize(20);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (me.name.empty()) me.name = "Player";
        me.save();
    }

    ImGui::SeparatorText("Outfits");
    int i = 0;
    for (const auto& [name, colors] : Player::colorPresets()) {
        if (ImGui::Button(name, ImVec2(130, 30))) { me.colors = colors; changed = true; }
        if (++i % 3 != 0) ImGui::SameLine();
    }
    if (i % 3 != 0) ImGui::NewLine();

    ImGui::SeparatorText("Body colours");
    const ImGuiColorEditFlags cf = ImGuiColorEditFlags_NoInputs;
    changed |= ImGui::ColorEdit3("Head", &me.colors.head.x, cf);   ImGui::SameLine(160);
    changed |= ImGui::ColorEdit3("Torso", &me.colors.torso.x, cf);
    changed |= ImGui::ColorEdit3("Left Arm", &me.colors.leftArm.x, cf); ImGui::SameLine(160);
    changed |= ImGui::ColorEdit3("Right Arm", &me.colors.rightArm.x, cf);
    changed |= ImGui::ColorEdit3("Left Leg", &me.colors.leftLeg.x, cf); ImGui::SameLine(160);
    changed |= ImGui::ColorEdit3("Right Leg", &me.colors.rightLeg.x, cf);

    ImGui::SeparatorText("Hat");
    for (int h = 0; h < 4; ++h) {
        bool on = (int)me.hat == h;
        bool pressed = on ? Classic::button(Player::hatName((HatStyle)h), Classic::kBlue, ImVec2(95, 30))
                          : ImGui::Button(Player::hatName((HatStyle)h), ImVec2(95, 30));
        if (pressed) {
            me.hat = (HatStyle)h;
            changed = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    ImGui::EndChild();
    ImGui::EndChild();

    if (changed && p) {
        me.applyTo(*p);
        me.save();
    }
}

// ---------------------------------------------------------------------------
// In game
// ---------------------------------------------------------------------------

void PlayerApp::drawGame(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !m_chatOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_paused = !m_paused;

    ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1 || size.y < 1) return;

    // Networking first (receive the world), then simulate, then (next tick) send.
    if (m_client) {
        m_client->update(dt);
        if (m_client->state() == NetClient::State::Failed) {
            std::string err = m_client->error();
            leaveGame();
            m_status = err;
            return;
        }
        if (m_client->state() == NetClient::State::Joined) m_currentTitle = m_client->gameTitle();
    }
    if (m_server) m_server->update(dt);
    if (!m_server && !m_client) m_soloChat->update(dt);

    bool connecting = m_client && m_client->state() != NetClient::State::Joined;
    if (connecting) {
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(20, 22, 28, 255));
        const char* t = "Joining game...";
        ImVec2 ts = ImGui::CalcTextSize(t);
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + size.y * 0.45f),
                                            IM_COL32(255, 255, 255, 255), t);
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) leaveGame();
        return;
    }

    // Chat: "/" or Enter starts typing.
    if (!m_chatOpen && !io.WantTextInput && !m_paused &&
        (ImGui::IsKeyPressed(ImGuiKey_Slash, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
        m_chatOpen = true;

    // Simulate (the world keeps running while the menu is open, like Roblox).
    bool acceptInput = !m_paused && !m_showSettings && !m_chatOpen;
    m_session->update(dt, m_camera.yaw, acceptInput);

    // Camera: follow the character; right-drag to look around, wheel to zoom.
    bool hovered = ImGui::IsWindowHovered();
    if (acceptInput && hovered) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            m_camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
        if (io.MouseWheel != 0.0f) m_camera.zoom(io.MouseWheel);
        m_camera.distance = std::clamp(m_camera.distance, 2.0f, 60.0f);
    }
    if (Player* p = m_scene->player()) {
        glm::vec3 target = p->focusPoint();
        m_camera.pivot += (target - m_camera.pivot) * std::min(1.0f, dt * 12.0f);
    }

    m_view.resize((int)size.x, (int)size.y);
    m_camera.resize((int)size.x, (int)size.y);
    m_renderer->render(*m_scene, m_camera, m_view, false);
    Audio::setListener(m_camera.position(), glm::normalize(m_camera.pivot - m_camera.position()));
    ImGui::Image((ImTextureID)(intptr_t)m_view.colorTexture(), size, ImVec2(0, 1), ImVec2(1, 0));

    // Clicking parts (for part.Clicked in scripts).
    if (acceptInput && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 m = ImGui::GetMousePos();
        float nx = (m.x - pos.x) / size.x * 2.0f - 1.0f;
        float ny = 1.0f - (m.y - pos.y) / size.y * 2.0f;
        glm::mat4 inv = glm::inverse(m_camera.projection() * m_camera.view());
        glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
        glm::vec3 ro = glm::vec3(a) / a.w, rd = glm::normalize(glm::vec3(b) / b.w - ro);
        const SceneNode* character = m_scene->player() ? m_scene->player()->root() : nullptr;
        SceneNode* hit = Physics::raycast(*m_scene, ro, rd, nullptr, character);
        m_session->click(hit ? hit->id : 0);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    Hud::draw(dl, pos, max, *m_scene, m_session->gui());
    Hud::drawBubbles(dl, pos, max, *m_scene, m_camera.projection() * m_camera.view(), chat().bubbles);
    if (m_server) Hud::drawPlayerList(dl, pos, max, m_server->playerNames());
    if (m_client) Hud::drawPlayerList(dl, pos, max, m_client->playerNames());
    drawChat(pos, max);

    // Top-left menu button + FPS.
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 12, max.y - 44));
    if (bigButton("Menu (Esc)", ImVec4(0.1f, 0.1f, 0.12f, 0.8f))) m_paused = true;
    if (GraphicsSettings::get().showFps) {
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%.0f FPS", io.Framerate);
        dl->AddText(ImVec2(max.x - 80, max.y - 26), IM_COL32(255, 255, 255, 160), fps);
    }

    if (m_paused) drawPauseMenu();
}

void PlayerApp::drawChat(ImVec2 min, ImVec2 max) {
    ChatLog& log = chat();
    const float w = 420.0f;
    ImVec2 p(min.x + 12, max.y - 60 - 210);
    ImGui::SetNextWindowPos(p);
    ImGui::SetNextWindowSize(ImVec2(w, 210));
    ImGui::SetNextWindowBgAlpha(m_chatOpen ? 0.45f : 0.2f);
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##chat", nullptr, f);
    ImGui::BeginChild("##lines", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_None);
    size_t first = log.lines.size() > 40 ? log.lines.size() - 40 : 0;
    for (size_t i = first; i < log.lines.size(); ++i) {
        const auto& l = log.lines[i];
        if (l.system) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1), "%s", l.text.c_str());
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1), "%s:", l.from.c_str());
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(l.text.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    if (m_chatOpen) {
        ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##say", "Type a message and press Enter (Esc to cancel)", &m_chatInput,
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
            sendChat(m_chatInput);
            m_chatInput.clear();
            m_chatOpen = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_chatOpen = false; m_chatInput.clear(); }
    } else {
        ImGui::TextDisabled("Press / to chat");
    }
    ImGui::End();
}

void PlayerApp::drawPauseMenu() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::GetForegroundDrawList()->AddRectFilled(vp->WorkPos,
        ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y), IM_COL32(0, 0, 0, 120));
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(320, 0));
    ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted(m_currentTitle.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Spacing();
    const ImVec2 full(-1, 40);
    if (bigButton("Resume", kGreen, full)) m_paused = false;
    if (bigButton("Reset Character", ImVec4(0.3f, 0.3f, 0.35f, 1), full)) {
        if (Player* p = m_scene->player()) p->kill();
        m_paused = false;
    }
    if (bigButton("Settings", ImVec4(0.3f, 0.3f, 0.35f, 1), full)) m_showSettings = true;
    if (bigButton("Leave Game", ImVec4(0.75f, 0.25f, 0.25f, 1), full)) leaveGame();
    ImGui::End();
}
