#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <imgui.h>

class Scene;
struct ImGuiInputTextCallbackData;

// A simple code editor for Script objects, with live error checking, a menu
// of ready-to-use code snippets for beginners, find / replace (Ctrl+F /
// Ctrl+H), find in every script (Ctrl+Shift+F) and autocomplete (Tab).
class ScriptEditorPanel {
public:
    explicit ScriptEditorPanel(Scene* scene);
    void open(uint64_t scriptId);   // show this script and bring the tab forward
    void render();
    void renderFindAll();           // the "Find in All Scripts" window
    uint64_t current() const { return m_id; }
    void showFindAll() { m_showFindAll = true; m_focusFindAll = true; }
    void renderAnalysis();          // the "Script Analysis" window (ScriptAnalysis.cpp)
    void showAnalysis() { m_showAnalysis = true; m_analysedAt = -100.0; }
    bool analysisShown() const { return m_showAnalysis; }
    void runAnalysis();
    struct Issue { uint64_t script; std::string name; int line; bool error; std::string text; };
    const std::vector<Issue>& issues() const { return m_issues; }

private:
    static int onEdit(ImGuiInputTextCallbackData* d);
    void findNext(const std::string& text, bool backwards);
    void updateSuggestions(const std::string& source);
    void runFindAll();

    Scene*      m_scene;
    uint64_t    m_id = 0;
    bool        m_focus = false;
    std::string m_checked;          // source text the error below belongs to
    std::string m_error;
    int         m_errorLine = 0;
    int         m_cursorLine = 1;
    int         m_cursor = 0;       // byte position of the text cursor

    // Find / replace
    bool        m_showFind = false, m_showReplace = false, m_focusFind = false, m_matchCase = false;
    std::string m_find, m_replace, m_findStatus;
    int         m_selectStart = -1, m_selectEnd = -1;   // ask the editor to select this next frame
    bool        m_focusCode = false;
    int         m_matchAt = -1;                         // where the last "Find" landed

    // Autocomplete
    std::vector<std::string> m_suggest;
    std::string m_prefix;
    ImVec2      m_caret{0, 0};      // where the text cursor is on screen
    bool        m_codeActive = false;
    int         m_pick = 0;

    // Find in all scripts
    struct Hit { uint64_t script; std::string name; int line; int pos; std::string text; };
    bool        m_showFindAll = false, m_focusFindAll = false;
    std::string m_findAll, m_findAllRan;
    std::vector<Hit> m_hits;

    // Script Analysis
    bool               m_showAnalysis = false;
    double             m_analysedAt = -100.0;
    std::vector<Issue> m_issues;
};
