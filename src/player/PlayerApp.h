#pragma once
#include "../game/GameGui.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "../renderer/Camera.h"
#include "../renderer/Framebuffer.h"
#include "../scene/Scene.h"

namespace Catalog { struct Item; }
#include "../game/Catalog.h"
#include "../game/PlayerEntry.h"
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
    bool        guest = false;     // --guest  (tests: press "Play as Guest")
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
    bool        onlinePlay = false;    // --online-play (with a game): press Play once online (public server)
    bool        privateServer = false; // --private-server (with a game): start a private server once online
    std::string testAccessory, testFace;   // --test-accessory file.json / --test-face face.png (tests)
    std::string testClothes;           // --test-clothes shirt.png,pants.png: wear these pictures (tests)
    std::string joinCode;              // --join-code <code>: join a private server once online
    std::string testSignup, testLogin; // --test-signup / --test-login "user:password" once online
    float       cameraYaw = -1000.0f;  // --camera-yaw <degrees> (tests: look from another side)
    std::string testRename;            // --test-rename "New name" (tests: rename your first game on the Create page)
    std::string testPublish;           // --test-publish file.gbscene (tests: publish a game file to the server)
    int         createTab = 0;         // --create-tab N (tests: which Create tab to open)
    std::string testTools;             // --test-tools "print 1 click 2 drop" (tests: one step every 25 frames in a game)
    std::string testClick;             // --test-click "x,y" (tests: click there 3 times, 0..1 of the window)
    std::string launchUrl;             // gutsandbolts://play/<game>?guest=boy (the website's Play button)
};

// Guts&BoltsPlayer: the platform app. Browse the games on this computer,
// dress up your avatar, and play — no editor in sight.
class PlayerApp {
public:
    explicit PlayerApp(PlayerOptions opts);
    ~PlayerApp();
    void run();

private:
    enum class Page { Home, Games, Avatar, GameInfo, Game, Catalog, Staff, Bolts, Create, People, Profile, Groups, Group, Friends, Login };
    // How a game is started: alone, or as the host of a server.
    enum class HostMode { Solo, Lan, Public, Private };
    using Starter = std::function<void(HostMode)>;   // loads the game (downloading it if needed) and starts it

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
    // The classic loading screen: the game's name, who made it and a spinner.
    void drawLoading(ImVec2 pos, ImVec2 size, float alpha, const char* status);
    // From pressing Play until the game shows: the connecting screen over everything.
    bool        m_connectScreen = false;
    GameGui::Input m_guiInput;                     // the game's own UI: which button is pointed at / pressed
    std::string m_loadingGameId;                   // server game id (for its icon), or ""
    std::string m_loadingIcon;                     // the icon file, once downloaded
    std::string m_loadingTitle, m_loadingAuthor;   // the game's name and creator on the server
    void startLoadingScreen(const std::string& gameId, const std::string& title);
    void drawConnectScreen();
    void drawPauseMenu();
    std::vector<PlayerEntry> currentPlayers() const;   // everyone in the game (or just you, offline)
    void drawCatalog();
    void drawItemDialog();
    void drawCreateItemDialog();
    void drawStaff();
    void drawBolts();

    // Online (a Guts&Bolts server) — PlayerOnline.cpp
    void drawServerButton(ImVec2 at);
    void drawNoServer();
    void fetchAvatarParts();                   // download worn accessories / faces, then put them on
    std::set<std::string> m_avatarFetching;    // asset ids already asked for
    void drawServerCards(const std::string& gameKey, const std::string& title);   // game page: who's playing where   // not connected: "Connecting..." / "Can't reach Guts&Bolts"
    bool testMode() const { return !m_opts.screenshot.empty() && m_opts.page != "noserver"; }   // automated tests may play offline
    void drawOnlineCatalog();
    void drawWardrobe();
    void drawModeration();                          // the ban screen and staff warnings (PlayerOnline.cpp)                            // Avatar page: what you own, click to wear (PlayerOnline.cpp)
    void wardrobeToggle(const Catalog::Item& it);
    bool itemOn(const Catalog::Item& it) const;   // worn, or (gear) equipped
    void toggleGear(const std::string& id, bool on);
    void drawOnlineItemDialog();
    void drawCreate();
    void drawMyGames();
    void myGameRow(const std::string& key, const std::string& title, const std::string& sub, unsigned thumbTex,
                   const std::string& cardId, const std::filesystem::path& path, const std::string& publishedId,
                   const std::function<void()>& play);
    void drawUploadForm(const std::string& kind);
    void drawMyUploads(const std::string& kind);
    static bool renameGameFile(const std::filesystem::path& path, const std::string& title, std::string& error);
    void renameGame(const std::filesystem::path& path, const std::string& publishedId, const std::string& title);
    // Put a game file from this computer on the server (so it's on the website for everyone), with
    // `picture` (a .png) for its card, and remember in the file that it's published.
    void publishGameFile(const std::filesystem::path& path, const std::string& picture,
                         const std::function<void(bool ok, const std::string& message)>& done);
    void openInStudio(const std::filesystem::path& path);
    void drawOnlineGames();
    void drawOnlineGameDialog();
    void drawOnlineBolts();
    void drawOnlineStaff();
    void refreshOnline(const std::string& what);   // "catalog", "games", "mine", "history"
    void onlinePlayTick(float dt);

