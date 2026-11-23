#pragma once

#include <imgui.h>

namespace app {

// Applies a colour theme (an index into uiThemes()) and the application style. The first
// call also loads the fonts. Requires a live ImGui context; safe to call again.
void applyTheme(int theme);

// Pixels per point of the window. Fonts are rasterized at this density (reloaded when it
// changes). Must be called between frames.
void setFontDensity(float pixelsPerPoint);
// Chooses the bundled serif (data/fonts) or the system's interface font. Must be called
// between frames.
void setSerifFont(bool serif);

// A larger font for headline numbers, and one for section titles (both fall back to the
// normal font).
ImFont* headlineFont();
ImFont* titleFont();

// Colours that screens use for their own drawing.
struct ThemeColors {
    ImVec4 accentText;  // readable accent for text on panels
    ImVec4 error;
    ImVec4 background;  // behind everything
    bool light = false;
};
ThemeColors themeColors(int theme);

// Paints the application background into ImGui's background draw list: a gradient with soft,
// slowly drifting light. `time` is in seconds; with `animate` false the picture stands still.
void drawBackground(int theme, double time, bool animate);

}  // namespace app
