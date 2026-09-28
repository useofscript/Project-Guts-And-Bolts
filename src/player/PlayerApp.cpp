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
#include "../core/Account.h"
#include "../game/Badges.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

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
    if (!m_opts.touchTest.empty() && m_opts.touchTest.rfind("sdl-", 0) != 0)
        GraphicsSettings::get().touchControls = GraphicsSettings::TouchOn;
    if (m_opts.createStaff) {
        m_notice = Account::createStaffAccount();
        std::string err;
        if (Account::iAmStaff()) Profile::get().rename(Account::kStaffName, err);
    }
    if (m_opts.testItems) {
        std::string msg;
        Catalog::Item a; a.name = "Red Cap"; a.description = "A classic."; a.type = Catalog::Type::Hat;
        a.hat = HatStyle::Cap; a.color = {0.85f, 0.1f, 0.1f};
        Catalog::create(a, msg); Log::info(msg);
        Catalog::Item b; b.name = "Bolt Tee"; b.type = Catalog::Type::Shirt; b.color = {0.2f, 0.5f, 0.9f};
        Catalog::create(b, msg); Log::info(msg);
        Catalog::Item c; c.name = "Oil Jeans"; c.type = Catalog::Type::Pants; c.color = {0.15f, 0.15f, 0.2f};
        Catalog::create(c, msg); Log::info(msg);
    }
    if (!m_opts.testGrantFor.empty()) {
        std::string err;
        std::string code = Badges::makeCode(Badges::Id::Tester, m_opts.testGrantFor, err);
        std::printf("BADGECODE %s %s\n", code.c_str(), err.c_str());
        std::fflush(stdout);
    }
    if (!m_opts.testRedeem.empty()) {
        std::string msg;
        Badges::redeem(m_opts.testRedeem, msg);
        std::printf("REDEEM %s\n", msg.c_str());
        std::fflush(stdout);
    }
    buildAvatarStage();
    refreshGames();
    m_items = Catalog::load();
    std::printf("CATALOG %d items, account %s, staff %d\n", (int)m_items.size(), Account::shortId().c_str(),
                (int)Account::iAmStaff());
    std::fflush(stdout);

    if (m_opts.page == "avatar") m_page = Page::Avatar;
    if (m_opts.page == "games") m_page = Page::Games;
    if (m_opts.page.rfind("game:", 0) == 0) { m_selected = std::atoi(m_opts.page.c_str() + 5); m_page = Page::GameInfo; }
    if (m_opts.page == "settings") m_showSettings = true;
    if (m_opts.page == "catalog") m_page = Page::Catalog;
    if (m_opts.page == "staff" && Account::iAmStaff()) m_page = Page::Staff;
    if (m_opts.page == "create-item" && Account::iAmStaff()) { m_page = Page::Catalog; m_showCreate = true; }
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
        // Test helper: real (SDL) touch events, as a phone would send them.
        if (m_opts.touchTest.rfind("sdl-", 0) == 0 && m_frame > 40) {
            float k = std::min(1.0f, (m_frame - 40) / 6.0f);
            if (m_opts.touchTest == "sdl-stick") m_window->injectTouch(7, 0.12f, 0.80f - 0.08f * k, true);
            if (m_opts.touchTest == "sdl-jump")  m_window->injectTouch(8, 0.93f, 0.86f, ((m_frame - 40) / 15) % 2 == 0);
            // "sdl-tap:x,y" taps once; "sdl-swipe:x,y1,y2" drags a finger (0..1 screen coords).
            float a = 0, b = 0, c = 0;
            int t = m_frame - 40;
            if (std::sscanf(m_opts.touchTest.c_str(), "sdl-tap:%f,%f", &a, &b) == 2 && t <= 4)
                m_window->injectTouch(10, a, b, t < 3);
            if (std::sscanf(m_opts.touchTest.c_str(), "sdl-swipe:%f,%f,%f", &a, &b, &c) == 3 && t <= 21)
                m_window->injectTouch(11, a, b + (c - b) * std::min(1.0f, t / 20.0f), t < 21);
            if (m_opts.touchTest == "sdl-both") {
                m_window->injectTouch(7, 0.12f, 0.80f - 0.08f * k, true);
                m_window->injectTouch(9, 0.55f + 0.004f * (m_frame - 40), 0.4f, true);
            }
        }
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
    else               m_soloChat->add(Profile::get().name, text, false, Account::iAmStaff());
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
        // Esc / Android Back: go back to the home page.
        if (m_page != Page::Home && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_page = Page::Home;
        switch (m_page) {
            case Page::Home:     drawHome(); break;
            case Page::Games:    drawGames(); break;
            case Page::Avatar:   drawAvatar(dt); break;
            case Page::GameInfo: drawGameInfo(); break;
            case Page::Catalog:  drawCatalog(); break;
            case Page::Staff:    drawStaff(); break;
            default: break;
        }
        Classic::popLight();
        ImGui::EndChild();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }

    ImGui::End();
    if (m_page != Page::Game) touchScroll();
    SettingsWindow::draw(&m_showSettings);
    drawJoinDialog();
    drawItemDialog();
    drawCreateItemDialog();
    drawNotice();
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
#ifdef GB_MOBILE
    const float bannerH = 72.0f;     // phones are short: keep the banner slim
    const float logoSize = 40.0f;
