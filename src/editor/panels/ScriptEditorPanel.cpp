#include "ScriptEditorPanel.h"
#include "../Premades.h"
#include "../Theme.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../scripting/ScriptEngine.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <set>

namespace {

// Words autocomplete knows: Lua itself plus the Guts&Bolts / Roblox-style API.
const char* const kWords[] = {
    // Lua
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local", "nil", "not",
    "or", "repeat", "return", "then", "true", "until", "while", "continue",
    "print", "warn", "error", "pairs", "ipairs", "tostring", "tonumber", "type", "pcall", "select", "unpack",
    "require", "math", "string", "table", "os", "coroutine",
    "math.random", "math.floor", "math.ceil", "math.abs", "math.sin", "math.cos", "math.sqrt", "math.clamp",
    "math.min", "math.max", "math.huge", "math.pi", "math.round", "math.lerp",
    "string.format", "string.sub", "string.len", "string.upper", "string.lower", "string.rep", "string.split",
    "table.insert", "table.remove", "table.find", "table.sort", "table.concat",
    // Globals
    "workspace", "game", "script", "Instance.new", "Vector3.new", "Color3.new", "Color3.fromRGB", "Color3.fromHSV",
    "CFrame.new", "CFrame.Angles", "CFrame.lookAt", "task.wait", "task.spawn", "task.delay", "wait", "spawn", "delay",
    "time", "tick", "Explode", "Effects", "Sounds", "Gui", "Lighting", "Enum",
    "Players", "RunService", "UserInputService", "CollectionService",
    "game:GetService", "Players.LocalPlayer", "RunService.Heartbeat", "UserInputService.InputBegan",
    // Members
    "Parent", "Name", "ClassName", "Position", "Orientation", "Size", "CFrame", "Color", "Transparency", "Material",
    "Anchored", "CanCollide", "Velocity", "AssemblyLinearVelocity", "AssemblyAngularVelocity", "Shape",
    "Touched", "Clicked", "Heartbeat", "Died", "Humanoid", "Health", "MaxHealth", "WalkSpeed", "JumpPower", "Character",
    "Connect", "Once", "Wait", "Disconnect", "FindFirstChild", "FindFirstChildOfClass", "WaitForChild", "GetChildren",
    "GetDescendants", "Destroy", "Clone", "IsA", "IsDescendantOf", "GetFullName", "GetPivot", "PivotTo",
    "ApplyImpulse", "ApplyAngularImpulse", "TakeDamage", "BreakJoints", "Play", "Stop",
    "GetAttribute", "SetAttribute", "GetAttributes", "GetAttributeChangedSignal", "AttributeChanged",
    "AddTag", "RemoveTag", "HasTag", "GetTags", "GetTagged", "GetInstanceAddedSignal",
    "ClockTime", "Brightness", "FogEnabled", "FogColor", "Ambient",
    "Gui.Label", "Gui.Message", "Effects.Blood", "Effects.Oil", "Effects.Gibs", "Effects.Sparks", "Sounds.Play",
    // Game UI
    "UDim2.new", "UDim2.fromScale", "UDim2.fromOffset", "UDim.new", "Vector2.new", "StarterGui", "PlayerGui",
    "ScreenGui", "Frame", "TextLabel", "TextButton", "ImageLabel", "ImageButton", "UICorner", "UIStroke",
    "AnchorPoint", "BackgroundColor3", "BackgroundTransparency", "BorderSizePixel", "BorderColor3", "ZIndex",
    "Visible", "Text", "TextColor3", "TextSize", "TextScaled", "TextWrapped", "TextXAlignment", "TextYAlignment",
    "TextTransparency", "TextStrokeTransparency", "TextStrokeColor3", "Image", "ImageColor3", "ImageTransparency",
    "AutoButtonColor", "ClipsDescendants", "CornerRadius", "Thickness", "AbsoluteSize", "AbsolutePosition",
    "MouseButton1Click", "Activated", "MouseEnter", "MouseLeave", "DisplayOrder", "Enabled",
};

bool wordChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

size_t findText(const std::string& hay, const std::string& needle, size_t from, bool matchCase) {
    if (needle.empty()) return std::string::npos;
    if (matchCase) return hay.find(needle, from);
    return lower(hay).find(lower(needle), from);
}
size_t findTextBack(const std::string& hay, const std::string& needle, size_t before, bool matchCase) {
    if (needle.empty() || before == 0) return std::string::npos;
    return matchCase ? hay.rfind(needle, before - 1) : lower(hay).rfind(lower(needle), before - 1);
}

} // namespace

ScriptEditorPanel::ScriptEditorPanel(Scene* scene) : m_scene(scene) {}