    // People and groups — PlayerSocial.cpp
    void drawPeople();
    void drawProfile();
    // Someone's friends / following / followers (a popup over their profile).
    void openPeople(const std::string& user, const std::string& which, int page = 0);
    void drawPeopleDialog();
    std::string    m_peopleUser, m_peopleWhich;
    int            m_peoplePage = 0;
    long long      m_peopleTotal = 0;
    nlohmann::json m_people = nlohmann::json::array();
    std::string    m_peopleMsg;
    void drawGroups();
    void drawGroup();
    void openProfile(const std::string& accountId);
    void openGroup(const std::string& groupId);
    bool needsServer(const char* what);   // "you need a server" note; true if offline
    void drawAccount();
    void drawNotice();
    void updateTouch(ImVec2 min, ImVec2 max, bool acceptInput);

    // Sign up / log in — PlayerLogin.cpp
    bool needsLogin() const;
    void drawLogin();
    void signUp(const std::string& username, const std::string& password);
    void logIn(const std::string& username, const std::string& password);
    void logOut();

    // Friends and servers — PlayerFriends.cpp
    void drawFriends();
    void refreshFriends();
    void friendButton(const std::string& accountId, const std::string& status);   // Add / Accept / Friends
    void playGame(const std::string& gameKey, const std::string& title, Starter start);   // Play: a public server
    void openServers(const std::string& gameKey, const std::string& title, Starter start);
    void drawServersDialog();
    void joinRelay(const std::string& session, const std::string& code, const std::string& title);
    Starter localStarter(const std::filesystem::path& path);
    Starter onlineStarter(const std::string& assetId);
    void giveGear(const std::string& gameKey);   // your equipped catalog gear, in games that allow it

    void refreshGames();
    void joinGame(const std::filesystem::path& path, HostMode mode = HostMode::Solo, const std::string& gameKey = "");
    void joinServer(const std::string& address);
    void drawChat(ImVec2 min, ImVec2 max);
    void drawJoinDialog();
    ChatLog& chat();
    void sendChat(const std::string& text);
    void leaveGame();
    void buildAvatarStage();
    // The 3D avatar on someone's profile (their colours, hat and clothes).
    void buildProfileStage(const nlohmann::json& avatar, const nlohmann::json& wearing);

    PlayerOptions                  m_opts;
    std::unique_ptr<AppWindow>     m_window;
    std::unique_ptr<SceneRenderer> m_renderer;
    std::unique_ptr<Scene>         m_scene;        // the game being played
    std::unique_ptr<GameSession>   m_session;
    std::unique_ptr<Scene>         m_avatarScene;  // preview on the Avatar page
    std::unique_ptr<Scene>         m_profileScene; // the avatar on the Profile page
    std::string                    m_profileSceneFor;
    Camera                         m_profileCam;
    Framebuffer                    m_profileView;
    // "Choose Your Character" (guests): a boy and a girl to play as.
    bool                           m_charPickOpen = false;
    std::unique_ptr<Scene>         m_charScene[2];
    Framebuffer                    m_charView[2];
    Camera                         m_charCam;
    void drawCharacterPicker();
    void applyGuestLook(int which);    // 0 = boy (black cap), 1 = girl (ponytail)
    // A gutsandbolts:// link to act on once we're online (the website's Play button).
    std::string                    m_linkGame, m_linkGuest, m_linkServer;
    double                         m_linkPollAt = 0.0;
    void takeLink(const std::string& url);
    void followLink();

