#pragma once

struct ImFont;

// A cohesive "soft dark" editor theme. Keeping the look in one place makes it
// easy to tweak the whole UI from a single palette.
namespace EditorTheme {

// Load a crisp UI font (system Segoe UI if available, else ImGui's default).
// Must be called once, before the first frame.
void loadFonts(float scale = 1.0f);   // scale: bigger text on phones

// Monospace font for the script editor (falls back to the default font).
ImFont* codeFont();

// Apply colours and style metrics to the current ImGui context.
void apply();
// Studio's look: Roblox Studio's dark theme (square panels, #2E2E2E greys,
// Roblox blue #00A2FF for highlights and selection).
void applyStudio();
// Roblox blue.
inline constexpr unsigned kRobloxBlue = 0xFFFFA200;   // IM_COL32(0, 162, 255, 255)

} // namespace EditorTheme
