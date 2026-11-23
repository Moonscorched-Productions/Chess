#include "app/App.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>
#include <optional>
#include <string_view>

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <imgui-SFML.h>
#include <imgui.h>

#include "app/Paths.h"
#include "app/Theme.h"
#include "chess/Pgn.h"
#include "storage/GameStore.h"
#include "ui/AnalysisScreen.h"
#include "ui/LibraryScreen.h"
#include "ui/EndgameScreen.h"
#include "ui/OpeningsScreen.h"
#include "ui/PlayScreen.h"
#include "ui/PuzzleScreen.h"
#include "storage/PuzzleStore.h"
#include "storage/TrainingStore.h"
#include "ui/SettingsScreen.h"

namespace app {

namespace {

struct SectionInfo {
    Section section;
    const char* label;
    const char* plannedFor;  // nullptr when the section is implemented
};

constexpr std::array<SectionInfo, 9> kSections = {{
    {Section::Play, "Play", nullptr},
    {Section::Analysis, "Analysis", nullptr},
    {Section::Review, "Game Review", "Phase 4"},
    {Section::Puzzles, "Puzzles", nullptr},
    {Section::Openings, "Openings", nullptr},
    {Section::Endgames, "Endgames", nullptr},
    {Section::Library, "Game Library", nullptr},
    {Section::Statistics, "Statistics", "Phase 4"},
    {Section::Settings, "Settings", nullptr},
}};

const SectionInfo& infoFor(Section section) {
    for (const SectionInfo& info : kSections)
        if (info.section == section) return info;
    return kSections[0];
}

constexpr ImGuiWindowFlags kFixedWindow = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus;

}  // namespace

Options Options::parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--screenshot" && hasValue) {
            options.screenshotPath = argv[++i];
        } else if (arg == "--after" && hasValue) {
            options.screenshotAfter = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--size" && hasValue) {
            unsigned w = 0, h = 0;
            if (std::sscanf(argv[++i], "%ux%u", &w, &h) == 2 && w >= 200 && h >= 200) {
                options.width = w;
                options.height = h;
            }
        } else if (arg == "--autoplay") {
            options.autoplay = true;
        } else if (arg == "--settings") {
            options.openSettings = true;
        } else if (arg == "--new-game") {
            options.openNewGame = true;
        } else if (arg == "--analysis") {
            options.openAnalysis = true;
        } else if (arg == "--openings") {
            options.openOpenings = true;
        } else if (arg == "--endgames") {
            options.openEndgames = true;
        } else if (arg == "--repertoire" && hasValue) {
            options.repertoireFile = argv[++i];
        } else if (arg == "--train") {
            options.trainOpenings = true;
        } else if (arg == "--endgame" && hasValue) {
            options.endgameId = argv[++i];
        } else if (arg == "--puzzles") {
            options.openPuzzles = true;
        } else if (arg == "--autosolve") {
            options.autoSolve = true;
        } else if (arg == "--library") {
            options.openLibrary = true;
        } else if (arg == "--pgn" && hasValue) {
            options.pgnFile = argv[++i];
        } else if (arg == "--import" && hasValue) {
            options.importFile = argv[++i];
        } else if (arg == "--ply" && hasValue) {
            options.ply = std::atoi(argv[++i]);
        } else if (arg == "--analyse-game") {
            options.analyseGame = true;
        } else if (arg == "--discord") {
            options.forceDiscord = true;
        } else if (arg == "--no-save") {
            options.temporarySettings = true;
        }
    }
    return options;
}

namespace {

// Where the game database lives. Test runs get a throwaway file of their own.
std::filesystem::path databasePath(bool temporary) {
    if (!temporary) return userDataDir() / "games.db";
    std::error_code ec;
    const std::filesystem::path file = std::filesystem::temp_directory_path(ec) / "chess-desktop-test.db";
    for (const char* suffix : {"", "-wal", "-shm"}) std::filesystem::remove(std::filesystem::path(file.string() + suffix), ec);
    return file;
}

}  // namespace