void ScriptEditorPanel::open(uint64_t scriptId) {
    m_id = scriptId;
    m_focus = true;
}

// Called by the text box every frame while it's active (and when Tab completes a word).
int ScriptEditorPanel::onEdit(ImGuiInputTextCallbackData* d) {
    auto* self = static_cast<ScriptEditorPanel*>(d->UserData);
    if (d->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        // Tab: finish the word with the picked suggestion.
        if (!self->m_suggest.empty()) {
            const std::string& word = self->m_suggest[std::clamp(self->m_pick, 0, (int)self->m_suggest.size() - 1)];
            int start = d->CursorPos;
            while (start > 0 && (wordChar(d->Buf[start - 1]) || d->Buf[start - 1] == '.')) --start;
            d->DeleteChars(start, d->CursorPos - start);
            d->InsertChars(start, word.c_str());
            self->m_suggest.clear();
        }
        return 0;
    }
    // Somebody asked us to select something (Find).
    if (self->m_selectStart >= 0) {
        int len = d->BufTextLen;
        d->SelectionStart = std::clamp(self->m_selectStart, 0, len);
        d->SelectionEnd = std::clamp(self->m_selectEnd, 0, len);
        d->CursorPos = d->SelectionEnd;
        self->m_selectStart = self->m_selectEnd = -1;
    }
    self->m_cursor = d->CursorPos;
    self->m_cursorLine = 1 + (int)std::count(d->Buf, d->Buf + d->CursorPos, '\n');
    // Where the cursor is on screen (the text box's own scrolling window is the current one here).
    int lineStart = d->CursorPos;
    while (lineStart > 0 && d->Buf[lineStart - 1] != '\n') --lineStart;
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    float x = ImGui::CalcTextSize(d->Buf + lineStart, d->Buf + d->CursorPos).x;
    float y = (self->m_cursorLine - 1) * ImGui::GetTextLineHeight();
    self->m_caret = ImVec2(w->Pos.x + ImGui::GetStyle().FramePadding.x + x - w->Scroll.x,
                           w->Pos.y + ImGui::GetStyle().FramePadding.y + y - w->Scroll.y);
    // The word being typed.
    int start = d->CursorPos;
    while (start > 0 && (wordChar(d->Buf[start - 1]) || d->Buf[start - 1] == '.')) --start;
    self->m_prefix.assign(d->Buf + start, d->Buf + d->CursorPos);   // e.g. "math.fl" or "part.Pos"
    return 0;
}

void ScriptEditorPanel::updateSuggestions(const std::string& source) {
    m_suggest.clear();
    std::string p = m_prefix;
    // "part.Pos" -> suggest members for "Pos"; "math.fl" -> "math.floor".
    std::string member = p;
    if (auto dot = p.find_last_of('.'); dot != std::string::npos) member = p.substr(dot + 1);
    if (member.size() < 2 && p.size() < 3) return;
    std::set<std::string> seen;
    auto offer = [&](const std::string& word) {
        if (m_suggest.size() >= 8 || seen.count(word)) return;
        std::string lw = lower(word);
        // A dotted word ("math.floor") matches the whole prefix; a plain word matches the part after the dot.
        bool ok = word.find('.') != std::string::npos ? lw.rfind(lower(p), 0) == 0 && lw != lower(p)
                                                      : lw.rfind(lower(member), 0) == 0 && lw != lower(member);
        if (!ok) return;
        seen.insert(word);
        // Complete the part after the dot for members.
        m_suggest.push_back(word.find('.') == std::string::npos && p.find('.') != std::string::npos
                                ? p.substr(0, p.find_last_of('.') + 1) + word : word);
    };
    for (const char* w : kWords) offer(w);
    // Names used in this script (locals, functions...).
    std::string word;
    for (size_t i = 0; i <= source.size() && m_suggest.size() < 8; ++i) {
        char c = i < source.size() ? source[i] : ' ';
        if (wordChar(c)) { word += c; continue; }
        if (word.size() >= 3 && !std::isdigit((unsigned char)word[0])) offer(word);
        word.clear();
    }
    m_pick = std::clamp(m_pick, 0, std::max(0, (int)m_suggest.size() - 1));
}

