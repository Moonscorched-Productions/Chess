#pragma once

#include <optional>
#include <string>

#include <imgui.h>

#include "app/Settings.h"
#include "chess/GameTree.h"
#include "ui/BoardView.h"

namespace ui {

// Text colours for errors and highlights. They follow the colour scheme, see setColorScheme().
inline ImVec4 kErrorColor(0.90f, 0.42f, 0.38f, 1.0f);
inline ImVec4 kAccentColor(0.60f, 0.80f, 0.40f, 1.0f);

// Sets the colours above for the current theme.
void setColorScheme(ImVec4 accent, ImVec4 error);

// Engines report a tablebase win as a score of about 200 pawns.
inline constexpr int kTablebaseScore = 15000;

// True while the pieces should be hidden: blindfold mode is on and the user is not
// holding Space to peek.
bool blindfoldHidden(const app::Settings& settings);

// Board appearance options taken from the user's settings.
BoardOptions boardOptionsFrom(const app::Settings& settings);

// "+0.35", "-1.20", "M5", "-M3": an evaluation from White's point of view.
std::string formatEval(const core::Eval& eval);

// Annotation suffix for a PGN glyph number ("!", "??", ...), or "" if there is none.
const char* nagSymbol(int nag);

// Vertical evaluation bar: White's share grows from White's side of the board.
// With `animate`, the bar glides to a new value instead of jumping.
void drawEvalBar(ImVec2 size, const std::optional<core::Eval>& eval, bool flipped, bool animate = true);

// Starts a modal popup centred on the main window. Returns true while it is open.
bool beginCenteredModal(const char* title, ImGuiWindowFlags flags = 0);

}  // namespace ui
