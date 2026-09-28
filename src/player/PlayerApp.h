#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "../renderer/Camera.h"
#include "../renderer/Framebuffer.h"
#include "../scene/Scene.h"
#include "../game/Catalog.h"
#include "../game/TouchControls.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

class AppWindow;
class NetServer;
class NetClient;
struct ChatLog;
class SceneRenderer;
class GameSession;

struct PlayerOptions {
    std::string game;              // join this game file straight away
    std::string page;              // start on "avatar" / "settings"
    std::string join;              // --join host[:port]
    bool        host = false;      // --host (with a game file)
    std::string say;               // --say <text>  (tests: chat once joined)
    std::string screenshot;        // --screenshot <out.ppm> (tests)
    int         frames = 120;
    std::string holdKey;
    bool        createStaff = false;   // --create-staff-account
    std::string touchTest;             // --touch-test stick|jump|look (tests: fake fingers)
    bool        testItems = false;     // --test-make-items (tests, staff only)
    std::string testGrantFor;          // --test-grant <account id> (tests, staff only: print a badge code)
    std::string testRedeem;            // --test-redeem <code> (tests)
    std::string testBoltsFor;          // --test-bolts-code <account id> (tests, staff only: print a 500 Bolts code)
    std::string testRedeemBolts;       // --test-redeem-bolts <code> (tests)
    std::string testBuy;               // --test-buy "<item name>" (tests: buy and wear it)
    std::string onlineTest;            // --online-test "op {json}|op {json}" (tests: talk to the server, print replies)
};

// Guts&BoltsPlayer: the platform app. Browse the games on this computer,
// dress up your avatar, and play — no editor in sight.
class PlayerApp {
public:
    explicit PlayerApp(PlayerOptions opts);
    ~PlayerApp();
    void run();

private:
    enum class Page { Home, Games, Avatar, GameInfo, Game, Catalog, Staff, Bolts, Create };

    struct GameCard {
        std::filesystem::path        path;
        GameInfo                     info;
        std::unique_ptr<Framebuffer> thumb;
        bool                         broken = false;
        bool                         gore = false;       // blood / oil on
        bool                         ragdoll = false;
    };

    void frame(float dt);
    void drawTopBar(ImVec2 pos, float width);
    void drawHome();
    void drawGames();
    void drawGameInfo();
    void drawRow(const char* title, const std::vector<int>& games, const char* seeAll);
    bool drawTile(int index);
    void drawAvatar(float dt);
    void drawGame(float dt);
    void drawPauseMenu();
    void drawCatalog();
    void drawItemDialog();
    void drawCreateItemDialog();
    void drawStaff();
    void drawBolts();

    // Online (a Guts&Bolts server) — PlayerOnline.cpp
    void drawServerButton(ImVec2 at);
    void drawServerDialog();
    void drawOnlineCatalog();
    void drawOnlineItemDialog();
    void drawCreate();
    void drawOnlineGames();
    void drawOnlineGameDialog();
    void drawOnlineBolts();
    void drawOnlineStaff();
    void refreshOnline(const std::string& what);   // "catalog", "games", "mine", "history"
    void onlinePlayTick(float dt);
    void drawAccount();
    void drawNotice();
    void updateTouch(ImVec2 min, ImVec2 max, bool acceptInput);

    void refreshGames();
    void joinGame(const std::filesystem::path& path, bool host = false);
    void joinServer(const std::string& address);
    void drawChat(ImVec2 min, ImVec2 max);
    void drawJoinDialog();
    ChatLog& chat();
    void sendChat(const std::string& text);
    void leaveGame();
    void buildAvatarStage();

    PlayerOptions                  m_opts;
    std::unique_ptr<AppWindow>     m_window;
    std::unique_ptr<SceneRenderer> m_renderer;
    std::unique_ptr<Scene>         m_scene;        // the game being played
    std::unique_ptr<GameSession>   m_session;
    std::unique_ptr<Scene>         m_avatarScene;  // preview on the Avatar page

    Page        m_page = Page::Home;
    std::vector<GameCard> m_games;
    std::string m_search;
    std::string m_status;                          // "Couldn't load ..." etc.
    std::string m_currentTitle;

    Camera      m_camera;
    Framebuffer m_view;
    Camera      m_avatarCam;
    Framebuffer m_avatarView;
    Framebuffer m_bannerView;
    int         m_selected = -1;                   // game shown on the GameInfo page
    std::string m_category = "all";                // filter on the Games page

    std::unique_ptr<NetServer>     m_server;       // we are hosting
    std::unique_ptr<NetClient>     m_client;       // we joined someone
    std::unique_ptr<ChatLog>       m_soloChat;     // chat when playing alone
    std::string m_chatInput;
    bool        m_chatOpen = false;
    bool        m_showJoin = false;
    std::string m_joinAddress;

    // Catalog
    std::vector<Catalog::Item> m_items;
    int         m_itemType = -1;                   // filter; -1 = everything
    int         m_openItem = -1;                   // item shown in the item dialog
    bool        m_showCreate = false;
    Catalog::Item m_newItem;
    std::string m_catalogMsg;

    // Account / staff
    std::string m_notice;                          // popup message
    std::string m_nameEdit, m_nameError;
    std::string m_redeemCode, m_redeemMsg;
    int         m_grantBadge = 1;
    std::string m_grantTo, m_grantCode, m_grantError;

    // Bolts
    std::string m_boltsCode, m_boltsMsg;           // redeem box on the Bolts page
    int         m_giveBolts = 100;                 // staff: amount for a Bolts code
    std::string m_giveBoltsTo, m_giveBoltsCode, m_giveBoltsError;
    std::string m_boltsToast;                      // "+5 Bolts for playing!"
    double      m_boltsToastUntil = 0.0;
    std::string m_buyMsg;                          // catalog item dialog

    // Online
    bool           m_showServer = false;
    std::string    m_serverInput, m_serverMsg;
    nlohmann::json m_onlineItems = nlohmann::json::array();   // server hats / shirts / pants
    nlohmann::json m_onlineGames = nlohmann::json::array();
    nlohmann::json m_myCreations = nlohmann::json::array();
    nlohmann::json m_onlineHistory = nlohmann::json::array();
    nlohmann::json m_foundUsers = nlohmann::json::array();
    int            m_openOnlineItem = -1, m_openOnlineGame = -1;
    std::string    m_onlineMsg, m_createMsg, m_staffMsg, m_findQuery;
    int            m_createKind = 0, m_createStyle = 2, m_createPrice = 0, m_giveServerBolts = 100;
    std::string    m_createName, m_createDesc, m_createPath;
    glm::vec3      m_createColor{0.9f, 0.2f, 0.2f};
    bool           m_busy = false;                 // waiting on the server
    float          m_onlinePlaySeconds = 0.0f;
    std::string    m_loaded;                       // lists already fetched this visit ("catalog games ...")

    TouchControls m_touch;
    ImVec2      m_chatMin{0, 0}, m_chatMax{0, 0};  // where the chat box was last frame
    size_t      m_chatSeen = 0;                    // phones: chat pops up briefly for new messages
    double      m_chatShowUntil = 0.0;
    void        touchScroll();

    bool        m_paused = false;
    bool        m_showSettings = false;
    int         m_frame = 0;
};