void ScriptEditorPanel::findNext(const std::string& source, bool backwards) {
    if (m_find.empty()) return;
    size_t at;
    if (backwards) {
        size_t from = m_matchAt >= 0 ? (size_t)m_matchAt : (size_t)m_cursor;
        at = findTextBack(source, m_find, from, m_matchCase);
        if (at == std::string::npos) at = findTextBack(source, m_find, source.size() + 1, m_matchCase);   // wrap
    } else {
        size_t from = m_matchAt >= 0 ? (size_t)m_matchAt + 1 : (size_t)m_cursor;
        at = findText(source, m_find, from, m_matchCase);
        if (at == std::string::npos) at = findText(source, m_find, 0, m_matchCase);   // wrap
    }
    if (at == std::string::npos) { m_findStatus = "Not found"; m_matchAt = -1; return; }
    m_matchAt = (int)at;
    m_selectStart = (int)at;
    m_selectEnd = (int)(at + m_find.size());
    m_focusCode = true;
    int line = 1 + (int)std::count(source.begin(), source.begin() + (long)at, '\n');
    m_findStatus = "Line " + std::to_string(line);
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

    // Ctrl+Shift+F works from anywhere in Studio.
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false)) showFindAll();

    if (!s) {
        ImGui::Spacing();
        ImGui::TextWrapped("No script open.");
        ImGui::Spacing();
        ImGui::TextDisabled("To write code:");
        ImGui::BulletText("Select a part, then click  + Script  in the Toolbox\n(or Add > Script in the menu).");
        ImGui::BulletText("Double-click any Script in the Explorer to open it here.");
        ImGui::BulletText("Press Play (F5) to run your scripts.");
        ImGui::BulletText("Ctrl+Shift+F searches every script.");
        ImGui::End();
        return;
    }

    // Find / replace keys while this panel has focus.
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        m_showFind = true; m_showReplace = false; m_focusFind = true;
    }
    if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H, false)) {
        m_showFind = true; m_showReplace = true; m_focusFind = true;
    }
    if (focused && m_showFind && ImGui::IsKeyPressed(ImGuiKey_F3, false)) findNext(s->source, io.KeyShift);

    // --- Header: which script, enabled toggle, snippets ---
    ImGui::TextDisabled("Editing");
    ImGui::SameLine();
    ImGui::TextUnformatted(s->fullName().c_str());
    ImGui::SameLine();
    ImGui::Checkbox("Enabled", &s->enabled);
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
    ImGui::SameLine();
    if (ImGui::Button("Find")) { m_showFind = !m_showFind; m_focusFind = m_showFind; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Find (Ctrl+F) and replace (Ctrl+H) in this script");
    ImGui::SameLine();
    if (ImGui::Button("Find in all")) showFindAll();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search every script in the game (Ctrl+Shift+F)");

    // --- Find / replace bar ---
    if (m_showFind) {
        ImGui::SetNextItemWidth(220);
        if (m_focusFind) { ImGui::SetKeyboardFocusHere(); m_focusFind = false; }
        if (ImGui::InputTextWithHint("##find", "Find", &m_find, ImGuiInputTextFlags_EnterReturnsTrue))
            findNext(s->source, io.KeyShift);
        if (ImGui::IsItemEdited()) { m_matchAt = -1; m_findStatus.clear(); }
        if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_showFind = false;
        ImGui::SameLine();
        if (ImGui::ArrowButton("##prev", ImGuiDir_Up)) findNext(s->source, true);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##next", ImGuiDir_Down)) findNext(s->source, false);
        ImGui::SameLine();
        ImGui::Checkbox("Aa", &m_matchCase);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Match capital letters exactly");
        ImGui::SameLine();
        if (!m_find.empty()) {
            int count = 0;
            for (size_t at = findText(s->source, m_find, 0, m_matchCase); at != std::string::npos;
                 at = findText(s->source, m_find, at + m_find.size(), m_matchCase))
                ++count;
            ImGui::TextDisabled("%d found%s%s", count, m_findStatus.empty() ? "" : "  -  ", m_findStatus.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(m_showReplace ? "Hide replace" : "Replace...")) m_showReplace = !m_showReplace;
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) m_showFind = false;
        if (m_showReplace) {
            ImGui::SetNextItemWidth(220);
            ImGui::InputTextWithHint("##replace", "Replace with", &m_replace);
            ImGui::SameLine();
            if (ImGui::Button("Replace")) {
                // Replace the match we're on (if the cursor is still on it), then find the next one.
                if (m_matchAt >= 0 && findText(s->source, m_find, (size_t)m_matchAt, m_matchCase) == (size_t)m_matchAt) {
                    s->source.replace((size_t)m_matchAt, m_find.size(), m_replace);
                    m_cursor = m_matchAt + (int)m_replace.size();
                    m_matchAt = -1;
                }
                findNext(s->source, false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Replace all")) {
                int n = 0;
                for (size_t at = findText(s->source, m_find, 0, m_matchCase); at != std::string::npos;
                     at = findText(s->source, m_find, at + m_replace.size(), m_matchCase)) {
                    s->source.replace(at, m_find.size(), m_replace);
                    if (++n > 100000) break;
                }
                m_findStatus = "Replaced " + std::to_string(n);
                m_matchAt = -1;
            }
        }
    }

    // --- Code area ---
    float footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
    ImGui::PushFont(EditorTheme::codeFont());
    // While suggestions show, Tab picks one; otherwise it types a tab.
    const bool suggesting = m_codeActive && !m_suggest.empty();
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackAlways |
                                (suggesting ? ImGuiInputTextFlags_CallbackCompletion : ImGuiInputTextFlags_AllowTabInput);
    if (m_focusCode) { ImGui::SetKeyboardFocusHere(); m_focusCode = false; }
    ImGui::InputTextMultiline("##code", &s->source, ImVec2(-1, -footer), flags, &ScriptEditorPanel::onEdit, this);
    m_codeActive = ImGui::IsItemActive();
    const float lineH = ImGui::GetTextLineHeight();
    ImGui::PopFont();

    // --- Autocomplete popup under the cursor ---
    if (m_codeActive) updateSuggestions(s->source);
    else m_suggest.clear();
    if (m_codeActive && !m_suggest.empty()) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) m_pick = (m_pick + 1) % (int)m_suggest.size();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_pick = (m_pick + (int)m_suggest.size() - 1) % (int)m_suggest.size();
        ImGui::SetNextWindowPos(ImVec2(m_caret.x, m_caret.y + lineH + 2));
        ImGui::BeginTooltip();
        ImGui::PushFont(EditorTheme::codeFont());
        for (int i = 0; i < (int)m_suggest.size(); ++i) {
            if (i == m_pick) ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1), "> %s", m_suggest[i].c_str());
            else ImGui::Text("  %s", m_suggest[i].c_str());
        }
        ImGui::PopFont();
        ImGui::TextDisabled("Tab: use it   Ctrl+Up/Down: pick");
        ImGui::EndTooltip();
    }

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

