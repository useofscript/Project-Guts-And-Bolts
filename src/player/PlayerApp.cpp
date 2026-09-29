#include "PlayerApp.h"
#include "SiteUi.h"
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
#include "../game/Bolts.h"
#include "../online/OnlineClient.h"
#include "../online/AssetCache.h"
#include "../online/Protocol.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cmath>
#include <cstdio>

using namespace Site;

namespace {

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
        a.hat = HatStyle::Cap; a.color = {0.85f, 0.1f, 0.1f}; a.price = 75;
        Catalog::create(a, msg); Log::info(msg);
        Catalog::Item b; b.name = "Bolt Tee"; b.type = Catalog::Type::Shirt; b.color = {0.2f, 0.5f, 0.9f};
        Catalog::create(b, msg); Log::info(msg);
        Catalog::Item c; c.name = "Oil Jeans"; c.type = Catalog::Type::Pants; c.color = {0.15f, 0.15f, 0.2f}; c.price = 250;
        Catalog::create(c, msg); Log::info(msg);
    }
    if (!m_opts.testGrantFor.empty()) {
        std::string err;
        // --test-grant <account id>[:badge key]
        std::string who = m_opts.testGrantFor, key = "tester";
        if (auto c = who.find(':'); c != std::string::npos) { key = who.substr(c + 1); who = who.substr(0, c); }
        Badges::Id bid = Badges::Id::Tester;
        Badges::fromKey(key, bid);
        std::string code = Badges::makeCode(bid, who, err);
        std::printf("BADGECODE %s %s\n", code.c_str(), err.c_str());
        std::fflush(stdout);
    }
    if (!m_opts.testRedeem.empty()) {
        std::string msg;
        Badges::redeem(m_opts.testRedeem, msg);
        std::printf("REDEEM %s\n", msg.c_str());
        std::fflush(stdout);
    }
    if (!m_opts.testBoltsFor.empty()) {
        std::string err;
        std::string code = Bolts::makeCode(500, m_opts.testBoltsFor, err);
        std::printf("BOLTSCODE %s %s\n", code.c_str(), err.c_str());
        std::fflush(stdout);
    }
    if (!m_opts.testRedeemBolts.empty()) {
        std::string msg;
        Bolts::redeem(m_opts.testRedeemBolts, msg);
        std::printf("REDEEMBOLTS %s (balance %lld)\n", msg.c_str(), Bolts::balance());
        std::fflush(stdout);
    }
    if (!m_opts.onlineTest.empty()) {
        // Each step: "op {json args}". $GRANT:<badge>:<account> in a string becomes a real signed grant.
        std::stringstream ss(m_opts.onlineTest);
        std::string step;
        while (std::getline(ss, step, '|')) {
            size_t sp = step.find(' ');
            std::string op = step.substr(0, sp);
            nlohmann::json args = sp == std::string::npos ? nlohmann::json::object()
                                                          : nlohmann::json::parse(step.substr(sp + 1), nullptr, false);
            if (!args.is_object()) args = nlohmann::json::object();
            if (args.contains("grantFor")) {   // make the signed badge here, with our key
                std::string err;
                Badges::Id bid = Badges::Id::Verified;
                Badges::fromKey(args.value("key", std::string("verified")), bid);
                auto g = Badges::makeGrant(bid, args["grantFor"].get<std::string>(), err);
                args["to"] = args["grantFor"]; args.erase("grantFor");
                args["key"] = g.first; args["sig"] = g.second;
            }
            if (args.contains("id") && args["id"] == "$LAST") args["id"] = m_testLastId;   // the thing uploaded just before
            if (args.contains("fileB64")) {    // upload a file from disk
                std::ifstream f(args["fileB64"].get<std::string>(), std::ios::binary);
                std::stringstream buf; buf << f.rdbuf();
                args["data"] = Online::base64Encode(buf.str());
                args.erase("fileB64");
            }
            Online::request(op, args, [this, op](const nlohmann::json& r) {
                if (r.contains("asset") && r["asset"].is_object()) m_testLastId = r["asset"].value("id", m_testLastId);
                nlohmann::json shown = r;
                if (shown.contains("data")) shown["data"] = "(" + std::to_string(shown["data"].get<std::string>().size()) + " base64 chars)";
                if (shown.contains("me")) shown["me"] = {{"name", r["me"].value("name", "")}, {"bolts", r["me"].value("bolts", 0)},
                                                         {"verified", r["me"].value("verified", false)},
                                                         {"staff", r["me"].value("staff", false)},
                                                         {"username", r["me"].value("username", "")},
                                                         {"userId", r["me"].value("userId", 0)},
                                                         {"hasPassword", r["me"].value("hasPassword", false)}};
                std::printf("ONLINE %s -> %s\n", op.c_str(), shown.dump().substr(0, 600).c_str());
                std::fflush(stdout);
            });
            Online::finishAll();
        }
    }
    buildAvatarStage();
    refreshGames();
    m_items = Catalog::load();
    std::printf("CATALOG %d items, account %s, staff %d\n", (int)m_items.size(), Account::shortId().c_str(),
                (int)Account::iAmStaff());
    for (const Catalog::Item& it : m_items) {
        if (m_opts.testBuy.empty() || it.name != m_opts.testBuy) continue;
        std::string msg;
        bool ok = Catalog::buy(it, msg);
        if (ok) Catalog::wear(it);
        std::printf("BUY %d %s (balance %lld, owns %d)\n", (int)ok, msg.c_str(), Bolts::balance(), (int)Catalog::owns(it));
    }
    std::fflush(stdout);

    if (m_opts.guest) Online::setGuest(true);   // tests: "Play as Guest"
    if (m_opts.page == "avatar") m_page = Page::Avatar;
    if (m_opts.page == "games") m_page = Page::Games;
    if (m_opts.page.rfind("game:", 0) == 0) { m_selected = std::atoi(m_opts.page.c_str() + 5); m_page = Page::GameInfo; }
    if (m_opts.page == "settings") m_showSettings = true;
    if (m_opts.page == "character") m_charPickOpen = true;   // test: the guest "Choose Your Character" box
    if (m_opts.page == "catalog") m_page = Page::Catalog;
    if (m_opts.page == "bolts") m_page = Page::Bolts;
    if (m_opts.page == "create") { m_page = Page::Create; m_createKind = m_opts.createTab; }
    if (m_opts.page == "people") m_page = Page::People;
    if (m_opts.page == "groups") m_page = Page::Groups;
    if (m_opts.page == "friends") m_page = Page::Friends;
    if (m_opts.page == "login") { m_page = Page::Login; m_loginTab = 1; }
    if (m_opts.page.rfind("servers:", 0) == 0 && !m_games.empty()) {   // tests: a game's Servers window
        m_selected = std::clamp(std::atoi(m_opts.page.c_str() + 8), 0, (int)m_games.size() - 1);
        m_page = Page::GameInfo;
        m_autoServers = true;
    }
    if (m_opts.page.rfind("group:", 0) == 0) { m_groupId = m_opts.page.substr(6); m_page = Page::Group; }
    if (m_opts.page.rfind("profile:", 0) == 0) { m_profileId = m_opts.page.substr(8); m_page = Page::Profile; }
    if (m_opts.page.rfind("item:", 0) == 0) { m_page = Page::Catalog; m_openItem = std::atoi(m_opts.page.c_str() + 5); }
    if (m_opts.page == "staff" && Account::iAmStaff()) m_page = Page::Staff;
    if (m_opts.page == "create-item" && Account::iAmStaff()) { m_page = Page::Catalog; m_showCreate = true; }
    // (--online-play / --private-server / --join-code wait until we're online: see frame())
    if (!m_opts.game.empty() && !m_opts.onlinePlay && !m_opts.privateServer)
        joinGame(m_opts.game, m_opts.host ? HostMode::Lan : HostMode::Solo);
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
        // Test helper: rename your first game from the Create page.
        if (!m_opts.testRename.empty() && m_page == Page::Create && m_frame == 30) {
            for (GameCard& g : m_games)
                if (!g.broken && g.info.author != "Guts and Bolts" && g.path.extension() == Paths::kExtension) {
                    std::printf("RENAME %s -> %s\n", g.info.title.c_str(), m_opts.testRename.c_str());
                    std::fflush(stdout);
                    renameGame(g.path, g.info.publishedId, m_opts.testRename);
                    break;
                }
        }
        // Test helper: drive the tool hotbar, one step every 25 frames.
        if (!m_opts.testTools.empty() && m_page == Page::Game && m_frame > 40 && m_frame % 25 == 0) {
            size_t sp = m_opts.testTools.find(' ');
            std::string step = m_opts.testTools.substr(0, sp);
            m_opts.testTools = sp == std::string::npos ? "" : m_opts.testTools.substr(sp + 1);
            if (step.size() == 1 && step[0] >= '1' && step[0] <= '9') m_session->selectToolSlot(step[0] - '1');
            else if (step == "click") m_session->click(0);
            else if (step == "drop") m_session->dropTool();
            else if (step == "print" && m_scene->player()) {
                Player* p = m_scene->player();
                std::string names;
                for (SceneNode* t : p->tools()) names += (names.empty() ? "" : ",") + t->name;
                SceneNode* held = p->equippedTool();
                glm::vec3 at = p->position();
                std::printf("TOOLS held=%s slots=%s at=%.1f,%.1f,%.1f\n", held ? held->name.c_str() : "-", names.c_str(), at.x, at.y, at.z);
                std::fflush(stdout);
            }
        }
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
        Scene preview;
        if (!Serializer::loadGameFile(preview, path.string())) {
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

void PlayerApp::joinGame(const std::filesystem::path& path, HostMode mode, const std::string& gameKey) {
    std::string err;
    if (!Serializer::loadGameFile(*m_scene, path.string(), &err)) {
        m_status = "Couldn't load " + path.filename().string() + (err.empty() ? "" : ": " + err);
        m_page = Page::Home;
        return;
    }
    Online::fetchSounds(*m_scene);   // server audio ("gb:" sounds) this game uses
    Profile& me = Profile::get();
    if (Player* p = m_scene->player()) {
        me.applyTo(*p);
        if (SceneNode* r = p->root()) r->name = Online::playerName();   // like Roblox: the character is named after you
    }
    m_session->scripts().setPlayerName(Online::playerName());
    *m_soloChat = ChatLog{};

    m_currentTitle = m_scene->info().title;
    m_currentAuthor = m_scene->info().author;
    m_loadingT = 1.4f;
    m_window->setTitle(m_currentTitle + " - Guts&Bolts Player");
    frameSpawn(*m_scene, m_camera);
    m_camera.distance = 12.0f;
    m_camera.pitch = 20.0f;
    m_camera.yaw = 90.0f;        // behind the character, looking the way it faces (-Z)
    if (m_opts.cameraYaw > -999.0f) m_camera.yaw = m_opts.cameraYaw;
    m_paused = false;
    m_status.clear();
    Log::clear();
    if (mode == HostMode::Lan) {
        m_server = std::make_unique<NetServer>(m_scene.get(), m_session.get());
        std::string herr;
        if (!m_server->start(kDefaultPort, herr)) {
            m_status = "Couldn't host: " + herr;
            m_server.reset();
        }
    } else if (mode == HostMode::Public || mode == HostMode::Private) {
        // Host through the Guts&Bolts server: players join via the server, never by our address.
        std::string server, herr;
        int port = 0;
        if (Online::online() && Online::serverHostPort(server, port)) {
            m_server = std::make_unique<NetServer>(m_scene.get(), m_session.get());
            nlohmann::json req = Online::signedRequest("relay.host", {{"game", gameKey.empty() ? "local:" + path.stem().string() : gameKey},
                                                            {"title", m_currentTitle}, {"private", mode == HostMode::Private},
                                                            {"max", 12}});
            if (!m_server->startRelay(server, port, req.dump(), herr)) {
                m_status = "Couldn't start a server (" + herr + "), so you're playing alone.";
                m_server.reset();
            }
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
    m_currentAuthor.clear();
    m_loadingT = 0.9f;
    m_joinedOnce = false;
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
    if (Online::isGuest() && Online::online()) { chat().add("", Online::kGuestChatText, true); return; }
    if (m_server)      m_server->say(text);
    else if (m_client) m_client->say(text);
    else               m_soloChat->add(Online::playerName(), text, false, Account::iAmStaff(), Badges::iHave(Badges::Id::Verified));
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
    Online::update();   // replies from the Guts&Bolts server
    if (m_autoServers && Online::online()) {
        m_autoServers = false;
        const GameCard& g = m_games[m_selected];
        openServers("local:" + g.path.stem().string(), g.info.title, localStarter(g.path));
    }
    if (Online::online() && (!m_opts.testSignup.empty() || !m_opts.testLogin.empty())) {   // tests
        std::string& t = m_opts.testSignup.empty() ? m_opts.testLogin : m_opts.testSignup;
        bool signup = !m_opts.testSignup.empty();
        size_t colon = t.find(':');
        std::string user = t.substr(0, colon), pass = colon == std::string::npos ? "" : t.substr(colon + 1);
        t.clear();
        if (signup) { m_loginUser = user; signUp(user, pass); }
        else        { m_loginUser = user; m_loginTab = 1; logIn(user, pass); }
    }
    if (!m_autoStarted && Online::online()) {   // test options that need the server first
        if (m_opts.onlinePlay && !m_opts.game.empty()) {
            m_autoStarted = true;
            std::filesystem::path path = m_opts.game;
            playGame("local:" + path.stem().string(), path.stem().string(), localStarter(path));
        } else if (m_opts.privateServer && !m_opts.game.empty()) {
            m_autoStarted = true;
            std::filesystem::path path = m_opts.game;
            joinGame(path, HostMode::Private);
        } else if (!m_opts.joinCode.empty()) {
            m_autoStarted = true;
            joinRelay("", m_opts.joinCode, "");
        }
    }
    m_window->lockLandscape(m_page == Page::Game);   // phones: games are landscape, the site isn't
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
        // Phones: thin margins, so the page gets as much of the small screen as it can.
        const bool smallScreen = size.x < 700.0f || size.y < 520.0f;
        const float gutter = smallScreen ? 6.0f : 16.0f;
        float width = std::min(1000.0f, size.x - gutter * 2.0f);
        ImVec2 col(pos.x + (size.x - width) * 0.5f, pos.y + (smallScreen ? 4.0f : 10.0f));
        drawTopBar(col, width);

        float top = ImGui::GetCursorScreenPos().y;
        ImGui::SetCursorScreenPos(ImVec2(col.x, top));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, smallScreen ? ImVec2(10, 8) : ImVec2(18, 14));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.88f, 0.9f, 0.93f, 1));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.62f, 0.66f, 0.72f, 1));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.5f, 0.56f, 0.66f, 1));
        ImGui::BeginChild("##content", ImVec2(width, pos.y + size.y - top - (smallScreen ? 4 : 10)), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImVec2 cpos = ImGui::GetWindowPos(), csize = ImGui::GetWindowSize();
        Classic::stripes(ImGui::GetWindowDrawList(), cpos, ImVec2(cpos.x + csize.x, cpos.y + csize.y));
        Classic::pushLight();
        // Esc / Android Back: go back to the home page.
        if (m_page != Page::Home && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_page = Page::Home;
        ImGui::PushTextWrapPos(0.0f);   // long lines wrap at the page edge (small phone screens)
        if (needsLogin()) drawLogin();   // online but not signed up: that comes first
        else switch (m_page) {
            case Page::Home:     drawHome(); break;
            case Page::Games:    drawGames(); break;
            case Page::Avatar:   drawAvatar(dt); break;
            case Page::GameInfo: drawGameInfo(); break;
            case Page::Catalog:  drawCatalog(); break;
            case Page::Staff:    drawStaff(); break;
            case Page::Bolts:    drawBolts(); break;
            case Page::Create:   drawCreate(); break;
            case Page::People:   drawPeople(); break;
            case Page::Profile:  drawProfile(); break;
            case Page::Groups:   drawGroups(); break;
            case Page::Group:    drawGroup(); break;
            case Page::Friends:  drawFriends(); break;
            case Page::Login:    drawLogin(); break;
            default: break;
        }
        ImGui::PopTextWrapPos();
        Classic::popLight();
        ImGui::EndChild();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }

    drawCharacterPicker();
    ImGui::End();
    if (m_page != Page::Game) touchScroll();
    if (m_showSettings) {   // dressed like the rest of the site: white box, blue title bar
        Classic::pushLight();
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.97f, 0.97f, 0.98f, 1));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.06f, 0.38f, 0.73f, 1));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.10f, 0.45f, 0.82f, 1));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.86f, 0.91f, 0.98f, 1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.80f, 0.88f, 0.98f, 1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, Classic::kBlue);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, Classic::kLink);
        SettingsWindow::draw(&m_showSettings);
        ImGui::PopStyleColor(8);
        Classic::popLight();
    }
    drawJoinDialog();
    drawServersDialog();
    drawItemDialog();
    drawCreateItemDialog();
    drawServerDialog();
    drawOnlineItemDialog();
    drawOnlineGameDialog();
    drawNotice();
    if (m_page != Page::Game && UpdateToast::draw("GutsAndBoltsPlayer")) m_window->close();
}

