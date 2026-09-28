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

} // namespace EditorTheme