App::App(const Options& options)
    : options_(options),
      settingsFile_(userDataDir() / "settings.ini"),
      settings_(options.temporarySettings ? Settings{} : Settings::load(settingsFile_)),
      window_(sf::VideoMode({options.width, options.height}), "Chess") {
    window_.setVerticalSyncEnabled(true);
    window_.setMinimumSize(sf::Vector2u{900u, 600u});

    imguiReady_ = ImGui::SFML::Init(window_, false);
    if (!imguiReady_) return;
    ImGui::GetIO().IniFilename = nullptr;
    applyTheme();

    const std::string missing = assets_.load(findUpwards("data"));
    if (!missing.empty()) assetWarning_ = "Missing data files: " + missing;

    const std::filesystem::path databaseFile = databasePath(options_.temporarySettings);
    std::string storeError;
    store_ = storage::GameStore::open(databaseFile, &storeError);

    play_ = std::make_unique<ui::PlayScreen>(settings_, assets_, store_.get());
    play_->setAutoplay(options_.autoplay);
    if (options_.openNewGame) play_->openNewGameDialog();
    puzzleStore_ = storage::PuzzleStore::open(databaseFile);
    trainingStore_ = storage::TrainingStore::open(databaseFile);
    openingBook_.load(findUpwards("data") / "openings");

    analysis_ = std::make_unique<ui::AnalysisScreen>(settings_, assets_, &openingBook_);
    puzzles_ = std::make_unique<ui::PuzzleScreen>(settings_, assets_, puzzleStore_.get(), databaseFile,
                                                  findUpwards("data") / "puzzles" / "starter.csv");
    puzzles_->setAutoSolve(options_.autoSolve);
    if (!options_.repertoireFile.empty() && trainingStore_) {
        std::ifstream input(pathFromUtf8(options_.repertoireFile), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const std::optional<core::GameRecord> record = core::parsePgn(text);
        const auto id = trainingStore_->createRepertoire("Test repertoire", core::Color::White, "Tests");
        if (record && id) trainingStore_->saveTree(*id, record->tree);
    }
    openings_ = std::make_unique<ui::OpeningsScreen>(settings_, assets_, trainingStore_.get(), &openingBook_);
    endgames_ = std::make_unique<ui::EndgameScreen>(trainingStore_.get());
    if (options_.trainOpenings) {
        openings_->setAutoAnswer(true);
        openings_->requestTraining();
    }
    if (const core::EndgamePosition* endgame = core::findEndgame(options_.endgameId.c_str())) play_->startChallenge(*endgame);
    library_ = std::make_unique<ui::LibraryScreen>(store_.get(), databaseFile, storeError);
    settingsScreen_ = std::make_unique<ui::SettingsScreen>(settings_, presence_);

    if (options_.openSettings) section_ = Section::Settings;
    if (options_.openAnalysis) section_ = Section::Analysis;
    if (options_.openLibrary) section_ = Section::Library;
    if (options_.openPuzzles) section_ = Section::Puzzles;
    if (options_.openOpenings) section_ = Section::Openings;
    if (options_.openEndgames) section_ = Section::Endgames;
    if (!options_.pgnFile.empty()) {
        std::ifstream input(pathFromUtf8(options_.pgnFile), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (std::optional<core::GameRecord> record = core::parsePgn(text)) analysis_->load(std::move(*record));
    }
    if (options_.ply >= 0) analysis_->goToPly(options_.ply);
    if (options_.analyseGame) analysis_->requestGameAnalysis();
    if (!options_.importFile.empty() && store_) store_->importFile(pathFromUtf8(options_.importFile));
}

App::~App() {
    saveSettings();
    // Screens hold engine processes and textures; release them before the window goes away.
    settingsScreen_.reset();
    library_.reset();
    puzzles_.reset();
    openings_.reset();
    endgames_.reset();
    analysis_.reset();
    play_.reset();
    store_.reset();
    puzzleStore_.reset();
    trainingStore_.reset();
    if (imguiReady_) ImGui::SFML::Shutdown();
}

// Moves games and requests between the screens, which do not know each other.
void App::routeRequests() {
    if (play_->consumeSettingsChanged() || analysis_->consumeSettingsChanged()) saveSettings();
    if (play_->consumeLibraryChanged()) library_->markDirty();

    if (std::optional<core::GameRecord> record = openings_->takeAnalysisRequest()) {
        analysis_->load(std::move(*record));
        section_ = Section::Analysis;
    }
    // Endgame training is played in the Play section; its result comes back here.
    if (const core::EndgamePosition* endgame = endgames_->takeStartRequest()) {
        play_->startChallenge(*endgame);
        section_ = Section::Play;
    }
    if (const std::optional<ui::PlayScreen::ChallengeResult> result = play_->takeChallengeResult()) {
        if (trainingStore_) trainingStore_->recordEndgame(result->id, result->success);
        endgames_->markDirty();
    }
    if (std::optional<core::GameRecord> record = puzzles_->takeAnalysisRequest()) {
        analysis_->load(std::move(*record));
        section_ = Section::Analysis;
    }
    if (std::optional<core::GameRecord> record = play_->takeAnalysisRequest()) {
        analysis_->load(std::move(*record));
        section_ = Section::Analysis;
    }
    if (std::optional<ui::LibraryScreen::OpenRequest> request = library_->takeOpenRequest()) {
        analysis_->load(std::move(request->record), request->id);
        section_ = Section::Analysis;
    }
    if (std::optional<ui::AnalysisScreen::SaveRequest> request = analysis_->takeSaveRequest()) {
        std::optional<std::int64_t> id;
        if (store_) {
            if (request->record.tag("Date").empty()) request->record.setTag("Date", core::pgnDateToday());
            if (request->record.tag("Event").empty()) request->record.setTag("Event", "Analysis");
            if (request->libraryId && store_->update(*request->libraryId, request->record)) {
                id = request->libraryId;
            } else {
                id = store_->add(request->record, "analysis");
            }
        }
        analysis_->onSaved(id);
        library_->markDirty();
    }
}

void App::updatePresence() {
    // Automated test runs stay out of the user's Discord status unless asked.
    presence_.setEnabled(settings_.discordPresence && (!options_.temporarySettings || options_.forceDiscord));

    PresenceText text;
    switch (section_) {
        case Section::Play: text = play_->presence(); break;
        case Section::Analysis: text = analysis_->presence(); break;
        case Section::Puzzles: text = puzzles_->presence(); break;
        case Section::Openings: text = openings_->presence(); break;
        case Section::Endgames:
            text.details = "Choosing an endgame to practise";
            break;
        case Section::Library:
            text.details = "Browsing the game library";
            break;
        case Section::Settings:
            text.details = "Adjusting settings";
            break;
        default:
            text.details = "In the menus";
            break;
    }
    presence_.set(text);
    presence_.tick();
}

void App::saveSettings() {
    if (!options_.temporarySettings) settings_.save(settingsFile_);
}

int App::run() {
    if (!imguiReady_) return 1;

    // Keep drawing while the user drags or resizes the window.
    // The handle is a pointer on Windows and macOS and an integer on X11; either fits a void*.
    liveResize_.install(reinterpret_cast<void*>(window_.getNativeHandle()), [this] { frame(); });

    sf::Clock runClock;
    int exitCode = 0;

    while (window_.isOpen()) {
        while (const std::optional event = window_.pollEvent()) {
            ImGui::SFML::ProcessEvent(window_, *event);
            if (event->is<sf::Event::Closed>()) window_.close();
        }
        if (!window_.isOpen()) break;

        if (!options_.screenshotPath.empty() && runClock.getElapsedTime().asSeconds() >= options_.screenshotAfter) {
            screenshotRequested_ = true;
            frame();
            exitCode = screenshotSaved_ ? 0 : 1;
            window_.close();
            break;
        }
        frame();
    }
    liveResize_.remove();
    return exitCode;
}

void App::frame() {
    const sf::Vector2u size = window_.getSize();
    if (size.x == 0 || size.y == 0) return;  // minimized

    ImGui::SFML::Update(window_, frameClock_.restart());

    play_->update();
    analysis_->update(section_ == Section::Analysis);
    library_->update();
    if (section_ == Section::Puzzles) puzzles_->update();
    if (section_ == Section::Openings) openings_->update();
    routeRequests();
    updatePresence();

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sidebarWidth = 190.0f;
    drawSidebar(sidebarWidth, display.y);
    drawContent(sidebarWidth, display.x - sidebarWidth, display.y);

    window_.clear(sf::Color(22, 21, 18));
    ImGui::SFML::Render(window_);
    if (screenshotRequested_) {
        screenshotRequested_ = false;
        screenshotSaved_ = saveScreenshot();
    }
    window_.display();
}

bool App::saveScreenshot() {
    sf::Texture texture;
    if (!texture.resize(window_.getSize())) return false;
    texture.update(window_);
    sf::Image image = texture.copyToImage();
    // The framebuffer's alpha channel is a by-product of blending; the window itself is opaque.
    const sf::Vector2u size = image.getSize();
    for (unsigned y = 0; y < size.y; ++y) {
        for (unsigned x = 0; x < size.x; ++x) {
            sf::Color pixel = image.getPixel({x, y});
            pixel.a = 255;
            image.setPixel({x, y}, pixel);
        }
    }
    return image.saveToFile(pathFromUtf8(options_.screenshotPath));
}

void App::drawSidebar(float width, float height) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
    ImGui::Begin("##sidebar", nullptr, kFixedWindow);

    ImGui::TextUnformatted("Chess");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    for (const SectionInfo& info : kSections) {
        const bool selected = section_ == info.section;
        const bool planned = info.plannedFor != nullptr;
        if (planned) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Selectable(info.label, selected, ImGuiSelectableFlags_None, ImVec2(0.0f, 30.0f))) section_ = info.section;
        if (planned) ImGui::PopStyleColor();
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

void App::drawContent(float x, float width, float height) {
    ImGui::SetNextWindowPos(ImVec2(x, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##content", nullptr, kFixedWindow);

    if (!assetWarning_.empty()) {
        ImGui::TextColored(ImVec4(0.90f, 0.42f, 0.38f, 1.0f), "%s", assetWarning_.c_str());
    }

    const SectionInfo& info = infoFor(section_);
    if (section_ == Section::Play) {
        play_->draw();
    } else if (section_ == Section::Analysis) {
        analysis_->draw();
    } else if (section_ == Section::Openings) {
        openings_->draw();
    } else if (section_ == Section::Endgames) {
        endgames_->draw();
    } else if (section_ == Section::Puzzles) {
        puzzles_->draw();
    } else if (section_ == Section::Library) {
        library_->draw();
    } else if (section_ == Section::Settings) {
        const ui::SettingsScreen::Changes changes = settingsScreen_->draw();
        if (changes.any) {
            saveSettings();
            play_->onSettingsChanged(changes.enginePath);
            analysis_->onSettingsChanged(changes.enginePath);
        }
    } else {
        ImGui::TextUnformatted(info.label);
        ImGui::TextDisabled("This section arrives in %s.", info.plannedFor);
    }

    ImGui::End();
}

}  // namespace app