void PlayerApp::drawJoinDialog() {
    if (m_showJoin) { ImGui::OpenPopup("Join on local network"); m_showJoin = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(420), 0));
    if (ImGui::BeginPopupModal("Join on local network", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("For people on the same Wi-Fi only. Your friend opens a game, clicks Create a server, "
                           "then Local network, and tells you the address it shows (like 192.168.1.20). "
                           "Online, use your Friends list instead: it never shares anyone's address.");
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
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const bool portrait = screen.y > screen.x;
    const bool shortScreen = screen.y < 520.0f;   // a phone on its side: every pixel of height counts
#ifdef GB_MOBILE
    const float bannerH = portrait ? 64.0f : (shortScreen ? 54.0f : 72.0f);   // phones: keep the banner slim
    const float logoSize = portrait ? 24.0f : (shortScreen ? 30.0f : 40.0f);
#else
    const float bannerH = shortScreen ? 60.0f : 118.0f;
    const float logoSize = shortScreen ? 34.0f : 64.0f;
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
#ifndef GB_MOBILE
    drawServerButton(ImVec2(pos.x + 26, b1.y - 30));   // online / offline (phones: in the nav bar)
#endif

    // Account box, top-right of the banner: name, your Bolts and a link to the avatar editor.
    const bool guest = Online::online() && Online::isGuest();
    std::string hi = "Hi, " + (guest ? Online::guestName() : me.name);
    ImVec2 ts = ImGui::CalcTextSize(hi.c_str());
    const bool staff = Account::iAmStaff();
    const bool verifiedMe = Badges::iHave(Badges::Id::Verified);
    float badgeW = (staff ? 24.0f : 0.0f) + (verifiedMe ? 20.0f : 0.0f);
    std::string boltsText = Bolts::format(Online::online() ? Online::bolts() : Bolts::balance());
    float boltsW = 18 + 4 + ImGui::CalcTextSize(boltsText.c_str()).x;
    float line2 = boltsW + 14 + ImGui::CalcTextSize(Online::online() && Online::isGuest() ? "Sign up" : "Edit avatar").x;
    float boxW = std::max(ts.x + badgeW, line2) + 24;
    const float boxY = pos.y + std::min(10.0f, std::max(2.0f, (bannerH - 50.0f) * 0.5f));
    ImVec2 a(b1.x - boxW - (shortScreen ? 6 : 10), boxY), c(b1.x - (shortScreen ? 6 : 10), boxY + 50);
    dl->AddRectFilled(a, c, IM_COL32(255, 255, 255, 215), 5.0f);
    dl->AddRect(a, c, IM_COL32(120, 140, 170, 255), 5.0f);
    if (staff) Badges::drawIcon(dl, ImVec2(a.x + 22, a.y + 15), 20.0f, Badges::Id::Administrator);
    float nameX = a.x + 12 + (staff ? 24.0f : 0.0f);
    dl->AddText(ImVec2(nameX, a.y + 7), IM_COL32(30, 30, 40, 255), hi.c_str());
    if (verifiedMe) Badges::drawCheck(dl, ImVec2(nameX + ts.x + 10, a.y + 7 + ImGui::GetFontSize() * 0.5f), 15.0f);
    // Your Bolts (click to open the Bolts page).
    ImGui::SetCursorScreenPos(ImVec2(a.x + 10, a.y + 26));
    if (ImGui::InvisibleButton("##boltsbox", ImVec2(boltsW + 4, 20))) m_page = Page::Bolts;
    bool boltsHover = ImGui::IsItemHovered();
    if (boltsHover) {
        dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 200, 60, 70), 3.0f);
        ImGui::SetTooltip("Your Bolts. Click to earn more!");
    }
    Bolts::drawIcon(dl, ImVec2(a.x + 21, a.y + 36), 18.0f);
    dl->AddText(ImVec2(a.x + 33, a.y + 29), IM_COL32(140, 90, 0, 255), boltsText.c_str());
    ImGui::SetCursorScreenPos(ImVec2(a.x + 12 + boltsW + 14, a.y + 29));
    ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    if (guest) { if (ImGui::SmallButton("Sign up")) { m_page = Page::Login; m_loginTab = 0; } }
    else if (ImGui::SmallButton("Edit avatar")) m_page = Page::Avatar;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    // --- The blue nav bar (wraps onto a second row on narrow, portrait screens) ---
    const float navH = shortScreen ? 28.0f : 34.0f;
    const float navGap = portrait ? 16.0f : (shortScreen ? 20.0f : 26.0f);
    struct Item { const char* label; int action; };
    // "Friends (2)" when friend requests are waiting.
    static std::string friendsLabel;
    size_t waiting = m_friends.contains("incoming") ? m_friends["incoming"].size() : 0;
    friendsLabel = waiting ? "Friends (" + std::to_string(waiting) + ")" : std::string("Friends");
    std::vector<Item> items = {{"Home", 0}, {"Games", 1}, {"Catalog", 6}, {"Bolts", 8}, {"Create", 9}, {"Friends", 3}, {"People", 11}, {"Groups", 12}, {"Avatar", 2},
                               {"Develop", 4}, {"Settings", 5}};
    if (Badges::canVerify()) items.push_back({"Staff", 7});
