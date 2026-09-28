#include "Application.h"
#include "core/AppWindow.h"
#include "scene/Scene.h"
#include "editor/Editor.h"

#include <imgui.h>
#include <sstream>
#include <vector>

namespace {
// "ctrl+shift+left" -> modifiers + key (test helper).
struct Chord { bool ctrl = false, shift = false; ImGuiKey key = ImGuiKey_None; };
Chord parseChord(const std::string& text) {
    Chord c;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, '+')) {
        if (part == "ctrl") c.ctrl = true;
        else if (part == "shift") c.shift = true;
        else if (part == "left") c.key = ImGuiKey_LeftArrow;
        else if (part == "right") c.key = ImGuiKey_RightArrow;
        else if (part == "up") c.key = ImGuiKey_UpArrow;
        else if (part == "down") c.key = ImGuiKey_DownArrow;
        else if (part == "del") c.key = ImGuiKey_Delete;
        else if (part == "esc") c.key = ImGuiKey_Escape;
        else if (part == "enter") c.key = ImGuiKey_Enter;
        else if (part.size() >= 2 && part[0] == 'f') c.key = (ImGuiKey)(ImGuiKey_F1 + std::stoi(part.substr(1)) - 1);
        else if (part.size() == 1 && part[0] >= 'a' && part[0] <= 'z') c.key = (ImGuiKey)(ImGuiKey_A + (part[0] - 'a'));
    }
    return c;
}
} // namespace

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
    std::vector<Chord> chords;
    { std::stringstream ss(m_opts.testKeys); std::string w; while (ss >> w) chords.push_back(parseChord(w)); }
    while (!m_window->shouldClose()) {
        ++frame;
        float dt = m_window->beginFrame([&] {
            // Test helper: hold a key down while playing.
            if (!m_opts.holdKey.empty() && frame > 3) {
                ImGuiKey k = m_opts.holdKey == "Space" ? ImGuiKey_Space
                           : (ImGuiKey)(ImGuiKey_A + (m_opts.holdKey[0] - 'A'));
                ImGui::GetIO().AddKeyEvent(k, true);
            }
            // Test helper: press one chord every 6 frames, starting at frame 20.
            int step = frame - 20;
            if (step >= 0 && step % 6 < 2 && step / 6 < (int)chords.size()) {
                const Chord& c = chords[step / 6];
                bool down = step % 6 == 0;
                ImGuiIO& io = ImGui::GetIO();
                io.AddKeyEvent(ImGuiMod_Ctrl, down && c.ctrl);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, down && c.ctrl);
                io.AddKeyEvent(ImGuiMod_Shift, down && c.shift);
                io.AddKeyEvent(ImGuiKey_LeftShift, down && c.shift);
                io.AddKeyEvent(c.key, down);
            }
        });
        if (m_opts.play && frame == 3) m_editor->togglePlay();
        if (m_opts.teamHost && frame == 2) m_editor->startTeamCreate(true, "");
        if (!m_opts.teamJoin.empty() && frame == 2) m_editor->startTeamCreate(false, m_opts.teamJoin);
        if (!m_opts.testAddPart.empty() && frame == 60) m_editor->testAddPart(m_opts.testAddPart);
        if (!m_opts.testPremades.empty() && frame == 2) m_editor->testPremades(m_opts.testPremades);
        if (!m_opts.testSelect.empty() && frame == 10) m_editor->testSelect(m_opts.testSelect);

        m_editor->render(dt);

        bool shoot = !m_opts.screenshot.empty() && frame == m_opts.frames;
        m_window->endFrame(shoot ? m_opts.screenshot : std::string());
        if (shoot) m_window->close();
    }
}
