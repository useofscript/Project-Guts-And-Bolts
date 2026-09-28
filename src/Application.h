#pragma once
#include <memory>
#include <string>

class AppWindow;
class Scene;
class Editor;

// Command-line options (mostly for testing and launching games directly).
struct LaunchOptions {
    std::string openFile;          // --open <file>
    bool        play = false;      // --play            start in Play mode
    std::string screenshot;        // --screenshot <out.ppm>  (then quit)
    int         frames = 120;      // --frames <n>      when to take it
    std::string holdKey;           // --hold <key>      hold a key during Play (W, A, S, D, Space)
    bool        teamHost = false;  // --team-host       start Team Create
    std::string teamJoin;          // --team-join <addr>
    std::string testAddPart;       // --test-add-part <name>  (tests: add + select a part)
    std::string testPremades;      // --test-premades <comma list or "all">  (tests)
};

// The editor application ("Guts and Bolts Studio").
class Application {
public:
    explicit Application(LaunchOptions opts = {});
    ~Application();
    void run();

private:
    LaunchOptions m_opts;
    // Declared first so it is destroyed last (after everything using OpenGL).
    std::unique_ptr<AppWindow> m_window;
    std::unique_ptr<Scene>     m_scene;
    std::unique_ptr<Editor>    m_editor;
};