#ifdef GB_MOBILE
    items.push_back({Online::online() ? "Online" : "Offline", 10});
#endif
#ifdef GB_MOBILE
    // No Studio on phones.
    items.erase(std::remove_if(items.begin(), items.end(), [](const Item& i) { return i.action == 4; }), items.end());
#endif
    // Lay the items out in rows first, so the bar knows how tall to be.
    std::vector<ImVec2> at;
    {
        float x = pos.x + (portrait ? 10 : 14), row = 0;
        for (const Item& it : items) {
            float w = ImGui::CalcTextSize(it.action == 3 ? friendsLabel.c_str() : it.label).x;
            if (x + w + 4 > pos.x + width && x > pos.x + 14) { x = pos.x + (portrait ? 10 : 14); row += navH; }
            at.push_back(ImVec2(x, row));
            x += w + navGap;
        }
    }
    float rows = (at.empty() ? 0 : at.back().y) + navH;
    ImVec2 n0(pos.x, b1.y), n1(pos.x + width, b1.y + rows);
    dl->AddRectFilledMultiColor(n0, n1, Classic::kNavTop, Classic::kNavTop, Classic::kNavBottom, Classic::kNavBottom);
    dl->AddLine(ImVec2(n0.x, n1.y - 1), ImVec2(n1.x, n1.y - 1), IM_COL32(10, 60, 130, 255));
    for (size_t k = 0; k < items.size(); ++k) {
        Item it = items[k];
        if (it.action == 3) it.label = friendsLabel.c_str();
        float x = at[k].x;
        float rowY = n0.y + at[k].y;
        ImVec2 sz = ImGui::CalcTextSize(it.label);
        ImVec2 p0(x - navGap * 0.3f, rowY), p1(x + sz.x + navGap * 0.3f, rowY + navH);
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(it.action);
        bool clicked = ImGui::InvisibleButton("##nav", ImVec2(p1.x - p0.x, navH));
        ImGui::PopID();
        bool active = (it.action == 0 && m_page == Page::Home) || (it.action == 1 && m_page == Page::Games) ||
                      (it.action == 2 && m_page == Page::Avatar) || (it.action == 6 && m_page == Page::Catalog) ||
                      (it.action == 7 && m_page == Page::Staff) || (it.action == 8 && m_page == Page::Bolts) ||
                      (it.action == 9 && m_page == Page::Create) || (it.action == 3 && m_page == Page::Friends) ||
                      (it.action == 11 && (m_page == Page::People || m_page == Page::Profile)) ||
                      (it.action == 12 && (m_page == Page::Groups || m_page == Page::Group));
        if (ImGui::IsItemHovered() || active)
            dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, active ? 60 : 35));
        dl->AddText(ImVec2(x + 1, rowY + (navH - sz.y) * 0.5f + 1), IM_COL32(0, 30, 80, 180), it.label);
        dl->AddText(ImVec2(x, rowY + (navH - sz.y) * 0.5f), IM_COL32(255, 255, 255, 255), it.label);
        if (clicked) {
            switch (it.action) {
                case 0: m_page = Page::Home; m_loaded.clear(); break;
                case 1: m_page = Page::Games; m_category = "all"; break;
                case 2: m_page = Page::Avatar; break;
                case 3: m_page = Page::Friends; m_friendsAt = -100.0; m_friendMsg.clear(); break;
                case 4:
                    if (!Paths::launch(Paths::sibling("GutsAndBolts")))
                        m_status = "Couldn't find the Guts and Bolts editor next to this app.";
                    break;
                case 5: m_showSettings = true; break;
                case 6: m_page = Page::Catalog; m_items = Catalog::load(); m_loaded.clear(); break;
                case 7: m_page = Page::Staff; break;
                case 8: m_page = Page::Bolts; m_loaded.clear(); break;
                case 9: m_page = Page::Create; m_loaded.clear(); break;
                case 11: m_page = Page::People; m_socialMsg.clear(); m_loaded.clear(); break;
                case 12: m_page = Page::Groups; m_socialMsg.clear(); m_loaded.clear(); break;
                case 10: m_serverInput = Online::serverAddress(); m_showServer = true; break;
            }
        }
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
    // Signed up but no password yet (like an account made before passwords existed):
    // you can't log in anywhere else, including the website, until you set one.
    if (Online::online() && Online::me().value("userId", 0LL) > 0 && !Online::me().value("hasPassword", true)) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 0.95f, 0.8f, 1));
        ImGui::BeginChild("##nopw", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextColored(ImVec4(0.55f, 0.35f, 0.0f, 1), "Your account has no password yet.");
        ImGui::TextWrapped("Set one so you can log in on the website and other devices as @%s.",
                           Online::me().value("username", std::string()).c_str());
        if (Classic::button("Set a password", Classic::kBlue)) {
            m_page = Page::Login;
            m_loginTab = 0;
            m_loginUser = Online::me().value("username", std::string());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    // Online: games people published.
    drawOnlineGames();
    // Daily Bolts waiting for you?
    if (Online::online() ? Online::me().value("canDaily", false) : Bolts::canClaimDaily()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        const bool narrow = w < 560;   // phones held upright: the button goes under the text
        float h = narrow ? 84.0f : 46.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 244, 205, 255), 6.0f);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(225, 180, 60, 255), 6.0f);
        Bolts::drawIcon(dl, ImVec2(p.x + 26, p.y + 23), 30.0f);
        ImGui::SetCursorScreenPos(ImVec2(p.x + 50, p.y + 6));
        ImGui::TextColored(ImVec4(0.45f, 0.3f, 0.0f, 1), "Your daily Bolts are ready!");
        ImGui::SetCursorScreenPos(ImVec2(p.x + 50, p.y + 24));
        if (!narrow) ImGui::TextDisabled("Claim %lld Bolts every day. Spend them in the Catalog.", Bolts::kDaily);
        ImGui::SetCursorScreenPos(narrow ? ImVec2(p.x + 50, p.y + 44) : ImVec2(p.x + w - 130, p.y + 8));
        if (Classic::button(narrow ? ("Claim " + std::to_string(Bolts::kDaily) + " Bolts").c_str() : "Claim", Classic::kPlay,
                            ImVec2(narrow ? 170.0f : 120.0f, 30))) {
            if (Online::online())
                Online::request("bolts.daily", nlohmann::json::object(), [this](const nlohmann::json& r) {
                    m_boltsMsg = r.value("ok", false) ? "You got 25 Bolts! Come back tomorrow for more." : r.value("error", std::string());
                });
            else Bolts::claimDaily(m_boltsMsg);
            m_page = Page::Bolts;
            m_loaded.clear();
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 10));
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

    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Tip: press Play to jump into a public server, or open a game and click Create a server to play privately with friends.");
    ImGui::PopTextWrapPos();
}

