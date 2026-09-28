#include "Application.h"
#include "core/AppWindow.h"
#include "scene/Scene.h"
#include "editor/Editor.h"

#include <imgui.h>

Application::Application(LaunchOptions opts) : m_opts(std::move(opts)) {
    m_window = std::make_unique<AppWindow>("Guts and Bolts", 1280, 720, "editor_layout.ini");
    m_window->setFixedTimestep(!m_opts.screenshot.empty());
    m_scene  = std::make_unique<Scene>();
    m_editor = std::make_unique<Editor>(m_window->handle(), m_scene.get());
    if (!m_opts.openFile.empty()) m_editor->openFile(m_opts.openFile);
}

Application::~Application() {
    m_editor.reset();
    m_scene.reset();
    m_window.reset();
}

void Application::run() {
    int frame = 0;
    while (!m_window->shouldClose()) {
        ++frame;
        float dt = m_window->beginFrame([&] {
            // Test helper: hold a key down while playing.
            if (!m_opts.holdKey.empty() && frame > 3) {
                ImGuiKey k = m_opts.holdKey == "Space" ? ImGuiKey_Space
                           : (ImGuiKey)(ImGuiKey_A + (m_opts.holdKey[0] - 'A'));
                ImGui::GetIO().AddKeyEvent(k, true);
            }
        });
        if (m_opts.play && frame == 3) m_editor->togglePlay();

        m_editor->render(dt);

        bool shoot = !m_opts.screenshot.empty() && frame == m_opts.frames;
        m_window->endFrame(shoot ? m_opts.screenshot : std::string());
        if (shoot) m_window->close();
    }
}
