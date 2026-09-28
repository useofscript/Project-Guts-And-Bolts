#include "UpdateToast.h"
#include "../core/Settings.h"
#include "../core/UpdateChecker.h"

#include <imgui.h>
#include <algorithm>
#include <cstdlib>
#include <string>

namespace UpdateToast {

namespace {
bool  g_started = false;
bool  g_dismissed = false;
float g_slide = 0.0f;       // 0 = hidden, 1 = fully in
std::string g_error;

void openUrl(const std::string& url) {
#ifdef _WIN32
    std::system(("start \"\" \"" + url + "\"").c_str());
#elif defined(__APPLE__)
    std::system(("open \"" + url + "\"").c_str());
#else
    std::system(("xdg-open \"" + url + "\" >/dev/null 2>&1 &").c_str());
#endif
}
} // namespace

bool draw(const char* appName) {
    if (!g_started) {
        g_started = true;
        if (GraphicsSettings::get().checkUpdates) UpdateChecker::start();
    }
    UpdateChecker::Info info = UpdateChecker::info();
    bool show = info.state == UpdateChecker::State::Available && !g_dismissed;
    float dt = ImGui::GetIO().DeltaTime;
    g_slide = std::clamp(g_slide + (show ? dt : -dt) * 4.0f, 0.0f, 1.0f);
    if (g_slide <= 0.0f) return false;

    // Ease-out slide from the right edge.
    float e = 1.0f - (1.0f - g_slide) * (1.0f - g_slide);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = 360.0f;
    ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - (w + 20.0f) * e, vp->WorkPos.y + vp->WorkSize.y - 170.0f);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.13f, 0.17f, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.55f, 0.2f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.93f, 0.95f, 1));
    ImGui::Begin("##updatetoast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);

    bool quit = false;
    ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.25f, 1), "Update available");
    ImGui::SameLine(w - 44);
    if (ImGui::SmallButton("x")) g_dismissed = true;
    ImGui::PushTextWrapPos(w - 16);
    ImGui::Text("%d new change%s since your version (%s).", info.behindBy, info.behindBy == 1 ? "" : "s",
                UpdateChecker::currentVersion());
    ImGui::PopTextWrapPos();
    if (!info.latestMessage.empty()) {
        ImGui::PushTextWrapPos(w - 16);
        ImGui::TextDisabled("Latest: %s", info.latestMessage.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!g_error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_error.c_str());
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.5f, 0.15f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.25f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::BeginDisabled(!UpdateChecker::canUpdate());
    if (ImGui::Button("Update now", ImVec2(120, 30))) {
        if (UpdateChecker::launchUpdater(appName)) quit = true;
        else g_error = "Couldn't start the updater - run install.py --update yourself.";
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    if (!UpdateChecker::canUpdate() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("The source folder isn't next to this build - download the new version from GitHub.");
    ImGui::SameLine();
    if (ImGui::Button("What's new", ImVec2(100, 30))) openUrl(info.compareUrl);
    ImGui::SameLine();
    if (ImGui::Button("Later", ImVec2(80, 30))) g_dismissed = true;

    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return quit;
}

} // namespace UpdateToast