#else
    const float bannerH = 118.0f;
    const float logoSize = 64.0f;
#endif
    const float fb = ImGui::GetIO().DisplayFramebufferScale.x;   // > 1 on phones: draw with every real pixel
    m_bannerView.resize((int)(width * fb), (int)(bannerH * fb));
    Camera cam;
    cam.resize((int)(width * fb), (int)(bannerH * fb));
    cam.fov = 30.0f;
    cam.pivot = {-5.2f, 1.45f, 0.0f};      // look left of the avatar so it stands on the right
    cam.yaw = 90.0f;
    cam.pitch = 3.0f;
    cam.distance = 10.5f;
    m_renderer->render(*m_avatarScene, cam, m_bannerView, false);
    ImVec2 b0 = pos, b1(pos.x + width, pos.y + bannerH);
    dl->AddImageRounded((ImTextureID)(intptr_t)m_bannerView.colorTexture(), b0, b1, ImVec2(0, 1), ImVec2(1, 0),
                        IM_COL32_WHITE, 8.0f, ImDrawFlags_RoundCornersTop);
    Classic::logo(dl, ImVec2(pos.x + 26, pos.y + (bannerH - logoSize) * 0.5f - 4), logoSize, "GUTS&BOLTS");

    // Account box, top-right of the banner.
    std::string hi = "Hi, " + me.name;
    ImVec2 ts = ImGui::CalcTextSize(hi.c_str());
    const bool staff = Account::iAmStaff();
    float badgeW = staff ? 24.0f : 0.0f;
    ImVec2 a(b1.x - ts.x - badgeW - 34, pos.y + 10), c(b1.x - 10, pos.y + 10 + 50);
    dl->AddRectFilled(a, c, IM_COL32(255, 255, 255, 215), 5.0f);
    dl->AddRect(a, c, IM_COL32(120, 140, 170, 255), 5.0f);
    if (staff) Badges::drawIcon(dl, ImVec2(a.x + 22, a.y + 15), 20.0f, Badges::Id::Administrator);
    dl->AddText(ImVec2(a.x + 12 + badgeW, a.y + 7), IM_COL32(30, 30, 40, 255), hi.c_str());
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
    std::vector<Item> items = {{"Home", 0}, {"Games", 1}, {"Catalog", 6}, {"Avatar", 2}, {"Join a Friend", 3},
                               {"Develop", 4}, {"Settings", 5}};
    if (staff) items.push_back({"Staff", 7});
#ifdef GB_MOBILE
    // No Studio on phones.
    items.erase(std::remove_if(items.begin(), items.end(), [](const Item& i) { return i.action == 4; }), items.end());
#endif
    float x = n0.x + 14;
    for (const Item& it : items) {
        ImVec2 sz = ImGui::CalcTextSize(it.label);
        ImVec2 p0(x - 8, n0.y), p1(x + sz.x + 8, n1.y);
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(it.label);
        bool clicked = ImGui::InvisibleButton("##nav", ImVec2(p1.x - p0.x, navH));
        ImGui::PopID();
        bool active = (it.action == 0 && m_page == Page::Home) || (it.action == 1 && m_page == Page::Games) ||
                      (it.action == 2 && m_page == Page::Avatar) || (it.action == 6 && m_page == Page::Catalog) ||
                      (it.action == 7 && m_page == Page::Staff);
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
                case 6: m_page = Page::Catalog; m_items = Catalog::load(); break;
                case 7: m_page = Page::Staff; break;
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
    if (hover && !g.info.description.empty() && !m_window->hasTouchScreen()) {   // no hovering on phones
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));   // light text on the dark tooltip
        ImGui::SetTooltip("%s", g.info.description.c_str());
        ImGui::PopStyleColor();
    }
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
#ifdef GB_MOBILE
    float previewW = std::max(160.0f, avail.x * 0.38f);
