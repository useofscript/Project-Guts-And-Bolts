#include "Theme.h"

#include <imgui.h>
#include <cstdio>

namespace EditorTheme {

namespace {
// Palette ---------------------------------------------------------------------
constexpr ImVec4 kAccent      = {0.26f, 0.55f, 0.96f, 1.00f};
constexpr ImVec4 kAccentHover = {0.33f, 0.62f, 1.00f, 1.00f};

constexpr ImVec4 kText        = {0.90f, 0.91f, 0.93f, 1.00f};
constexpr ImVec4 kTextDim     = {0.46f, 0.49f, 0.55f, 1.00f};

constexpr ImVec4 kBgDarkest   = {0.094f, 0.102f, 0.122f, 1.00f}; // menu/title bars
constexpr ImVec4 kBgWindow    = {0.122f, 0.133f, 0.157f, 1.00f};
constexpr ImVec4 kBgPopup     = {0.137f, 0.149f, 0.176f, 1.00f};
constexpr ImVec4 kFrame       = {0.169f, 0.184f, 0.216f, 1.00f};
constexpr ImVec4 kFrameHover  = {0.212f, 0.231f, 0.271f, 1.00f};
constexpr ImVec4 kFrameActive = {0.247f, 0.271f, 0.318f, 1.00f};
constexpr ImVec4 kBorder      = {1.00f, 1.00f, 1.00f, 0.055f};

ImVec4 withAlpha(ImVec4 c, float a) { c.w = a; return c; }
} // namespace

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Crisp settings for a UI font: no oversampling + horizontal pixel snap.
    // (Heavy oversampling enlarges the atlas and proved unstable on some
    // drivers, so keep the atlas modest.)
    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;

    // Prefer a clean system UI font; fall back to ImGui's built-in bitmap font.
    const char* candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/SegoeUI.ttf",
    };
    ImFont* loaded = nullptr;
    for (const char* path : candidates) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            loaded = io.Fonts->AddFontFromFileTTF(path, 17.0f, &cfg);
            if (loaded) break;
        }
    }
    if (!loaded)
        io.Fonts->AddFontDefault();
}

void apply() {
    ImGuiStyle& s = ImGui::GetStyle();

    // Metrics -----------------------------------------------------------------
    s.WindowPadding     = {10, 10};
    s.FramePadding      = {8, 5};
    s.ItemSpacing       = {8, 7};
    s.ItemInnerSpacing  = {6, 5};
    s.CellPadding       = {6, 4};
    s.IndentSpacing     = 20;
    s.ScrollbarSize     = 12;
    s.GrabMinSize       = 10;

    s.WindowBorderSize  = 1;
    s.ChildBorderSize   = 1;
    s.PopupBorderSize   = 1;
    s.FrameBorderSize   = 0;

    s.WindowRounding    = 6;
    s.ChildRounding     = 6;
    s.FrameRounding     = 5;
    s.PopupRounding     = 5;
    s.ScrollbarRounding = 9;
    s.GrabRounding      = 5;
    s.TabRounding       = 6;

    s.WindowTitleAlign  = {0.0f, 0.5f};
    s.WindowMenuButtonPosition = ImGuiDir_None;

    // Colours -----------------------------------------------------------------
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                  = kText;
    c[ImGuiCol_TextDisabled]          = kTextDim;
    c[ImGuiCol_WindowBg]              = kBgWindow;
    c[ImGuiCol_ChildBg]               = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg]               = kBgPopup;
    c[ImGuiCol_Border]                = kBorder;
    c[ImGuiCol_BorderShadow]          = {0, 0, 0, 0};

    c[ImGuiCol_FrameBg]               = kFrame;
    c[ImGuiCol_FrameBgHovered]        = kFrameHover;
    c[ImGuiCol_FrameBgActive]         = kFrameActive;

    c[ImGuiCol_TitleBg]               = kBgDarkest;
    c[ImGuiCol_TitleBgActive]         = kBgDarkest;
    c[ImGuiCol_TitleBgCollapsed]      = kBgDarkest;
    c[ImGuiCol_MenuBarBg]             = kBgDarkest;

    c[ImGuiCol_ScrollbarBg]           = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab]         = kFrameActive;
    c[ImGuiCol_ScrollbarGrabHovered]  = {0.30f, 0.33f, 0.39f, 1.0f};
    c[ImGuiCol_ScrollbarGrabActive]   = kAccent;

    c[ImGuiCol_CheckMark]             = kAccent;
    c[ImGuiCol_SliderGrab]            = kAccent;
    c[ImGuiCol_SliderGrabActive]      = kAccentHover;

    c[ImGuiCol_Button]                = kFrame;
    c[ImGuiCol_ButtonHovered]         = kFrameHover;
    c[ImGuiCol_ButtonActive]          = kFrameActive;

    c[ImGuiCol_Header]                = withAlpha(kAccent, 0.28f);
    c[ImGuiCol_HeaderHovered]         = withAlpha(kAccent, 0.45f);
    c[ImGuiCol_HeaderActive]          = withAlpha(kAccent, 0.65f);

    c[ImGuiCol_Separator]             = kBorder;
    c[ImGuiCol_SeparatorHovered]      = withAlpha(kAccent, 0.6f);
    c[ImGuiCol_SeparatorActive]       = kAccent;

    c[ImGuiCol_ResizeGrip]            = {1, 1, 1, 0.03f};
    c[ImGuiCol_ResizeGripHovered]     = withAlpha(kAccent, 0.5f);
    c[ImGuiCol_ResizeGripActive]      = kAccent;

    c[ImGuiCol_Tab]                   = kBgDarkest;
    c[ImGuiCol_TabHovered]            = kFrameHover;
    c[ImGuiCol_TabSelected]           = kFrame;
    c[ImGuiCol_TabSelectedOverline]   = kAccent;
    c[ImGuiCol_TabDimmed]             = kBgDarkest;
    c[ImGuiCol_TabDimmedSelected]     = {0.165f, 0.180f, 0.212f, 1.0f};

    c[ImGuiCol_DockingPreview]        = withAlpha(kAccent, 0.40f);
    c[ImGuiCol_DockingEmptyBg]        = kBgDarkest;

    c[ImGuiCol_TextSelectedBg]        = withAlpha(kAccent, 0.35f);
    c[ImGuiCol_NavCursor]             = kAccent;
}

} // namespace EditorTheme
