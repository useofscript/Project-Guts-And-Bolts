#pragma once
#include <cstdint>
#include <string>

class Scene;

// A simple code editor for Script objects, with live error checking and a menu
// of ready-to-use code snippets for beginners.
class ScriptEditorPanel {
public:
    explicit ScriptEditorPanel(Scene* scene);
    void open(uint64_t scriptId);   // show this script and bring the tab forward
    void render();
    uint64_t current() const { return m_id; }

private:
    Scene*      m_scene;
    uint64_t    m_id = 0;
    bool        m_focus = false;
    std::string m_checked;          // source text the error below belongs to
    std::string m_error;
    int         m_errorLine = 0;
    int         m_cursorLine = 1;
};