#else
    float previewW = std::max(200.0f, avail.x * 0.55f);
#endif
    ImGui::BeginChild("##preview", ImVec2(previewW, 0), ImGuiChildFlags_Borders);
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x > 1 && size.y > 1) {
        const float fb = ImGui::GetIO().DisplayFramebufferScale.x;
        m_avatarView.resize((int)(size.x * fb), (int)(size.y * fb));
        m_avatarCam.resize((int)(size.x * fb), (int)(size.y * fb));
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
    if (!ImGui::IsAnyItemActive() && m_nameEdit != me.name && m_nameError.empty()) m_nameEdit = me.name;
    if (ImGui::InputText("Display name", &m_nameEdit) && m_nameEdit.size() > 20) m_nameEdit.resize(20);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (me.rename(m_nameEdit, m_nameError)) m_nameError.clear();
        else m_nameEdit = me.name;
    }
    if (!m_nameError.empty()) ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "%s", m_nameError.c_str());

    ImGui::SeparatorText("Outfits");
    int i = 0;
    for (const auto& [name, colors] : Player::colorPresets()) {
        // Three per row, shrinking to fit narrow (phone) screens.
        float rowW = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
        float bw = std::min(130.0f, (rowW - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f);
        if (ImGui::Button(name, ImVec2(bw, 30))) { me.colors = colors; changed = true; }
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
        float rowW = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
        float hw = std::min(95.0f, (rowW - ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f);
        bool pressed = on ? Classic::button(Player::hatName((HatStyle)h), Classic::kBlue, ImVec2(hw, 30))
                          : ImGui::Button(Player::hatName((HatStyle)h), ImVec2(hw, 30));
        if (pressed) {
            me.hat = (HatStyle)h;
            me.hatColor = glm::vec3(-1.0f);   // back to its normal colours
            changed = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    drawAccount();

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
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    const bool touch = GraphicsSettings::get().touchEnabled();
    if (touch) updateTouch(pos, max, acceptInput);
    else       m_session->setTouchInput(glm::vec2(0.0f), false);
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

#ifdef GB_MOBILE
    // Phones: render with the real pixels, not the (bigger) UI pixels.
    ImVec2 px(size.x * io.DisplayFramebufferScale.x, size.y * io.DisplayFramebufferScale.y);
#else
    ImVec2 px = size;
#endif
    m_view.resize((int)px.x, (int)px.y);
    m_camera.resize((int)px.x, (int)px.y);
    m_renderer->render(*m_scene, m_camera, m_view, false);
    Audio::setListener(m_camera.position(), glm::normalize(m_camera.pivot - m_camera.position()));
    ImGui::Image((ImTextureID)(intptr_t)m_view.colorTexture(), size, ImVec2(0, 1), ImVec2(1, 0));

    // Clicking parts (for part.Clicked in scripts). With touch controls, a tap does it.
    ImVec2 tapAt;
    bool tapped = touch && m_touch.tapped(tapAt);
    if (acceptInput && (tapped || (!touch && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))) {
        ImVec2 m = tapped ? tapAt : ImGui::GetMousePos();
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
    float labelsAt = 0.0f;
    if (touch) {
        bool chatShowing = m_chatOpen || ImGui::GetTime() < m_chatShowUntil;
        labelsAt = chatShowing ? (m_chatOpen ? 216.0f : 156.0f) : 58.0f;   // under the chat box when it's up
    }
    Hud::draw(dl, pos, max, *m_scene, m_session->gui(), labelsAt);
    Hud::drawBubbles(dl, pos, max, *m_scene, m_camera.projection() * m_camera.view(), chat().bubbles);
    if (m_server)      Hud::drawPlayerList(dl, pos, max, m_server->players());
    else if (m_client) Hud::drawPlayerList(dl, pos, max, m_client->players());
    else               Hud::drawPlayerList(dl, pos, max, {{Profile::get().name, Account::iAmStaff()}});
    drawChat(pos, max);

    // Menu button + FPS. (Touch screens get their own buttons instead.)
    if (touch) {
        if (acceptInput) m_touch.draw(dl);
    } else {
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 12, max.y - 44));
        if (bigButton("Menu (Esc)", ImVec4(0.1f, 0.1f, 0.12f, 0.8f))) m_paused = true;
    }
    if (GraphicsSettings::get().showFps) {
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%.0f FPS", io.Framerate);
        dl->AddText(ImVec2(max.x - 80, max.y - 26), IM_COL32(255, 255, 255, 160), fps);
    }

    if (m_paused) drawPauseMenu();
}

void PlayerApp::drawChat(ImVec2 min, ImVec2 max) {
    ChatLog& log = chat();
    const bool touchUi = GraphicsSettings::get().touchEnabled();
    if (touchUi) {
        // Phones: chat stays out of the way until you tap the chat button, and
        // pops up for a few seconds when someone says something.
        double now = ImGui::GetTime();
        if (log.lines.size() != m_chatSeen) { m_chatSeen = log.lines.size(); m_chatShowUntil = now + 6.0; }
        if (!m_chatOpen && now > m_chatShowUntil) { m_chatMin = m_chatMax = ImVec2(0, 0); return; }
    }
    // Bottom-left normally; top-left on touch screens (the thumbstick lives bottom-left).
    const bool touch = GraphicsSettings::get().touchEnabled();
    const float w = touch ? 340.0f : 420.0f, h = touch ? (m_chatOpen ? 150.0f : 90.0f) : 210.0f;
    ImVec2 p = touch ? ImVec2(min.x + 12, min.y + 70) : ImVec2(min.x + 12, max.y - 60 - h);
    ImGui::SetNextWindowPos(p);
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::SetNextWindowBgAlpha(m_chatOpen ? 0.45f : 0.2f);
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##chat", nullptr, f);
    m_chatMin = ImGui::GetWindowPos();
    m_chatMax = ImVec2(m_chatMin.x + ImGui::GetWindowWidth(), m_chatMin.y + ImGui::GetWindowHeight());
    float footer = (touch && !m_chatOpen) ? 0.0f : ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##lines", ImVec2(0, -footer), ImGuiChildFlags_None);
    size_t first = log.lines.size() > 40 ? log.lines.size() - 40 : 0;
    for (size_t i = first; i < log.lines.size(); ++i) {
        const auto& l = log.lines[i];
        if (l.system) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1), "%s", l.text.c_str());
            ImGui::PopTextWrapPos();
        } else {
            if (l.admin) {
                Badges::icon(Badges::Id::Administrator, ImGui::GetTextLineHeight());
                ImGui::SameLine(0, 4);
            }
            ImGui::TextColored(l.admin ? ImVec4(1.0f, 0.85f, 0.4f, 1) : ImVec4(0.55f, 0.8f, 1.0f, 1), "%s:", l.from.c_str());
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(l.text.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    if (m_chatOpen) {
        if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(touch ? -100 : -60);
        bool enter = ImGui::InputTextWithHint("##say", touch ? "Type a message" : "Type a message and press Enter (Esc to cancel)",
                                              &m_chatInput, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (enter || ImGui::Button("Send", ImVec2(touch ? 56 : -1, 0))) {
            sendChat(m_chatInput);
            m_chatInput.clear();
            m_chatOpen = false;
        }
        if (touch) {   // phones have no Esc key
            ImGui::SameLine();
            if (ImGui::Button("X", ImVec2(-1, 0))) { m_chatOpen = false; m_chatInput.clear(); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_chatOpen = false; m_chatInput.clear(); }
    } else {
        if (!touch) ImGui::TextDisabled("Press / to chat");
    }
    ImGui::End();
}

void PlayerApp::drawPauseMenu() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    // Darken the game behind the menu (not the menu itself).
    ImGui::GetWindowDrawList()->AddRectFilled(vp->WorkPos,
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

// ---------------------------------------------------------------------------
// Touch controls
// ---------------------------------------------------------------------------

void PlayerApp::updateTouch(ImVec2 min, ImVec2 max, bool acceptInput) {
    const float scale = GraphicsSettings::get().touchSize;
    m_touch.begin(min, max, scale);
    m_touch.setBlocked({{m_chatMin, m_chatMax}});
    if (m_window->hasTouchScreen()) {
        // A real touch screen: every finger counts (thumbstick + look + jump at once).
        std::vector<TouchControls::Finger> f;
        for (const TouchPoint& t : m_window->touches()) f.push_back({(int)t.id, ImVec2(t.x, t.y), true});
        m_touch.feed(f, acceptInput);
    } else if (!m_opts.touchTest.empty()) {
        // Test helper: pretend fingers are on the screen.
        std::vector<TouchControls::Finger> f;
        int k = m_frame - 40;
        if (k > 0 && m_opts.touchTest == "stick") {
            ImVec2 base(min.x + 150, max.y - 150);
            float push = std::min(1.0f, k / 6.0f);
            f.push_back({1, ImVec2(base.x + 12 * push, base.y - 70 * push), true});
        }
        if (k > 0 && m_opts.touchTest == "jump" && (k / 15) % 2 == 0)
            f.push_back({2, ImVec2(max.x - 105 * scale, max.y - 105 * scale), true});
        if (k > 0 && m_opts.touchTest == "look")
            f.push_back({3, ImVec2((min.x + max.x) * 0.5f + k * 4.0f, (min.y + max.y) * 0.4f), true});
        m_touch.feed(f);
    } else {
        m_touch.feedMouse(acceptInput && ImGui::IsWindowHovered());
    }
    if (!acceptInput) { m_session->setTouchInput(glm::vec2(0.0f), false); return; }
    m_session->setTouchInput(m_touch.move(), m_touch.jump());
    ImVec2 look = m_touch.look();
    if (look.x != 0.0f || look.y != 0.0f) m_camera.orbit(look.x, look.y);
    if (m_touch.zoom() != 0.0f) m_camera.zoom(m_touch.zoom());
    if (m_touch.chatPressed()) m_chatOpen = true;
    if (m_touch.menuPressed()) m_paused = true;
}

// ---------------------------------------------------------------------------
// Account, badges and staff tools
// ---------------------------------------------------------------------------

void PlayerApp::drawAccount() {
    Profile& me = Profile::get();
    ImGui::SeparatorText("Badges");
    auto badges = Badges::verified(Account::id(), me.grants);
    if (badges.empty()) {
        ImGui::TextDisabled("No official badges yet.");
    } else {
        for (size_t i = 0; i < badges.size(); ++i) {
            if (i > 0) ImGui::SameLine(0, 16);
            ImGui::BeginGroup();
            Badges::icon(badges[i], 56.0f);
            ImGui::TextUnformatted(Badges::info(badges[i]).name);
            ImGui::EndGroup();
        }
    }

    ImGui::SeparatorText("Your account");
    ImGui::Text("Account ID: %s...", Account::shortId().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy full ID")) ImGui::SetClipboardText(Account::id().c_str());
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Staff need this ID to give you a badge. It's safe to share - the secret key that "
                        "proves it's you never leaves this computer.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##redeem", "Paste a badge code", &m_redeemCode);
    ImGui::SameLine();
    if (Classic::button("Redeem", Classic::kBlue)) {
        Badges::redeem(m_redeemCode, m_redeemMsg);
        m_redeemCode.clear();
    }
    if (!m_redeemMsg.empty()) ImGui::TextWrapped("%s", m_redeemMsg.c_str());
}

void PlayerApp::drawStaff() {
    if (!Account::iAmStaff()) { m_page = Page::Home; return; }
    Badges::icon(Badges::Id::Administrator, 40.0f);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Staff Tools");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Only the official Guts account can see this page.");
    ImGui::EndGroup();

    ImGui::SeparatorText("Give someone a badge");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Ask them for their account ID (Avatar page > Copy full ID), make a code here and send "
                        "it to them. The code only works for their account.");
    ImGui::PopTextWrapPos();
    std::vector<const char*> names;
    std::vector<int> ids;
    for (int i = 0; i < (int)Badges::Id::Count; ++i)
        if (Badges::info((Badges::Id)i).grantable) { names.push_back(Badges::info((Badges::Id)i).name); ids.push_back(i); }
    int cur = 0;
    for (int k = 0; k < (int)ids.size(); ++k) if (ids[k] == m_grantBadge) cur = k;
    Badges::icon((Badges::Id)ids[cur], 32.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("Badge", &cur, names.data(), (int)names.size())) m_grantBadge = ids[cur];
    m_grantBadge = ids[cur];
    ImGui::SetNextItemWidth(520);
    ImGui::InputTextWithHint("Their account ID", "64 letters and numbers", &m_grantTo);
    if (Classic::button("Make badge code", Classic::kPlay, ImVec2(180, 30))) {
        m_grantError.clear();
        m_grantCode = Badges::makeCode((Badges::Id)m_grantBadge, m_grantTo, m_grantError);
    }
    if (!m_grantError.empty()) ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "%s", m_grantError.c_str());
    if (!m_grantCode.empty()) {
        ImGui::InputTextMultiline("##code", &m_grantCode, ImVec2(520, 60), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Copy code")) ImGui::SetClipboardText(m_grantCode.c_str());
    }

    ImGui::SeparatorText("Catalog");
    ImGui::Text("%d item(s) in the catalog.", (int)m_items.size());
    if (Classic::button("Create a catalog item", Classic::kBlue, ImVec2(220, 30))) {
        m_page = Page::Catalog;
        m_showCreate = true;
    }

    ImGui::SeparatorText("Your staff key");
    ImGui::PushTextWrapPos(0);
    ImGui::TextWrapped("Your secret key is %s. Back it up somewhere safe and never share it - whoever has it "
                       "is the Guts account.", (Account::folder() / "account.key").string().c_str());
    ImGui::PopTextWrapPos();
    ImGui::Text("Official ID: %s...", Account::shortId().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy official ID")) ImGui::SetClipboardText(Account::id().c_str());
}

void PlayerApp::drawNotice() {
    static bool opened = false;
    if (m_notice.empty()) { opened = false; return; }
    if (!opened) { ImGui::OpenPopup("Guts&Bolts##notice"); opened = true; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 0));
    if (ImGui::BeginPopupModal("Guts&Bolts##notice", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("%s", m_notice.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Copy my account ID", ImVec2(200, 32))) ImGui::SetClipboardText(Account::id().c_str());
        ImGui::SameLine();
        if (bigButton("OK", kGreen, ImVec2(100, 32))) { m_notice.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Catalog
// ---------------------------------------------------------------------------

namespace {
// A simple picture of a catalog item, drawn in its colour.
void drawItemIcon(ImDrawList* dl, ImVec2 c, float s, const Catalog::Item& it) {
    ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(it.color.r, it.color.g, it.color.b, 1));
    ImU32 line = IM_COL32(40, 40, 50, 255);
    float t = std::max(1.5f, s * 0.02f);
    switch (it.type) {
    case Catalog::Type::Hat:
        if (it.hat == HatStyle::TopHat) {
            ImVec2 a(c.x - s * 0.2f, c.y - s * 0.3f), b(c.x + s * 0.2f, c.y + s * 0.15f);
            dl->AddRectFilled(a, b, fill); dl->AddRect(a, b, line, 0, 0, t);
            ImVec2 ba(c.x - s * 0.38f, c.y + s * 0.15f), bb(c.x + s * 0.38f, c.y + s * 0.24f);
            dl->AddRectFilled(ba, bb, fill, 4); dl->AddRect(ba, bb, line, 4, 0, t);
        } else if (it.hat == HatStyle::Crown) {
            ImVec2 pts[] = {{c.x - s * 0.34f, c.y + s * 0.2f}, {c.x - s * 0.34f, c.y - s * 0.22f}, {c.x - s * 0.17f, c.y - s * 0.02f},
                            {c.x, c.y - s * 0.3f}, {c.x + s * 0.17f, c.y - s * 0.02f}, {c.x + s * 0.34f, c.y - s * 0.22f},
                            {c.x + s * 0.34f, c.y + s * 0.2f}};
            for (int i = 1; i < 6; ++i) dl->AddTriangleFilled(pts[0], pts[i], pts[i + 1], fill);
            dl->AddPolyline(pts, 7, line, ImDrawFlags_Closed, t);
        } else {   // cap
            dl->PathArcTo(ImVec2(c.x, c.y + s * 0.1f), s * 0.3f, 3.14159f, 6.28318f, 24);
            dl->PathFillConvex(fill);
            dl->PathArcTo(ImVec2(c.x, c.y + s * 0.1f), s * 0.3f, 3.14159f, 6.28318f, 24);
            dl->PathStroke(line, 0, t);
            ImVec2 va(c.x, c.y + s * 0.06f), vb(c.x + s * 0.46f, c.y + s * 0.14f);
            dl->AddRectFilled(va, vb, fill, 3); dl->AddRect(va, vb, line, 3, 0, t);
        }
        break;
    case Catalog::Type::Shirt: {
        ImVec2 pts[] = {{c.x - s * 0.18f, c.y - s * 0.32f}, {c.x + s * 0.18f, c.y - s * 0.32f}, {c.x + s * 0.4f, c.y - s * 0.12f},
                        {c.x + s * 0.3f, c.y + s * 0.0f}, {c.x + s * 0.22f, c.y - s * 0.06f}, {c.x + s * 0.22f, c.y + s * 0.34f},
                        {c.x - s * 0.22f, c.y + s * 0.34f}, {c.x - s * 0.22f, c.y - s * 0.06f}, {c.x - s * 0.3f, c.y + s * 0.0f},
                        {c.x - s * 0.4f, c.y - s * 0.12f}};
        dl->AddRectFilled(ImVec2(c.x - s * 0.22f, c.y - s * 0.32f), ImVec2(c.x + s * 0.22f, c.y + s * 0.34f), fill);
        dl->AddTriangleFilled(pts[1], pts[2], pts[3], fill); dl->AddTriangleFilled(pts[1], pts[3], pts[4], fill);
        dl->AddTriangleFilled(pts[0], pts[9], pts[8], fill); dl->AddTriangleFilled(pts[0], pts[8], pts[7], fill);
        dl->AddPolyline(pts, 10, line, ImDrawFlags_Closed, t);
        break;
    }
    case Catalog::Type::Pants: {
        // Waistband plus two legs.
        ImVec2 w0(c.x - s * 0.25f, c.y - s * 0.34f), w1(c.x + s * 0.25f, c.y - s * 0.22f);
        ImVec2 l0(c.x - s * 0.25f, c.y - s * 0.22f), l1(c.x - s * 0.02f, c.y + s * 0.36f);
        ImVec2 r0(c.x + s * 0.02f, c.y - s * 0.22f), r1(c.x + s * 0.25f, c.y + s * 0.36f);
        ImVec2 mid0(c.x - s * 0.03f, c.y - s * 0.22f), mid1(c.x + s * 0.03f, c.y - s * 0.05f);
        for (auto [a, b] : {std::pair{w0, w1}, std::pair{l0, l1}, std::pair{r0, r1}, std::pair{mid0, mid1}})
            dl->AddRectFilled(a, b, fill);
        dl->AddRect(w0, w1, line, 0, 0, t);
        dl->AddRect(l0, l1, line, 0, 0, t);
        dl->AddRect(r0, r1, line, 0, 0, t);
        break;
    }
    default: break;
    }
}
} // namespace

void PlayerApp::drawCatalog() {
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Catalog");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Hats, shirts and pants for your avatar.");
    ImGui::Spacing();

    const char* tabs[] = {"All", "Hats", "Shirts", "Pants"};
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        bool on = m_itemType == i - 1;
        if (on ? Classic::button(tabs[i], Classic::kBlue, ImVec2(90, 28)) : ImGui::Button(tabs[i], ImVec2(90, 28)))
            m_itemType = i - 1;
    }
    if (Account::iAmStaff()) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 140);
        if (Classic::button("Create Item", Classic::kPlay, ImVec2(140, 28))) m_showCreate = true;
    }
    if (!m_catalogMsg.empty()) ImGui::TextWrapped("%s", m_catalogMsg.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<int> list;
    for (int i = 0; i < (int)m_items.size(); ++i)
        if (m_itemType < 0 || (int)m_items[i].type == m_itemType) list.push_back(i);

    if (list.empty()) {
        ImGui::Dummy(ImVec2(0, 40));
        const char* a = m_items.empty() ? "The catalog is empty right now." : "Nothing in this section yet.";
        const char* b = "New items are on the way - check back soon!";
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::SetWindowFontScale(1.3f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - ImGui::CalcTextSize(a).x) * 0.5f);
        ImGui::TextUnformatted(a);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - ImGui::CalcTextSize(b).x) * 0.5f);
        ImGui::TextDisabled("%s", b);
        return;
    }

    const float tile = 150.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    int perRow = std::max(1, (int)((avail + 14) / (tile + 14)));
    for (size_t k = 0; k < list.size(); ++k) {
        const Catalog::Item& it = m_items[list[k]];
        if (k % perRow != 0) ImGui::SameLine(0, 14);
        ImGui::PushID(list[k]);
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##item", ImVec2(tile, tile))) m_openItem = list[k];
        bool hover = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + tile, p.y + tile), IM_COL32(255, 255, 255, 255));
        dl->AddRect(p, ImVec2(p.x + tile, p.y + tile), hover ? IM_COL32(40, 120, 230, 255) : IM_COL32(160, 165, 175, 255),
                    0, 0, hover ? 2.0f : 1.0f);
        drawItemIcon(dl, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile * 0.8f, it);
        if (Catalog::isWearing(it))
            dl->AddText(ImVec2(p.x + 6, p.y + 4), IM_COL32(20, 140, 60, 255), "Wearing");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile);
        ImGui::TextColored(Classic::kLink, "%s", it.name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "Free");
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void PlayerApp::drawItemDialog() {
    if (m_openItem >= (int)m_items.size()) m_openItem = -1;
    if (m_openItem >= 0 && !ImGui::IsPopupOpen("Catalog Item")) ImGui::OpenPopup("Catalog Item");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (!ImGui::BeginPopupModal("Catalog Item", nullptr, ImGuiWindowFlags_NoResize)) return;
    if (m_openItem < 0) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    Catalog::Item it = m_items[m_openItem];

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(180, 180));
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 180, p.y + 180), IM_COL32(245, 246, 250, 255), 6);
    drawItemIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + 90, p.y + 90), 150, it);
    ImGui::SameLine(0, 18);
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted(it.name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("%s  -  by Guts", Catalog::typeName(it.type));
    ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "Free");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(it.description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Spacing();

    bool wearing = Catalog::isWearing(it);
    ImGui::BeginDisabled(wearing);
    if (bigButton(wearing ? "Wearing" : "Wear", kGreen, ImVec2(140, 34))) {
        Catalog::wear(it);
        if (Player* pl = m_avatarScene->player()) Profile::get().applyTo(*pl);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 34))) { m_openItem = -1; ImGui::CloseCurrentPopup(); }
    if (Account::iAmStaff()) {
        ImGui::SameLine();
        if (bigButton("Remove from catalog", ImVec4(0.75f, 0.25f, 0.25f, 1), ImVec2(0, 34))) {
            Catalog::remove(it, m_catalogMsg);
            m_items = Catalog::load();
            m_openItem = -1;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

void PlayerApp::drawCreateItemDialog() {
    if (m_showCreate) {
        if (Account::iAmStaff()) ImGui::OpenPopup("Create Catalog Item");
        m_showCreate = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 0));
    if (!ImGui::BeginPopupModal("Create Catalog Item", nullptr, ImGuiWindowFlags_NoResize)) return;
    if (!Account::iAmStaff()) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }

    Catalog::Item& it = m_newItem;
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(300);
    ImGui::InputText("Name", &it.name);
    ImGui::InputTextMultiline("Description", &it.description, ImVec2(300, 70));
    int type = (int)it.type;
    const char* types[] = {"Hat", "Shirt", "Pants"};
    ImGui::SetNextItemWidth(300);
    if (ImGui::Combo("Type", &type, types, 3)) it.type = (Catalog::Type)type;
    if (it.type == Catalog::Type::Hat) {
        int style = std::max(0, (int)it.hat - 1);
        const char* styles[] = {"Top Hat", "Cap", "Crown"};
        ImGui::SetNextItemWidth(300);
        if (ImGui::Combo("Style", &style, styles, 3)) it.hat = (HatStyle)(style + 1);
        if (it.hat == HatStyle::None) it.hat = HatStyle::Cap;
    }
    ImGui::ColorEdit3("Colour", &it.color.x);
    ImGui::EndGroup();
    ImGui::SameLine(0, 20);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(170, 170));
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 170, p.y + 170), IM_COL32(245, 246, 250, 255), 6);
    drawItemIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + 85, p.y + 85), 140, it);

    ImGui::Spacing();
    ImGui::TextDisabled("The item is signed with your staff key, so nobody else can make or change catalog items.");
    ImGui::Spacing();
    if (bigButton("Create", kGreen, ImVec2(140, 34))) {
        if (Catalog::create(it, m_catalogMsg)) {
            m_items = Catalog::load();
            m_newItem = Catalog::Item{};
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 34))) ImGui::CloseCurrentPopup();
    if (!m_catalogMsg.empty()) ImGui::TextWrapped("%s", m_catalogMsg.c_str());
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Touch scrolling: drag a list up and down with your finger, like a phone.
// ---------------------------------------------------------------------------

void PlayerApp::touchScroll() {
    if (!m_window->hasTouchScreen()) return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || io.WantTextInput) return;
    ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
    if (std::abs(drag.y) < 12.0f || std::abs(drag.y) < std::abs(drag.x) * 1.5f) return;   // not a vertical swipe
    // The scrollable window under the finger (or the one it's inside).
    ImGuiWindow* w = g.HoveredWindow;
    while (w && w->ScrollMax.y <= 0.0f && (w->Flags & ImGuiWindowFlags_ChildWindow)) w = w->ParentWindow;
    if (!w || w->ScrollMax.y <= 0.0f) return;
    ImGui::SetScrollY(w, std::clamp(w->Scroll.y - io.MouseDelta.y, 0.0f, w->ScrollMax.y));
    if (g.ActiveId != 0) ImGui::ClearActiveID();   // a swipe isn't a tap on whatever it started on
}
