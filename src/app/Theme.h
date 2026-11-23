#pragma once

struct ImFont;

namespace app {

// Applies the application style, dark or light. The first call also loads the fonts.
// Requires a live ImGui context; safe to call again when the setting changes.
void applyTheme(bool light = false);

// A larger font for headline numbers (falls back to the normal font).
ImFont* headlineFont();

}  // namespace app
