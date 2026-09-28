#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "../renderer/Camera.h"
#include "../renderer/Framebuffer.h"
#include "../scene/Scene.h"

class AppWindow;
class SceneRenderer;
class GameSession;

struct PlayerOptions {
    std::string game;              // join this game file straight away
    std::string page;              // start on "avatar" / "settings"
    std::string screenshot;        // --screenshot <out.ppm> (tests)
    int         frames = 120;
    std::string holdKey;
};

// Guts&BoltsPlayer: the platform app. Browse the games on this computer,
// dress up your avatar, and play — no editor in sight.
class PlayerApp {
public:
    explicit PlayerApp(PlayerOptions opts);
    ~PlayerApp();
    void run();

private:
    enum class Page { Home, Avatar, Game };

    struct GameCard {
        std::filesystem::path        path;
        GameInfo                     info;
        std::unique_ptr<Framebuffer> thumb;
        bool                         broken = false;
    };

    void frame(float dt);
    void drawTopBar();
    void drawHome();
    void drawAvatar(float dt);
    void drawGame(float dt);
    void drawPauseMenu();

    void refreshGames();
    void joinGame(const std::filesystem::path& path);
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

    bool        m_paused = false;
    bool        m_showSettings = false;
    int         m_frame = 0;
};
