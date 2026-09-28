#pragma once
#include <functional>
#include <string>

struct GLFWwindow;

// Window + OpenGL + Dear ImGui setup and the per-frame loop helpers, shared by
// the editor and the Player app. Also applies the VSync / FPS-limit settings.
class AppWindow {
public:
    AppWindow(const char* title, int width, int height, const char* layoutFile);
    ~AppWindow();

    GLFWwindow* handle() const { return m_window; }
    bool shouldClose() const;
    void close();
    void setTitle(const std::string& title);

    // Start a frame: poll input, start ImGui. `beforeImGui` runs right before
    // ImGui::NewFrame (used to inject test input). Returns seconds since last frame.
    float beginFrame(const std::function<void()>& beforeImGui = nullptr);
    // Finish a frame: draw ImGui, show it, and wait if an FPS limit is set.
    // `screenshotPath` (optional) saves this frame as an image first.
    void endFrame(const std::string& screenshotPath = {});

    void setFixedTimestep(bool on) { m_fixedDt = on; }   // deterministic test runs
    void saveScreenshot(const std::string& path);         // binary PPM

private:
    GLFWwindow* m_window = nullptr;
    double      m_lastTime = 0.0;
    double      m_nextFrame = 0.0;
    int         m_appliedVsync = -1;
    bool        m_fixedDt = false;
};
