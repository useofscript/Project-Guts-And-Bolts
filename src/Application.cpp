#include "Application.h"
#include "scene/Scene.h"
#include "editor/Editor.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <stdexcept>

Application::Application() {
    initWindow();
    initGL();
    initImGui();
    m_scene  = std::make_unique<Scene>();
    m_editor = std::make_unique<Editor>(m_window, m_scene.get());
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

    m_window = glfwCreateWindow(m_width, m_height, "GameEngine", nullptr, nullptr);
    if (!m_window)
        throw std::runtime_error("Failed to create window");

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
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

    ImGui::StyleColorsDark();
    // Slightly refined dark theme
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding   = 4.0f;
    s.FrameRounding    = 3.0f;
    s.GrabRounding     = 3.0f;
    s.WindowBorderSize = 1.0f;
    s.Colors[ImGuiCol_WindowBg]       = {0.13f, 0.14f, 0.15f, 1.0f};
    s.Colors[ImGuiCol_Header]         = {0.20f, 0.22f, 0.27f, 1.0f};
    s.Colors[ImGuiCol_HeaderHovered]  = {0.26f, 0.59f, 0.98f, 0.80f};
    s.Colors[ImGuiCol_HeaderActive]   = {0.26f, 0.59f, 0.98f, 1.00f};

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 450");
}

void Application::run() {
    float lastTime = (float)glfwGetTime();
    while (!glfwWindowShouldClose(m_window)) {
        float now = (float)glfwGetTime();
        float dt  = now - lastTime;
        lastTime  = now;

        glfwPollEvents();

        int w, h;
        glfwGetFramebufferSize(m_window, &w, &h);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        m_editor->render(dt);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);
    }
}

void Application::cleanup() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}