// ---------------------------------------------------------------------------
// Find in all scripts
// ---------------------------------------------------------------------------

void ScriptEditorPanel::runFindAll() {
    m_hits.clear();
    m_findAllRan = m_findAll;
    if (m_findAll.empty()) return;
    m_scene->forEach([&](SceneNode* n) {
        if (!n->isScript() || m_hits.size() >= 500) return;
        const std::string& src = n->source;
        for (size_t at = findText(src, m_findAll, 0, false); at != std::string::npos && m_hits.size() < 500;
             at = findText(src, m_findAll, at + m_findAll.size(), false)) {
            size_t ls = src.rfind('\n', at);
            ls = ls == std::string::npos ? 0 : ls + 1;
            size_t le = src.find('\n', at);
            std::string text = src.substr(ls, (le == std::string::npos ? src.size() : le) - ls);
            text.erase(0, text.find_first_not_of(" \t"));
            if (text.size() > 90) text = text.substr(0, 90) + "...";
            int line = 1 + (int)std::count(src.begin(), src.begin() + (long)at, '\n');
            m_hits.push_back({n->id, n->fullName(), line, (int)at, text});
        }
    });
}

void ScriptEditorPanel::renderFindAll() {
    if (!m_showFindAll) return;
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Find in All Scripts", &m_showFindAll)) { ImGui::End(); return; }
    ImGui::SetNextItemWidth(-1);
    if (m_focusFindAll) { ImGui::SetKeyboardFocusHere(); m_focusFindAll = false; }
    ImGui::InputTextWithHint("##findall", "Search every script (not case sensitive)", &m_findAll);
    if (m_findAll != m_findAllRan) runFindAll();
    if (!m_findAll.empty()) ImGui::TextDisabled("%d match%s", (int)m_hits.size(), m_hits.size() == 1 ? "" : "es");
    ImGui::Separator();
    ImGui::BeginChild("##hits");
    for (size_t i = 0; i < m_hits.size(); ++i) {
        const Hit& h = m_hits[i];
        ImGui::PushID((int)i);
        char label[256];
        std::snprintf(label, sizeof(label), "%s  (line %d)", h.name.c_str(), h.line);
        if (ImGui::Selectable(label)) {
            if (SceneNode* n = m_scene->findById(h.script)) {
                m_scene->select(n);
                open(h.script);
                m_selectStart = h.pos;
                m_selectEnd = h.pos + (int)m_findAll.size();
                m_focusCode = true;
            }
        }
        ImGui::PushFont(EditorTheme::codeFont());
        ImGui::TextDisabled("    %s", h.text.c_str());
        ImGui::PopFont();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}
