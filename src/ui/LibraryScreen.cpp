#include "ui/LibraryScreen.h"

#include <utility>

#include <imgui.h>

#include "app/FileDialog.h"
#include "chess/Pgn.h"
#include "ui/Common.h"

namespace ui {

namespace {

constexpr int kListLimit = 1000;
constexpr const char* kResultFilters[] = {"", "1-0", "0-1", "1/2-1/2", "*"};

const char* sourceLabel(const std::string& source) {
    if (source == "play") return "Played";
    if (source == "import") return "Imported";
    if (source == "analysis") return "Analysis";
    return source.c_str();
}

}  // namespace

LibraryScreen::LibraryScreen(storage::GameStore* store, std::filesystem::path databaseFile, std::string openError)
    : store_(store), databaseFile_(std::move(databaseFile)), openError_(std::move(openError)) {}

LibraryScreen::~LibraryScreen() {
    importCancel_ = true;
    if (importThread_.joinable()) importThread_.join();
}

std::optional<LibraryScreen::OpenRequest> LibraryScreen::takeOpenRequest() {
    return std::exchange(openRequest_, std::nullopt);
}

std::optional<LibraryScreen::OpenRequest> LibraryScreen::takeReviewRequest() {
    return std::exchange(reviewRequest_, std::nullopt);
}

bool LibraryScreen::consumeChanged() { return std::exchange(changed_, false); }

void LibraryScreen::update() {
    if (importDone_.load()) finishImport();
}

void LibraryScreen::reload() {
    dirty_ = false;
    if (store_ == nullptr) return;
    storage::GameFilter filter;
    filter.text = search_.data();
    filter.result = kResultFilters[resultFilter_];
    filter.limit = kListLimit;
    games_ = store_->list(filter);
    total_ = store_->count();
}

void LibraryScreen::openGame(storage::GameId id, bool review) {
    if (store_ == nullptr) return;
    const std::optional<std::string> text = store_->pgn(id);
    std::string error;
    std::optional<core::GameRecord> record = text ? core::parsePgn(*text, &error) : std::nullopt;
    if (!record) {
        notice_ = "Could not open the game" + (error.empty() ? std::string(".") : ": " + error + ".");
        return;
    }
    (review ? reviewRequest_ : openRequest_) = OpenRequest{id, std::move(*record)};
}

void LibraryScreen::startImport(const std::filesystem::path& file) {
    if (importRunning_.load()) return;
    if (importThread_.joinable()) importThread_.join();
    importRunning_ = true;
    importDone_ = false;
    importCancel_ = false;
    importProgress_ = 0;
    importResult_ = {};
    notice_.clear();

    // The worker uses its own connection, so the list stays usable while it runs.
    importThread_ = std::thread([this, file] {
        std::string error;
        const std::unique_ptr<storage::GameStore> store = storage::GameStore::open(databaseFile_, &error);
        if (store) {
            importResult_ = store->importFile(file, &importProgress_, &importCancel_);
        } else {
            importResult_.error = error;
        }
        importDone_ = true;
    });
}

void LibraryScreen::finishImport() {
    if (importThread_.joinable()) importThread_.join();
    importDone_ = false;
    importRunning_ = false;

    if (!importResult_.error.empty()) {
        notice_ = "Import failed: " + importResult_.error;
    } else {
        notice_ = "Imported " + std::to_string(importResult_.imported) + (importResult_.imported == 1 ? " game" : " games");
        if (importResult_.skipped > 0) notice_ += ", skipped " + std::to_string(importResult_.skipped) + " that could not be read";
        notice_ += ".";
    }
    dirty_ = true;
    changed_ = true;
}

void LibraryScreen::exportGames(const std::vector<storage::GameId>& ids) {
    if (store_ == nullptr) return;
    const std::optional<std::filesystem::path> path = app::savePgnDialog(ids.empty() ? "games.pgn" : "game.pgn");
    if (!path) return;
    notice_ = store_->exportFile(*path, ids) ? "Export finished." : "Could not write the file.";
}

void LibraryScreen::draw() {
    ImGui::TextUnformatted("Game Library");
    if (store_ == nullptr) {
        ImGui::TextColored(kErrorColor, "%s", openError_.empty() ? "The game database is not available." : openError_.c_str());
        return;
    }
    if (dirty_) reload();

    ImGui::SameLine();
    ImGui::TextDisabled("%d %s", total_, total_ == 1 ? "game" : "games");
    ImGui::Spacing();

    // --- Toolbar ---------------------------------------------------------------
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::InputTextWithHint("##search", "Search players and events", search_.data(), search_.size())) dirty_ = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::Combo("##result", &resultFilter_, "Any result\0White won\0Black won\0Draw\0Unfinished\0")) dirty_ = true;

    const bool importing = importRunning_.load();
    ImGui::SameLine(0.0f, 24.0f);
    if (importing) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Importing... %d games", importProgress_.load());
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) importCancel_ = true;
    } else {
        if (ImGui::Button("Import PGN...")) {
            if (const std::optional<std::filesystem::path> path = app::openPgnDialog()) startImport(*path);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(total_ == 0);
        if (ImGui::Button("Export all...")) exportGames({});
        ImGui::EndDisabled();
    }

    const bool hasSelection = selected_ != 0;
    // The actions for the selected game move to their own row in a narrow window.
    const ImGuiStyle& style = ImGui::GetStyle();
    float actionsWidth = 3.0f * style.ItemSpacing.x;
    for (const char* label : {"Open in Analysis", "Review", "Export...", "Delete"})
        actionsWidth += ImGui::CalcTextSize(label).x + 2.0f * style.FramePadding.x;
    ImGui::SameLine(0.0f, 24.0f);
    if (ImGui::GetContentRegionAvail().x < actionsWidth) ImGui::NewLine();
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Open in Analysis")) openGame(selected_);
    ImGui::SameLine();
    if (ImGui::Button("Review")) openGame(selected_, true);
    ImGui::SetItemTooltip("Let the engine check every move of this game");
    ImGui::SameLine();
    if (ImGui::Button("Export...")) exportGames({selected_});
    ImGui::SameLine();
    if (ImGui::Button("Delete")) wantDeleteDialog_ = true;
    ImGui::EndDisabled();

    if (!notice_.empty()) {
        ImGui::TextDisabled("%s", notice_.c_str());
    } else if (total_ == 0) {
        ImGui::TextDisabled("Games you finish in Play are stored here. You can also import PGN files.");
    }
    ImGui::Spacing();

    // --- Table -----------------------------------------------------------------
    const float footer = static_cast<int>(games_.size()) >= kListLimit ? ImGui::GetTextLineHeightWithSpacing() : 0.0f;
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    // The list sits on a panel like the rest of the app.
    ImGui::BeginChild("##games-card", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - footer), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginTable("##games", 7, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 96.0f);
        ImGui::TableSetupColumn("White", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Black", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Moves", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 84.0f);
        tableHeaders();

        storage::GameId toOpen = 0;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(games_.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const storage::GameSummary& game = games_[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(game.id));
                const char* date = game.date.empty() ? "-" : game.date.c_str();
                const RowClick click = tableRow(date, selected_ == game.id);
                if (click.clicked) selected_ = game.id;
                if (click.doubleClicked) toOpen = game.id;
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(game.white.empty() ? "?" : game.white.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(game.black.empty() ? "?" : game.black.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(game.result == "1/2-1/2" ? "\xC2\xBD-\xC2\xBD" : game.result.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%d", game.moves);
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(game.event.c_str());
                ImGui::TableSetColumnIndex(6);
                ImGui::TextDisabled("%s", sourceLabel(game.source));
            }
        }
        ImGui::EndTable();
        if (toOpen != 0) openGame(toOpen);
    }
    ImGui::EndChild();
    if (footer > 0.0f) ImGui::TextDisabled("Showing the newest %d games. Search to narrow the list.", kListLimit);

    // --- Delete confirmation ---------------------------------------------------
    if (wantDeleteDialog_) {
        ImGui::OpenPopup("Delete game");
        wantDeleteDialog_ = false;
    }
    if (beginCenteredModal("Delete game", ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Delete this game from the library? This cannot be undone.");
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
            if (store_->remove(selected_)) {
                notice_ = "Game deleted.";
                selected_ = 0;
                dirty_ = true;
                changed_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace ui