void PlayerApp::drawGames() {
    const char* names[] = {"all", "recent", "carnage", "classic"};
    const char* titles[] = {"All Games", "Recently Played", "Carnage", "Classic"};
    int cur = 0;
    for (int i = 0; i < 4; ++i) if (m_category == names[i]) cur = i;

    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(titles[cur]);
    ImGui::SetWindowFontScale(1.0f);
    const float refreshW = ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::min(170.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::Combo("##cat", &cur, titles, 4)) m_category = names[cur];
    if (!portraitScreen()) ImGui::SameLine();   // phones: search goes on its own line
    ImGui::SetNextItemWidth(std::max(80.0f, std::min(260.0f, ImGui::GetContentRegionAvail().x - refreshW)));
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

    const bool tall = portraitScreen();
    float picW = tall ? ImGui::GetContentRegionAvail().x : std::min(560.0f, ImGui::GetContentRegionAvail().x * 0.6f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(picW, picW * 9.0f / 16.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g.thumb)
        dl->AddImage((ImTextureID)(intptr_t)g.thumb->colorTexture(), p,
                     ImVec2(p.x + picW, p.y + picW * 9.0f / 16.0f), ImVec2(0, 1), ImVec2(1, 0));
    dl->AddRect(p, ImVec2(p.x + picW, p.y + picW * 9.0f / 16.0f), IM_COL32(140, 150, 165, 255));

    if (tall) ImGui::Spacing();   // phones held upright: buttons go under the picture
    else      ImGui::SameLine(0, 20);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(g.broken);
    const std::string key = "local:" + g.path.stem().string();
    ImGui::BeginDisabled(m_busy);
    if (Classic::button(m_busy ? "Finding a server..." : "Play", Classic::kPlay, ImVec2(220, 56)))
        playGame(key, g.info.title, localStarter(g.path));
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (Classic::button("Create a server", Classic::kBlue, ImVec2(220, 34))) openServers(key, g.info.title, localStarter(g.path));
    ImGui::EndDisabled();
    ImGui::TextDisabled(Online::online() ? "Play puts you in a public server." : "Offline: Play is just you.");
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
    const bool tall = portraitScreen();
#ifdef GB_MOBILE
    float previewW = tall ? avail.x : std::max(160.0f, avail.x * 0.38f);
#else
    float previewW = tall ? avail.x : std::max(200.0f, avail.x * 0.55f);
#endif
    ImGui::BeginChild("##preview", ImVec2(previewW, tall ? std::min(260.0f, avail.y * 0.4f) : 0.0f), ImGuiChildFlags_Borders);
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
    if (!tall) ImGui::SameLine(0, 24);   // side by side, or stacked on an upright phone

    // Right: options.
    ImGui::BeginChild("##opts", ImVec2(0, 0));
    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("Your Avatar");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("This is how you look in every game.");
    ImGui::Spacing();

    const bool signedUp = Online::online() && Online::me().value("userId", 0LL) > 0;
    if (signedUp) {   // online, your name is your username (one of a kind, so nobody can pretend to be you)
        ImGui::Text("Name: %s", me.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(your username)");
    }
    if (!signedUp) {   // playing offline: call yourself what you like
        ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Display name").x - ImGui::GetStyle().ItemInnerSpacing.x));
        if (!ImGui::IsAnyItemActive() && m_nameEdit != me.name && m_nameError.empty()) m_nameEdit = me.name;
        if (ImGui::InputText("Display name", &m_nameEdit) && m_nameEdit.size() > 20) m_nameEdit.resize(20);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (me.rename(m_nameEdit, m_nameError)) m_nameError.clear();
            else m_nameEdit = me.name;
        }
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
    for (int h = 0; h < kHatStyleCount; ++h) {
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
        if (h % 4 != 3 && h + 1 < kHatStyleCount) ImGui::SameLine();   // four to a row
    }

    drawAccount();

    ImGui::EndChild();
    ImGui::EndChild();

    if (changed && p) {
        me.applyTo(*p);
        me.save();
        m_avatarPushAt = ImGui::GetTime() + 1.5;   // then save it on the server too (after you stop clicking)
    }
    if (m_avatarPushAt > 0.0 && ImGui::GetTime() > m_avatarPushAt) {
        m_avatarPushAt = 0.0;
        if (Online::online() && Online::me().value("userId", 0LL) > 0) Online::pushAvatar();
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
        if (m_client->state() == NetClient::State::Joined) {
            m_currentTitle = m_client->gameTitle();
            if (!m_joinedOnce) { m_joinedOnce = true; Online::fetchSounds(*m_scene); }   // server audio the host's game uses
        }
    }
    if (m_server) m_server->update(dt);
    if (!m_server && !m_client) m_soloChat->update(dt);

    bool connecting = m_client && m_client->state() != NetClient::State::Joined;
    if (connecting) {
        drawLoading(pos, size, 1.0f, "Joining server");
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) leaveGame();
        return;
    }

    // Chat: "/" or Enter starts typing (not for guests: they sign up to chat).
    const bool guestChat = Online::online() && Online::isGuest();
    if (!m_chatOpen && !guestChat && !io.WantTextInput && !m_paused &&
        (ImGui::IsKeyPressed(ImGuiKey_Slash, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
        m_chatOpen = true;

    // Simulate (the world keeps running while the menu is open, like Roblox).
    bool acceptInput = !m_paused && !m_showSettings && !m_chatOpen;
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    const bool touch = GraphicsSettings::get().touchEnabled();
    if (touch) updateTouch(pos, max, acceptInput);
    else       m_session->setTouchInput(glm::vec2(0.0f), false);
    m_session->update(dt, m_camera.yaw, acceptInput);

    // Bolts for playing (not while the menu is open). Online, the server keeps count.
    if (!m_paused && Online::online()) onlinePlayTick(dt);
    else if (!m_paused) {
        if (long long got = Bolts::addPlayTime(dt)) {
            m_boltsToast = "+" + Bolts::format(got) + " Bolts for playing!";
            m_boltsToastUntil = ImGui::GetTime() + 4.0;
        }
    }

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
    ImVec2 pointer = tapped ? tapAt : ImGui::GetMousePos();
    const bool onHotbar = Hud::overHotbar(pos, max, *m_scene, pointer);   // picking a tool isn't swinging it
    if (acceptInput && !onHotbar && (tapped || (!touch && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))) {
        ImVec2 m = pointer;
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
    if (int slot = Hud::drawHotbar(dl, pos, max, *m_scene, tapped && onHotbar ? &tapAt : nullptr); slot >= 0 && acceptInput)
        m_session->selectToolSlot(slot);
    Hud::drawBubbles(dl, pos, max, *m_scene, m_camera.projection() * m_camera.view(), chat().bubbles);
    if (m_server)      Hud::drawPlayerList(dl, pos, max, m_server->players());
    else if (m_client) Hud::drawPlayerList(dl, pos, max, m_client->players());
    else Hud::drawPlayerList(dl, pos, max, {{Online::playerName(), Account::iAmStaff(), Badges::iHave(Badges::Id::Verified),
                                             m_session->scripts().leaderstats(Online::playerName())}});
    if (m_loadingT <= 0.3f) drawChat(pos, max);   // not over the loading screen

    // "+5 Bolts for playing!" popup, top middle.
    if (ImGui::GetTime() < m_boltsToastUntil) {
        ImVec2 ts = ImGui::CalcTextSize(m_boltsToast.c_str());
        float w = ts.x + 50, x = pos.x + (size.x - w) * 0.5f, y = pos.y + 16;
        float fade = (float)std::min(1.0, m_boltsToastUntil - ImGui::GetTime());
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 34), IM_COL32(40, 30, 10, (int)(210 * fade)), 17.0f);
        Bolts::drawIcon(dl, ImVec2(x + 20, y + 17), 22.0f);
        dl->AddText(ImVec2(x + 38, y + (34 - ts.y) * 0.5f), IM_COL32(255, 215, 90, (int)(255 * fade)), m_boltsToast.c_str());
    }

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

    if (m_loadingT > 0.0f) {   // the loading screen fades away as the game appears
        m_loadingT -= dt;
        drawLoading(pos, size, std::clamp(m_loadingT / 0.6f, 0.0f, 1.0f), "Starting");
    }

    if (m_paused) drawPauseMenu();
}

void PlayerApp::drawLoading(ImVec2 pos, ImVec2 size, float alpha, const char* status) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float base = ImGui::GetFontSize();
    const int a = (int)(255 * alpha);
    auto col = [a](int r, int g, int b, float k = 1.0f) { return IM_COL32(r, g, b, (int)(a * k)); };
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    // Dark gray, a little lighter in the middle, like the old loading screens.
    dl->AddRectFilledMultiColor(pos, max, col(34, 36, 42), col(34, 36, 42), col(14, 15, 18), col(14, 15, 18));

    std::string title = m_currentTitle;
    if (title.rfind("Joining ", 0) == 0) title = title.substr(8);
    if (title.size() > 3 && title.compare(title.size() - 3, 3, "...") == 0) title.resize(title.size() - 3);
    if (title.empty()) title = "Guts&Bolts";
    const float wrap = std::max(120.0f, size.x - 60.0f);
    float big = base * 2.2f;
    ImVec2 ts = font->CalcTextSizeA(big, FLT_MAX, wrap, title.c_str());
    if (ts.x > wrap * 0.98f) { big = base * 1.6f; ts = font->CalcTextSizeA(big, FLT_MAX, wrap, title.c_str()); }
    float y = pos.y + size.y * 0.36f - ts.y * 0.5f;
    dl->AddText(font, big, ImVec2(pos.x + (size.x - ts.x) * 0.5f + 2, y + 2), col(0, 0, 0, 0.6f), title.c_str(), nullptr, wrap);
    dl->AddText(font, big, ImVec2(pos.x + (size.x - ts.x) * 0.5f, y), col(255, 255, 255), title.c_str(), nullptr, wrap);
    y += ts.y + 6;
    if (!m_currentAuthor.empty()) {
        std::string by = "By " + m_currentAuthor;
        ImVec2 bs = ImGui::CalcTextSize(by.c_str());
        dl->AddText(ImVec2(pos.x + (size.x - bs.x) * 0.5f, y), col(170, 176, 190), by.c_str());
        y += bs.y;
    }

    // The spinner: a ring of dots chasing each other round a Bolts coin.
    ImVec2 c(pos.x + size.x * 0.5f, y + 50);
    const float t = (float)ImGui::GetTime();
    const int dots = 10;
    for (int i = 0; i < dots; ++i) {
        float ang = (float)i / dots * 6.2831853f;
        float lag = std::fmod(t * 1.4f - (float)i / dots + 10.0f, 1.0f);   // 0 = the lead dot
        dl->AddCircleFilled(ImVec2(c.x + std::cos(ang) * 26, c.y + std::sin(ang) * 26), 4.0f,
                            col(255, 255, 255, 0.2f + 0.8f * (1.0f - lag)));
    }
    if (alpha > 0.5f) Bolts::drawIcon(dl, c, 26.0f);
    char line[64];
    std::snprintf(line, sizeof(line), "%s%.*s", status, 1 + (int)(t * 3.0f) % 3, "...");
    ImVec2 ls = ImGui::CalcTextSize(status);
    dl->AddText(ImVec2(pos.x + (size.x - ls.x) * 0.5f, c.y + 40), col(220, 224, 232), line);

    // The logo in the corner.
    const float logoSize = std::min(34.0f, size.y * 0.08f);
    const ImVec2 lsz = font->CalcTextSizeA(logoSize, FLT_MAX, 0.0f, "GUTS&BOLTS");
    if (alpha > 0.99f) Classic::logo(dl, ImVec2(max.x - lsz.x - 22, max.y - lsz.y - 18), logoSize, "GUTS&BOLTS");
    else dl->AddText(font, logoSize, ImVec2(max.x - lsz.x - 22, max.y - lsz.y - 18), col(222, 34, 28), "GUTS&BOLTS");
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
            ImGui::TextColored(l.admin ? ImVec4(1.0f, 0.85f, 0.4f, 1) : ImVec4(0.55f, 0.8f, 1.0f, 1), "%s", l.from.c_str());
            if (l.verified || l.admin) { ImGui::SameLine(0, 3); Badges::check(ImGui::GetTextLineHeight() * 0.9f); }
            ImGui::SameLine(0, 0);
            ImGui::TextColored(l.admin ? ImVec4(1.0f, 0.85f, 0.4f, 1) : ImVec4(0.55f, 0.8f, 1.0f, 1), ":");
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
    } else if (Online::online() && Online::isGuest()) {
        // Greyed out, with the reason, like Roblox's guests.
        ImGui::BeginDisabled();
        static std::string none;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##guestsay", Online::kGuestChatText, &none, ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Guests can't chat. Leave the game and sign up (it's free) to talk to other players.");
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
    ImGui::SetNextWindowSize(ImVec2(fitWidth(320), 0));
    ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted(m_currentTitle.c_str());
    ImGui::SetWindowFontScale(1.0f);
    if (m_server && m_server->relayed()) {
        if (!m_server->relayCode().empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1), "Private server - code: %s", m_server->relayCode().c_str());
            ImGui::TextDisabled("Friends can also join from their Friends list.");
        } else {
            ImGui::TextDisabled(m_server->relayReady() ? "Public server" : "Starting the server...");
        }
    } else if (m_server) {
        ImGui::TextDisabled("Local network server");
    } else if (!m_client) {
        ImGui::TextDisabled("Offline - just you");
    }
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
    if (Online::online() && Online::me().value("userId", 0LL) > 0) {
        const nlohmann::json& om = Online::me();
        ImGui::Text("Logged in as @%s", om.value("username", std::string()).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(user #%lld)", om.value("userId", 0LL));
        if (om.value("hasPassword", false)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Log out")) ImGui::OpenPopup("##logout");
            if (ImGui::BeginPopup("##logout")) {
                ImGui::TextUnformatted("Log out of this device? You can log back in with your username and password.");
                if (ImGui::Button("Yes, log out")) { ImGui::CloseCurrentPopup(); logOut(); }
                ImGui::EndPopup();
            }
        } else {
            ImGui::TextColored(ImVec4(0.75f, 0.45f, 0.0f, 1), "No password yet, so you can only use this account on this device.");
            if (Classic::button("Set a password", Classic::kBlue)) {
                m_page = Page::Login;
                m_loginTab = 0;
                m_loginUser = om.value("username", std::string());
            }
        }
    }
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
    if (!Badges::canVerify()) { m_page = Page::Home; return; }
    const bool official = Account::iAmStaff();
    Badges::icon(official ? Badges::Id::Administrator : Badges::Id::Staff, 40.0f);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Staff Tools");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled(official ? "Only the official Guts account can see all of this page."
                                 : "You're Staff: you can verify people.");
    ImGui::EndGroup();

    ImGui::SeparatorText(official ? "Give someone a badge" : "Verify someone");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Ask them for their account ID (Avatar page > Copy full ID), make a code here and send "
                        "it to them. The code only works for their account. (When everyone uses a Guts&Bolts "
                        "server, you can verify people straight from the server section below instead.)");
    ImGui::PopTextWrapPos();
    std::vector<const char*> names;
    std::vector<int> ids;
    for (int i = 0; i < (int)Badges::Id::Count; ++i) {
        const Badges::Info& in = Badges::info((Badges::Id)i);
        if (in.grantable && (official || in.staffCanGive)) { names.push_back(in.name); ids.push_back(i); }
    }
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

    if (Online::online()) drawOnlineStaff();   // verify people straight from the server
    if (!official) return;   // the rest is for the official account only

    ImGui::SeparatorText("Give someone Bolts");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Same idea as badges: make a code for their account ID and send it to them. Each code "
                        "works once, and only for that account.");
    ImGui::PopTextWrapPos();
    Bolts::drawIcon(ImGui::GetWindowDrawList(), ImVec2(ImGui::GetCursorScreenPos().x + 14, ImGui::GetCursorScreenPos().y + 14), 26.0f);
    ImGui::Dummy(ImVec2(28, 28));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputInt("Bolts", &m_giveBolts, 25, 100)) m_giveBolts = std::clamp(m_giveBolts, 1, 1000000);
    ImGui::SetNextItemWidth(520);
    ImGui::InputTextWithHint("Their account ID##bolts", "64 letters and numbers", &m_giveBoltsTo);
    if (Classic::button("Make Bolts code", Classic::kPlay, ImVec2(180, 30))) {
        m_giveBoltsError.clear();
        m_giveBoltsCode = Bolts::makeCode(m_giveBolts, m_giveBoltsTo, m_giveBoltsError);
    }
    if (!m_giveBoltsError.empty()) ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "%s", m_giveBoltsError.c_str());
    if (!m_giveBoltsCode.empty()) {
        ImGui::InputTextMultiline("##boltscode", &m_giveBoltsCode, ImVec2(520, 60), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Copy code##bolts")) ImGui::SetClipboardText(m_giveBoltsCode.c_str());
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

// The Bolts page: your balance, the daily reward, ways to earn, codes and history.
void PlayerApp::drawBolts() {
    if (Online::online()) { drawOnlineBolts(); return; }   // Bolts kept on the server
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    Bolts::drawIcon(dl, ImVec2(p.x + 30, p.y + 30), 58.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 72, p.y + 2));
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Bolts");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("The Guts&Bolts currency. Earn them, then spend them in the Catalog.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(p.y + 66, ImGui::GetItemRectMax().y + 8)));

    // Balance card.
    {
        ImVec2 a = ImGui::GetCursorScreenPos();
        float w = std::min(ImGui::GetContentRegionAvail().x, 420.0f);
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + 70), IM_COL32(255, 248, 225, 255), 8.0f);
        dl->AddRect(a, ImVec2(a.x + w, a.y + 70), IM_COL32(225, 180, 60, 255), 8.0f);
        ImGui::SetCursorScreenPos(ImVec2(a.x + 14, a.y + 8));
        ImGui::TextDisabled("Your balance");
        ImGui::SetCursorScreenPos(ImVec2(a.x + 14, a.y + 28));
        ImGui::SetWindowFontScale(1.6f);
        Bolts::amount(Bolts::balance(), 30.0f);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + 80));
    }
    if (Bolts::wasReset()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.1f, 1),
                           "Your Bolts file had been edited by hand, so your Bolts were reset. (Bolts are signed "
                           "with your account key, so they can't be typed in.)");
        ImGui::PopTextWrapPos();
    }

    // Daily reward.
    ImGui::SeparatorText("Daily reward");
    if (Bolts::canClaimDaily()) {
        if (Classic::button(("Claim " + std::to_string(Bolts::kDaily) + " Bolts").c_str(), Classic::kPlay, ImVec2(200, 34)))
            Bolts::claimDaily(m_boltsMsg);
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Claimed today", ImVec2(200, 34));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Next one in %s", Bolts::timeUntilDaily().c_str());
    }
    if (!m_boltsMsg.empty()) ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.15f, 1), "%s", m_boltsMsg.c_str());

    // Ways to earn.
    ImGui::SeparatorText("Ways to earn");
    ImGui::PushTextWrapPos(0);
    ImGui::Bullet(); ImGui::TextWrapped("Come back every day: %lld Bolts.", Bolts::kDaily);
    ImGui::Bullet(); ImGui::TextWrapped("Play games: %lld Bolts for every %d minutes (up to %lld a day - %lld so far today).",
                                        Bolts::kPlayReward, (int)(Bolts::kPlaySeconds / 60), Bolts::kPlayDailyCap,
                                        Bolts::earnedFromPlayToday());
    ImGui::Bullet(); ImGui::TextWrapped("Get a Bolts code from the Guts&Bolts staff (contests, helping out, finding bugs...).");
    ImGui::PopTextWrapPos();

    // Redeem a code.
    ImGui::SeparatorText("Redeem a Bolts code");
    ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x - 90));
    ImGui::InputTextWithHint("##boltscode", "BOLTS-...", &m_boltsCode);
    ImGui::SameLine();
    if (Classic::button("Redeem", Classic::kBlue)) {
        Bolts::redeem(m_boltsCode, m_boltsMsg);
        m_boltsCode.clear();
    }
    ImGui::TextDisabled("Your account ID: %s...", Account::shortId().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy full ID")) ImGui::SetClipboardText(Account::id().c_str());

    // History, newest first.
    ImGui::SeparatorText("History");
    const auto& h = Bolts::history();
    if (h.empty()) ImGui::TextDisabled("Nothing yet.");
    if (ImGui::BeginTable("##boltshistory", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        const bool narrow = ImGui::GetContentRegionAvail().x < 560;
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, narrow ? 64.0f : 150.0f);
        ImGui::TableSetupColumn("What");
        ImGui::TableSetupColumn("Bolts", ImGuiTableColumnFlags_WidthFixed, narrow ? 64.0f : 90.0f);
        int shown = 0;
        for (auto it = h.rbegin(); it != h.rend() && shown < 40; ++it, ++shown) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::time_t t = (std::time_t)it->time;
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char when[32];
            std::strftime(when, sizeof(when), narrow ? "%b %d" : "%b %d, %H:%M", &tm);
            ImGui::TextDisabled("%s", when);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", it->reason.c_str());
            ImGui::TableNextColumn();
            if (it->amount >= 0) ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "+%s", Bolts::format(it->amount).c_str());
            else ImGui::TextColored(ImVec4(0.75f, 0.2f, 0.15f, 1), "%s", Bolts::format(it->amount).c_str());
        }
        ImGui::EndTable();
    }
}

