#pragma once
#include <memory>
#include <string>

struct GLFWwindow;
class Scene;
class Editor;

// Command-line options (mostly for testing and launching games directly).
struct LaunchOptions {
    std::string openFile;          // --open <file>
    bool        play = false;      // --play            start in Play mode
    std::string screenshot;        // --screenshot <out.ppm>  (then quit)
    int         frames = 120;      // --frames <n>      when to take it
    std::string holdKey;           // --hold <key>      hold a key during Play (W, A, S, D, Space)
};

class Application {
public:
    explicit Application(LaunchOptions opts = {});
    ~Application();
    void run();

private:
    void initWindow();
    void initGL();
    void initImGui();
    void cleanup();

    void saveScreenshot(const std::string& path);

    LaunchOptions m_opts;
    GLFWwindow* m_window = nullptr;
    int m_width = 1280, m_height = 720;

    std::unique_ptr<Scene>  m_scene;
    std::unique_ptr<Editor> m_editor;
};
