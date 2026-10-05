// AutoSave and Auto-Recovery, like Roblox Studio's: every few minutes, unsaved
// work is copied to a backup file (your own game file is never touched). Closing
// Studio normally throws the backup away; if Studio crashes or the computer turns
// off, the backup is still there and Studio offers to open it next time.

#include "Editor.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"

#include <ctime>
#include <filesystem>
#include <imgui.h>
#include <nlohmann/json.hpp>

namespace {
constexpr double kAutoSaveEvery = 5 * 60.0;   // seconds

std::filesystem::path backupFile() { return Paths::file("AutoRecovery.gbscene"); }
std::filesystem::path backupInfo() { return Paths::file("AutoRecovery.json"); }
} // namespace

void Editor::autoSaveTick() {
    if (m_frame == 2) {   // just started: was there a crash last time?
        std::string info;
        if (std::filesystem::exists(backupFile()) && Serializer::readFile(backupInfo().string(), info)) {
            auto j = nlohmann::json::parse(info, nullptr, false);
            if (j.is_object()) {
                m_recoverFrom = j.value("file", std::string());
                m_recoverTime = j.value("time", 0LL);
                m_openRecover = true;
                m_recoverPending = true;
            }
        }
    }
    if (m_playing || !m_dirty || m_recoverPending) { m_lastAutoSave = ImGui::GetTime(); return; }
    if (ImGui::GetTime() - m_lastAutoSave < kAutoSaveEvery) return;
    m_lastAutoSave = ImGui::GetTime();
    if (Serializer::writeFile(backupFile().string(), Serializer::saveScene(*m_scene))) {
        nlohmann::json j = {{"file", m_path}, {"time", (long long)std::time(nullptr)}};
        Serializer::writeFile(backupInfo().string(), j.dump());
        m_autoSavedAt = std::time(nullptr);
    }
}

void Editor::dropAutoSave() {
    if (m_recoverPending) return;   // "Later": keep it for next time
    std::error_code ec;
    std::filesystem::remove(backupFile(), ec);
    std::filesystem::remove(backupInfo(), ec);
}

void Editor::renderRecoverDialog() {
    if (m_openRecover) { ImGui::OpenPopup("Auto-Recovery"); m_openRecover = false; }
    ImGui::SetNextWindowSize(ImVec2(460, 0));
    if (!ImGui::BeginPopupModal("Auto-Recovery", nullptr, ImGuiWindowFlags_NoResize)) return;
    char when[64] = "a while ago";
    std::time_t t = (std::time_t)m_recoverTime;
    if (std::tm* tm = std::localtime(&t)) std::strftime(when, sizeof when, "%b %d at %H:%M", tm);
    ImGui::TextWrapped("Studio closed without saving last time. It kept a copy of your work from %s%s%s.", when,
                       m_recoverFrom.empty() ? "" : " (", m_recoverFrom.empty() ? "" : (std::filesystem::path(m_recoverFrom).filename().string() + ")").c_str());
    ImGui::Spacing();
    if (ImGui::Button("Open it", ImVec2(130, 30))) {
        m_recoverPending = false;
        openFile(backupFile().string());
        m_path = m_recoverFrom;   // Save puts it back where it came from (or asks for a name)
        m_dirty = true;
        dropAutoSave();
        Log::system("Recovered your unsaved work. Save it (Ctrl+S) to keep it.");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Throw it away", ImVec2(130, 30))) { m_recoverPending = false; dropAutoSave(); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("Later", ImVec2(90, 30))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