void PlayerApp::drawNotice() {
    static bool opened = false;
    if (m_notice.empty()) { opened = false; return; }
    if (!opened) { ImGui::OpenPopup("Guts&Bolts##notice"); opened = true; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(620), 0));
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


void PlayerApp::drawCatalog() {
    if (Online::online()) { drawOnlineCatalog(); return; }   // the server's catalog
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Catalog");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Hats, shirts and pants for your avatar.");
    ImGui::Spacing();

    const char* tabs[] = {"All", "Hats", "Shirts", "Pants"};
    float tabW = std::min(90.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f);
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        bool on = m_itemType == i - 1;
        if (on ? Classic::button(tabs[i], Classic::kBlue, ImVec2(tabW, 28)) : ImGui::Button(tabs[i], ImVec2(tabW, 28)))
            m_itemType = i - 1;
    }
    if (Account::iAmStaff()) {
        if (portraitScreen()) ImGui::Spacing(); else ImGui::SameLine(ImGui::GetContentRegionMax().x - 140);
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
        if (Catalog::owns(it) && it.price > 0) ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "Owned");
        else if (it.price > 0) Bolts::amount(it.price);
        else ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "Free");
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void PlayerApp::drawItemDialog() {
    if (m_openItem >= (int)m_items.size()) m_openItem = -1;
    if (m_openItem >= 0 && !ImGui::IsPopupOpen("Catalog Item")) ImGui::OpenPopup("Catalog Item");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(520), 0));
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
    const bool owned = Catalog::owns(it);
    if (it.price == 0) ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "Free");
    else if (owned) ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "You own this");
    else Bolts::amount(it.price, 20.0f);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(it.description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Spacing();

    bool wearing = Catalog::isWearing(it);
    if (it.price > 0 && !owned) {
        // Buy it with Bolts.
        long long have = Bolts::balance();
        std::string label = "Buy for " + Bolts::format(it.price);
        ImGui::BeginDisabled(have < it.price);
        if (bigButton(label.c_str(), kGreen, ImVec2(170, 34))) {
            if (Catalog::buy(it, m_buyMsg)) {
                Catalog::wear(it);
                if (Player* pl = m_avatarScene->player()) Profile::get().applyTo(*pl);
            }
        }
        ImGui::EndDisabled();
        if (have < it.price && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("You need %s more Bolts", Bolts::format(it.price - have).c_str());
    } else {
        ImGui::BeginDisabled(wearing);
        if (bigButton(wearing ? "Wearing" : "Wear", kGreen, ImVec2(140, 34))) {
            if (it.price == 0) Catalog::buy(it, m_buyMsg);   // free: it's yours
            Catalog::wear(it);
            if (Player* pl = m_avatarScene->player()) Profile::get().applyTo(*pl);
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 34))) { m_openItem = -1; m_buyMsg.clear(); ImGui::CloseCurrentPopup(); }
    if (Account::iAmStaff()) {
        ImGui::SameLine();
        if (bigButton("Remove from catalog", ImVec4(0.75f, 0.25f, 0.25f, 1), ImVec2(0, 34))) {
            Catalog::remove(it, m_catalogMsg);
            m_items = Catalog::load();
            m_openItem = -1;
            ImGui::CloseCurrentPopup();
        }
    }
    // Your balance, and what happened when you tried to buy.
    ImGui::Spacing();
    ImGui::TextDisabled("You have");
    ImGui::SameLine();
    Bolts::amount(Bolts::balance());
    if (it.price > 0 && !owned && Bolts::balance() < it.price) {
        ImGui::SameLine();
        ImGui::TextDisabled("- earn more on the Bolts page");
    }
    if (!m_buyMsg.empty()) ImGui::TextWrapped("%s", m_buyMsg.c_str());
    ImGui::EndPopup();
}

void PlayerApp::drawCreateItemDialog() {
    if (m_showCreate) {
        if (Account::iAmStaff()) ImGui::OpenPopup("Create Catalog Item");
        m_showCreate = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(600), 0));
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
    int price = (int)it.price;
    ImGui::SetNextItemWidth(300);
    if (ImGui::InputInt("Price (Bolts)", &price, 5, 50)) it.price = std::clamp(price, 0, 1000000);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = free");
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
