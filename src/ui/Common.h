#pragma once

#include <optional>
#include <string>

#include <imgui.h>

#include "app/Settings.h"
#include "chess/GameTree.h"
#include "ui/BoardView.h"

namespace ui {

inline constexpr ImVec4 kErrorColor(0.90f, 0.42f, 0.38f, 1.0f);
inline constexpr ImVec4 kAccentColor(0.60f, 0.80f, 0.40f, 1.0f);

// Board appearance options taken from the user's settings.
BoardOptions boardOptionsFrom(const app::Settings& settings);

// "+0.35", "-1.20", "M5", "-M3": an evaluation from White's point of view.
std::string formatEval(const core::Eval& eval);

// Annotation suffix for a PGN glyph number ("!", "??", ...), or "" if there is none.
const char* nagSymbol(int nag);

// Vertical evaluation bar: White's share grows from White's side of the board.
void drawEvalBar(ImVec2 size, const std::optional<core::Eval>& eval, bool flipped);

// Starts a modal popup centred on the main window. Returns true while it is open.
bool beginCenteredModal(const char* title, ImGuiWindowFlags flags = 0);

}  // namespace ui
