#include "AppWindow.h"
#include "Settings.h"
#include "Audio.h"
#include "../editor/Theme.h"
#include "../renderer/MeshLibrary.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

AppWindow::AppWindow(const char* title, int width, int height, const char* layoutFile) {
    GraphicsSettings::get().load();

    if (!glfwInit())
        throw std::runtime_error("Failed to initialise GLFW");

    // OpenGL 4.1 core: the newest version every desktop OS (including macOS) supports.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);   // required on macOS

    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window)
        throw std::runtime_error("Failed to create a window. Your graphics driver needs OpenGL 4.1 or newer.");

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

#ifdef _WIN32
    // Use the embedded application icon (resource id 1) for the title bar and
    // taskbar, so it matches the .exe icon shown in Explorer.
    if (HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                        MAKEINTRESOURCEW(1), IMAGE_ICON,
                                        0, 0, LR_DEFAULTSIZE | LR_SHARED)) {
        HWND hwnd = glfwGetWin32Window(m_window);
        SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
#endif

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
        throw std::runtime_error("Failed to initialise GLEW");
    glGetError();   // GLEW can leave a harmless error behind on core profiles
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = layoutFile;

    EditorTheme::loadFonts();
    EditorTheme::apply();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 410");

    m_lastTime = m_nextFrame = glfwGetTime();
    Audio::init();
}

AppWindow::~AppWindow() {
    Audio::shutdown();
    // GPU resources must be released while the GL context still exists.
    MeshLibrary::clear();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool AppWindow::shouldClose() const { return glfwWindowShouldClose(m_window); }
void AppWindow::close() { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }
void AppWindow::setTitle(const std::string& t) { glfwSetWindowTitle(m_window, t.c_str()); }

float AppWindow::beginFrame(const std::function<void()>& beforeImGui) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    int wantVsync = (gs.vsync && !m_fixedDt) ? 1 : 0;
    if (wantVsync != m_appliedVsync) { glfwSwapInterval(wantVsync); m_appliedVsync = wantVsync; }

    double now = glfwGetTime();
    float dt = m_fixedDt ? 1.0f / 60.0f : (float)(now - m_lastTime);
    m_lastTime = now;

    glfwPollEvents();
    Audio::update();

    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    if (beforeImGui) beforeImGui();
    ImGui::NewFrame();
    return dt;
}

void AppWindow::endFrame(const std::string& screenshotPath) {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!screenshotPath.empty()) saveScreenshot(screenshotPath);
    glfwSwapBuffers(m_window);

    // FPS limit (only when VSync is off).
    const GraphicsSettings& gs = GraphicsSettings::get();
    if (!gs.vsync && gs.fpsCap > 0 && !m_fixedDt) {
        double step = 1.0 / gs.fpsCap;
        m_nextFrame += step;
        double now = glfwGetTime();
        if (m_nextFrame > now) {
            double wait = m_nextFrame - now;
            // Sleep most of the wait, then spin for accuracy.
            if (wait > 0.002)
                std::this_thread::sleep_for(std::chrono::duration<double>(wait - 0.001));
            while (glfwGetTime() < m_nextFrame) {}
        } else if (now - m_nextFrame > step * 4) {
            m_nextFrame = now;   // fell far behind; don't try to catch up
        }
    } else {
        m_nextFrame = glfwGetTime();
    }
}

void AppWindow::saveScreenshot(const std::string& path) {
    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    std::vector<unsigned char> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) std::fwrite(&px[(size_t)y * w * 3], 1, (size_t)w * 3, f);
        std::fclose(f);
    }
}
