#pragma once

struct ImFont;

namespace app {

// Applies the application style, dark or light. The first call also loads the fonts.
// Requires a live ImGui context; safe to call again when the setting changes.
void applyTheme(bool light = false);

// Pixels per point of the window. Fonts are rasterized at this density (reloaded when it
// changes). Must be called between frames.
void setFontDensity(float pixelsPerPoint);

// A larger font for headline numbers (falls back to the normal font).
ImFont* headlineFont();

}  // namespace app
