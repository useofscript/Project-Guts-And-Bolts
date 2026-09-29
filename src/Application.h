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
    std::string testKeys;          // --test-keys "ctrl+a ctrl+g f2"  (tests: press these one by one)
    std::string testSelect;        // --test-select "Name1,Name2"  (tests)
    std::string exportRoblox;      // --export-roblox <file.rbxlx> (tests)
    std::string testSnapshot;      // --test-snapshot <file.png> (tests: the picture Publish sends)
    std::string testMesh;          // --test-mesh "enter,face,top,extrude"  (tests: Modeling-mode steps)
    std::string testMouse;         // --test-mouse "click:x:y shift:x:y drag:x1:y1:x2:y2"  (tests, from frame 60)
    std::string testCommand;       // --test-command "<lua>"  (tests: run it in the Command Bar)
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
