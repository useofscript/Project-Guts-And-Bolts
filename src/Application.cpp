#include "Application.h"
#include "scene/Scene.h"
#include "editor/Editor.h"
#include "editor/Theme.h"
#include "renderer/MeshLibrary.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <stdexcept>
#include <cstdio>
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

Application::Application(LaunchOptions opts) : m_opts(std::move(opts)) {
    initWindow();
    initGL();
    initImGui();
    m_scene  = std::make_unique<Scene>();
    m_editor = std::make_unique<Editor>(m_window, m_scene.get());
    if (!m_opts.openFile.empty()) m_editor->openFile(m_opts.openFile);
}

Application::~Application() {
    cleanup();
}

void Application::initWindow() {
    if (!glfwInit())
        throw std::runtime_error("Failed to initialise GLFW");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(m_width, m_height, "Guts and Bolts", nullptr, nullptr);
    if (!m_window)
        throw std::runtime_error("Failed to create window");

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
}

void Application::initGL() {
    if (glewInit() != GLEW_OK)
        throw std::runtime_error("Failed to initialise GLEW");
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Application::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "editor_layout.ini";

    EditorTheme::loadFonts();
    EditorTheme::apply();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 450");
}

void Application::saveScreenshot(const std::string& path) {
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

void Application::run() {
    float lastTime = (float)glfwGetTime();
    int frame = 0;
    while (!glfwWindowShouldClose(m_window)) {
        float now = (float)glfwGetTime();
        float dt  = now - lastTime;
        lastTime  = now;
        ++frame;
        if (!m_opts.screenshot.empty()) dt = 1.0f / 60.0f;   // deterministic test runs

        glfwPollEvents();
        if (m_opts.play && frame == 3) m_editor->togglePlay();

        int w, h;
        glfwGetFramebufferSize(m_window, &w, &h);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        if (!m_opts.holdKey.empty() && frame > 3) {
            ImGuiKey k = m_opts.holdKey == "Space" ? ImGuiKey_Space
                       : (ImGuiKey)(ImGuiKey_A + (m_opts.holdKey[0] - 'A'));
            ImGui::GetIO().AddKeyEvent(k, true);
        }
        ImGui::NewFrame();

        m_editor->render(dt);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (!m_opts.screenshot.empty() && frame == m_opts.frames) {
            saveScreenshot(m_opts.screenshot);
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
        glfwSwapBuffers(m_window);
    }
}

void Application::cleanup() {
    // GPU resources must be released while the GL context still exists.
    m_editor.reset();
    m_scene.reset();
    MeshLibrary::clear();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}
