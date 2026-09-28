#include "PlayerApp.h"
#include "../core/AppWindow.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../core/Settings.h"
#include "../game/GameSession.h"
#include "../game/Hud.h"
#include "../game/Profile.h"
#include "../game/SettingsWindow.h"
#include "../renderer/SceneRenderer.h"
#include "../scene/Physics.h"
#include "../scene/Serializer.h"
#include "../net/NetGame.h"

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
        drawTopBar();
        if (m_page == Page::Home) drawHome();
        else                      drawAvatar(dt);
    }

    ImGui::End();
    SettingsWindow::draw(&m_showSettings);
    drawJoinDialog();
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

void PlayerApp::drawTopBar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kTopBarBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 10));
    ImGui::BeginChild("##top", ImVec2(0, 56), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);

    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Guts&Bolts");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SameLine(0, 30);

    auto tab = [&](const char* label, Page page) {
        bool active = m_page == page;
        if (bigButton(label, active ? kAccent : ImVec4(0.17f, 0.18f, 0.22f, 1))) m_page = page;
        ImGui::SameLine();
    };
    tab("Home", Page::Home);
    tab("Avatar", Page::Avatar);
    if (bigButton("Join a Friend", ImVec4(0.25f, 0.4f, 0.75f, 1))) m_showJoin = true;
    ImGui::SameLine();
    if (bigButton("Settings", ImVec4(0.17f, 0.18f, 0.22f, 1))) m_showSettings = true;

    // Right side: who you are, and a shortcut to the editor.
    const char* studio = "Open Studio";
    std::string me = "Signed in as " + Profile::get().name;
    float right = ImGui::CalcTextSize(me.c_str()).x + ImGui::CalcTextSize(studio).x + 60;
    ImGui::SameLine(ImGui::GetWindowWidth() - right);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", me.c_str());
    ImGui::SameLine();
    if (bigButton(studio, ImVec4(0.45f, 0.30f, 0.12f, 1))) {
        if (!Paths::launch(Paths::sibling("GutsAndBolts")))
            m_status = "Couldn't find the Guts and Bolts editor next to this app.";
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make your own games in the Guts and Bolts editor");

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void PlayerApp::drawHome() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 18));
    ImGui::BeginChild("##home", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("Hi, %s!", Profile::get().name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Pick a game to play.");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(320);
    ImGui::InputTextWithHint("##search", "Search games...", &m_search);
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refreshGames();
    ImGui::SameLine();
    ImGui::TextDisabled("%d game%s", (int)m_games.size(), m_games.size() == 1 ? "" : "s");
    if (!m_status.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", m_status.c_str());
    ImGui::Spacing();

    if (m_games.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("No games yet! Open Studio, build something, and save it - "
                           "it will show up here.");
        ImGui::TextDisabled("Games folder: %s", Paths::gamesFolder().string().c_str());
    }

    // Card grid.
    const float cardW = 280.0f, thumbH = cardW * 9.0f / 16.0f, cardH = thumbH + 118.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    int perRow = std::max(1, (int)((avail + 16) / (cardW + 16)));
    int shown = 0;
    std::string q = lower(m_search);
    for (size_t i = 0; i < m_games.size(); ++i) {
        GameCard& g = m_games[i];
        if (!q.empty() && lower(g.info.title).find(q) == std::string::npos &&
            lower(g.info.author).find(q) == std::string::npos)
            continue;
        if (shown % perRow != 0) ImGui::SameLine(0, 16);
        ++shown;

        ImGui::PushID((int)i);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kCardBg);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##card", ImVec2(cardW, cardH), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (g.thumb) {
            ImGui::GetWindowDrawList()->AddImageRounded(
                (ImTextureID)(intptr_t)g.thumb->colorTexture(), p0, ImVec2(p0.x + cardW, p0.y + thumbH),
                ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 10.0f, ImDrawFlags_RoundCornersTop);
        } else {
            ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + cardW, p0.y + thumbH),
                                                      IM_COL32(60, 30, 30, 255), 10.0f, ImDrawFlags_RoundCornersTop);
        }
        ImGui::Dummy(ImVec2(cardW, thumbH));
        ImGui::SetCursorPos(ImVec2(12, thumbH + 8));
        ImGui::TextUnformatted(g.info.title.c_str());
        ImGui::SetCursorPosX(12);
        ImGui::TextDisabled("by %s", g.info.author.c_str());
        ImGui::SetCursorPosX(12);
        ImGui::PushTextWrapPos(cardW - 12);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.72f, 0.76f, 1));
        std::string desc = g.info.description.size() > 70 ? g.info.description.substr(0, 67) + "..."
                                                           : g.info.description;
        ImGui::TextUnformatted(desc.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::SetCursorPos(ImVec2(12, cardH - 40));
        ImGui::BeginDisabled(g.broken);
        if (bigButton("Play", kGreen, ImVec2(cardW - 110, 30))) joinGame(g.path);
        ImGui::SameLine();
        if (bigButton("Host", ImVec4(0.25f, 0.4f, 0.75f, 1), ImVec2(78, 30))) joinGame(g.path, true);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play with friends: they click Join and type your address");
        ImGui::EndDisabled();
        ImGui::EndChild();
        if (ImGui::IsItemHovered() && !g.info.description.empty())
            ImGui::SetTooltip("%s", g.info.description.c_str());
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
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

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 18));
    ImGui::BeginChild("##avatar", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

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
        if (bigButton(name, ImVec4(0.2f, 0.22f, 0.27f, 1), ImVec2(130, 34))) { me.colors = colors; changed = true; }
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
        if (bigButton(Player::hatName((HatStyle)h), on ? kAccent : ImVec4(0.2f, 0.22f, 0.27f, 1), ImVec2(95, 34))) {
            me.hat = (HatStyle)h;
            changed = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleVar();

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