    Page        m_page = Page::Home;
    std::vector<GameCard> m_games;
    std::string m_search;
    std::string m_status;                          // "Couldn't load ..." etc.
    std::string m_currentTitle;
    std::string m_currentAuthor;                   // for the loading screen
    float       m_loadingT = 0.0f;                 // seconds of loading screen left (fades out)

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
    // The leaderboard: folded away or not (Tab), and the little menu you get by
    // clicking someone's name (Add Friend / Follow).
    bool        m_listOpen = true;
    std::string m_listMenu;          // whose menu is open ("" = none)
    ImVec2      m_listMenuAt{0, 0};
    nlohmann::json m_listRel;        // where you stand with them (friends.relation)
    std::string m_listMsg;           // "Friend request sent!" and such
    void        drawPlayerMenu();
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
    nlohmann::json m_onlineItems = nlohmann::json::array();   // server hats / shirts / pants
    nlohmann::json m_wardrobe = nlohmann::json::array();      // the Avatar page: items you own
    double      m_wardrobeAt = -100.0;                         // when it was last asked for
    int         m_avatarTab = 0;                               // 0 Wardrobe, 1 Body
    int         m_wardrobeKind = 0;                            // which kind of item it shows
    nlohmann::json m_onlineGames = nlohmann::json::array();
    nlohmann::json m_myCreations = nlohmann::json::array();
    nlohmann::json m_onlineHistory = nlohmann::json::array();
    nlohmann::json m_foundUsers = nlohmann::json::array();
    int            m_openOnlineItem = -1, m_openOnlineGame = -1;
    std::string    m_onlineMsg, m_createMsg, m_staffMsg, m_findQuery;
    std::string    m_banTarget, m_banTargetName, m_banNote;   // the "Ban account" popup
    int            m_banReason = 0;                           // index into Online::kBanReasons
    int            m_banDays = 4;                             // index into the ban lengths (last = forever)
    bool           m_warnMode = false;                        // the popup sends a warning, not a ban
    std::string    m_warnAcking;                              // the warning we said "I understand" to
    int            m_createKind = 0, m_createStyle = 2, m_createPrice = 0, m_giveServerBolts = 100;
    std::string    m_createName, m_createDesc, m_createPath;
    glm::vec3      m_createColor{0.9f, 0.2f, 0.2f};
    std::string    m_renameKey, m_renameText;      // My Games row being renamed
    bool           m_renameFocus = false;
    bool           m_gamesDirty = false;           // a game was renamed: re-read the games list
    std::set<std::string> m_askedDownloads;        // decal pictures already asked for
    std::string    m_testLastId;                   // --online-test: id of the last upload ("$LAST")
    double         m_avatarPushAt = 0.0;           // save the avatar on the server at this time (0 = nothing to save)
    bool           m_busy = false;                 // waiting on the server
    float          m_onlinePlaySeconds = 0.0f;
    std::string    m_loaded;                       // lists already fetched this visit ("catalog games ...")

    // People and groups
    std::string    m_peopleQuery, m_profileId, m_groupId, m_groupQuery, m_socialMsg;
    nlohmann::json m_peopleResults = nlohmann::json::array();
    nlohmann::json m_profile = nlohmann::json::object();
    nlohmann::json m_groupList = nlohmann::json::array();
    nlohmann::json m_myGroups = nlohmann::json::array();
    nlohmann::json m_group = nlohmann::json::object();
    int            m_groupsTab = 0;                // My groups / Browse / Create
    int            m_groupTab = 0;                 // a group page: Wall / Members / Admin
    std::string    m_newGroupName, m_newGroupDesc, m_wallInput, m_shoutInput, m_editDesc;
    glm::vec3      m_newGroupColor{0.23f, 0.48f, 0.84f};
    bool           m_newGroupOpen = true, m_editingGroup = false;

    // Sign up / log in
    int            m_loginTab = 0;                 // Sign Up / Log In
    bool           m_signupAgree = false;          // "I'm 18+ and agree to the Terms" (sign up)
    bool           m_showTerms = false;            // the Terms of Service window
    std::string    m_loginUser, m_loginPass, m_loginPass2, m_loginMsg;
    std::string    m_loginCode;                    // two-step verification: the code from your email
    bool           m_loginNeedCode = false;
    bool           m_loginAppCode = false;         // ...from an authenticator app (not email)
    nlohmann::json m_nameCheck = nlohmann::json::object();   // is the typed username free?
    double         m_nameCheckAt = 0.0;

    // Friends and servers
    nlohmann::json m_friends = nlohmann::json::object();   // friends.list reply
    double         m_friendsAt = -100.0;           // when we last asked
    int            m_friendsTab = 0;               // Friends / Requests / Add friends
    std::string    m_friendQuery, m_friendMsg;
    nlohmann::json m_friendSearch = nlohmann::json::array();
    bool           m_serversOpen = false;
    std::string    m_serversKey, m_serversTitle, m_serversMsg, m_codeInput;
    Starter        m_serversStart;
    nlohmann::json m_serverList = nlohmann::json::array();
    // The game page's server cards (Roblox-style): whose game, when fetched, which page.
    nlohmann::json m_gameServers = nlohmann::json::array();
    std::string    m_gameServersKey;
    double         m_gameServersAt = -100.0;
    int            m_gameServersPage = 0;
    std::string    m_playMsg;                      // "Finding a server..."
    bool           m_joinedOnce = false;           // fetched the game's sounds after joining
    bool           m_autoStarted = false;          // test options that wait for the server
    bool           m_autoServers = false;

    TouchControls m_touch;
    ImVec2      m_chatMin{0, 0}, m_chatMax{0, 0};  // where the chat box was last frame
    size_t      m_chatSeen = 0;                    // phones: chat pops up briefly for new messages
    double      m_chatShowUntil = 0.0;
    void        touchScroll();

    bool        m_paused = false;
    bool        m_showSettings = false;
    bool        m_shiftLock = false;   // Roblox Shift Lock (Shift toggles it)
    int         m_menuTab = 0;         // in-game menu: 0 Players, 1 Settings, 2 Help
    int         m_menuConfirm = 0;     // 1 = "Reset character?", 2 = "Leave game?"
    int         m_frame = 0;
};
