#include "ScriptEditorPanel.h"
#include "../Premades.h"
#include "../Theme.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../scripting/ScriptEngine.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>

ScriptEditorPanel::ScriptEditorPanel(Scene* scene) : m_scene(scene) {}

void ScriptEditorPanel::open(uint64_t scriptId) {
    m_id = scriptId;
    m_focus = true;
}

void ScriptEditorPanel::render() {
    if (m_focus) { ImGui::SetNextWindowFocus(); m_focus = false; }
    ImGui::Begin("Script Editor");

    SceneNode* s = m_id ? m_scene->findById(m_id) : nullptr;
    if (s && !s->isScript()) s = nullptr;
    // Follow the selection when a different script is picked.
    if (SceneNode* sel = m_scene->selected(); sel && sel->isScript() && sel != s) {
        s = sel;
        m_id = sel->id;
    }

    if (!s) {
        ImGui::Spacing();
        ImGui::TextWrapped("No script open.");
        ImGui::Spacing();
        ImGui::TextDisabled("To write code:");
        ImGui::BulletText("Select a part, then click  + Script  in the Toolbox\n(or Add > Script in the menu).");
        ImGui::BulletText("Double-click any Script in the Explorer to open it here.");
        ImGui::BulletText("Press Play (F5) to run your scripts.");
        ImGui::End();
        return;
    }

    // --- Header: which script, enabled toggle, snippets ---
    ImGui::TextDisabled("Editing");
    ImGui::SameLine();
    ImGui::TextUnformatted(s->fullName().c_str());
    ImGui::SameLine();
    ImGui::Checkbox("Enabled", &s->scriptEnabled);
    ImGui::SameLine();
    if (ImGui::Button("Insert code..."))
        ImGui::OpenPopup("##snippets");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add a ready-made piece of code to the end of this script");
    if (ImGui::BeginPopup("##snippets")) {
        ImGui::TextDisabled("Adds to the end of the script");
        ImGui::Separator();
        for (const Snippet& sn : snippetList()) {
            if (ImGui::MenuItem(sn.name)) {
                if (!s->source.empty() && s->source.back() != '\n') s->source += '\n';
                if (!s->source.empty()) s->source += '\n';
                s->source += sn.code;
            }
        }
        ImGui::EndPopup();
    }

    // --- Code area ---
    float footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
    ImGui::PushFont(EditorTheme::codeFont());
    auto cb = [](ImGuiInputTextCallbackData* d) -> int {
        int* line = static_cast<int*>(d->UserData);
        *line = 1 + (int)std::count(d->Buf, d->Buf + d->CursorPos, '\n');
        return 0;
    };
    ImGui::InputTextMultiline("##code", &s->source, ImVec2(-1, -footer),
                              ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
                              cb, &m_cursorLine);
    ImGui::PopFont();

    // --- Live syntax check (only re-run when the text changes) ---
    if (s->source != m_checked) {
        m_checked = s->source;
        m_error.clear();
        ScriptEngine::checkSyntax(s->source, m_error, m_errorLine);
    }
    if (m_error.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1), "No mistakes found");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.4f, 1), "Line %d: %s", m_errorLine, m_error.c_str());
    }
    ImGui::SameLine();
    char pos[32];
    std::snprintf(pos, sizeof(pos), "Ln %d", m_cursorLine);
    float w = ImGui::CalcTextSize(pos).x;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - w);
    ImGui::TextDisabled("%s", pos);

    ImGui::End();
}
