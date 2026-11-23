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
#include <SFML/Window/Mouse.hpp>
#include <imgui-SFML.h>
#include <imgui.h>

#include "app/Paths.h"
#include "app/Theme.h"
#include "platform/Platform.h"
#include "chess/Pgn.h"
#include "storage/GameStore.h"
#include "ui/AnalysisScreen.h"
#include "ui/LibraryScreen.h"
#include "ui/EndgameScreen.h"
#include "ui/OpeningsScreen.h"
#include "ui/PlayScreen.h"
#include "ui/PuzzleScreen.h"
#include "ui/QualityBadge.h"
#include "ui/Common.h"
#include "ui/ReviewScreen.h"
#include "ui/StatisticsScreen.h"
#include "storage/PuzzleStore.h"
#include "storage/ReviewStore.h"
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
    {Section::Review, "Game Review", nullptr},
    {Section::Puzzles, "Puzzles", nullptr},
    {Section::Openings, "Openings", nullptr},
    {Section::Endgames, "Endgames", nullptr},
    {Section::Library, "Game Library", nullptr},
    {Section::Statistics, "Statistics", nullptr},
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
        } else if (arg == "--hint" && hasValue) {
            options.hintLevel = std::atoi(argv[++i]);
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
        } else if (arg == "--review") {
            options.openReview = true;
        } else if (arg == "--review-all") {
            options.reviewAll = true;
        } else if (arg == "--statistics") {
            options.openStatistics = true;
        } else if (arg == "--player" && hasValue) {
            options.playerNames = argv[++i];
        } else if (arg == "--light") {
            options.lightMode = true;
        } else if (arg == "--fullscreen") {
            options.fullscreen = true;
        } else if (arg == "--blindfold") {
            options.blindfold = true;
        } else if (arg == "--badges") {
            options.moveBadges = true;
        } else if (arg == "--make-puzzles") {
            options.makePuzzles = true;
        } else if (arg == "--overlay" && hasValue) {
            options.overlay = std::atoi(argv[++i]);
        } else if (arg == "--fen" && hasValue) {
            options.fen = argv[++i];
        } else if (arg == "--compare" && hasValue) {
            options.compareEngine = argv[++i];
        } else if (arg == "--syzygy" && hasValue) {
            options.syzygyPath = argv[++i];
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

// The same event with positions and sizes divided by `scale`: window pixels to ImGui points.
sf::Event toPoints(const sf::Event& event, float scale) {
    const auto point = [scale](sf::Vector2i p) { return sf::Vector2i(sf::Vector2f(p) / scale); };
    if (const auto* e = event.getIf<sf::Event::MouseMoved>()) return sf::Event::MouseMoved{point(e->position)};
    if (const auto* e = event.getIf<sf::Event::MouseButtonPressed>()) {
        return sf::Event::MouseButtonPressed{e->button, point(e->position)};
    }
    if (const auto* e = event.getIf<sf::Event::MouseButtonReleased>()) {
        return sf::Event::MouseButtonReleased{e->button, point(e->position)};
    }
    if (const auto* e = event.getIf<sf::Event::MouseWheelScrolled>()) {
        return sf::Event::MouseWheelScrolled{e->wheel, e->delta, point(e->position)};
    }
    if (const auto* e = event.getIf<sf::Event::TouchBegan>()) return sf::Event::TouchBegan{e->finger, point(e->position)};
    if (const auto* e = event.getIf<sf::Event::TouchMoved>()) return sf::Event::TouchMoved{e->finger, point(e->position)};
    if (const auto* e = event.getIf<sf::Event::TouchEnded>()) return sf::Event::TouchEnded{e->finger, point(e->position)};
    if (const auto* e = event.getIf<sf::Event::Resized>()) {
        return sf::Event::Resized{sf::Vector2u(sf::Vector2f(e->size) / scale)};
    }
    return event;
}

std::optional<sf::Cursor::Type> systemCursor(ImGuiMouseCursor cursor) {
    switch (cursor) {
        case ImGuiMouseCursor_Arrow: return sf::Cursor::Type::Arrow;
        case ImGuiMouseCursor_TextInput: return sf::Cursor::Type::Text;
        case ImGuiMouseCursor_ResizeAll: return sf::Cursor::Type::SizeAll;
        case ImGuiMouseCursor_ResizeNS: return sf::Cursor::Type::SizeVertical;
        case ImGuiMouseCursor_ResizeEW: return sf::Cursor::Type::SizeHorizontal;
        case ImGuiMouseCursor_ResizeNESW: return sf::Cursor::Type::SizeBottomLeftTopRight;
        case ImGuiMouseCursor_ResizeNWSE: return sf::Cursor::Type::SizeTopLeftBottomRight;
        case ImGuiMouseCursor_Hand: return sf::Cursor::Type::Hand;
        case ImGuiMouseCursor_NotAllowed: return sf::Cursor::Type::NotAllowed;
        default: return std::nullopt;
    }
}

}  // namespace

App::App(const Options& options)
    : options_(options),
      settingsFile_(userDataDir() / "settings.ini"),
      settings_(options.temporarySettings ? Settings{} : Settings::load(settingsFile_)),
      window_(sf::VideoMode({options.width, options.height}), "Chess") {
    configureWindow();

    imguiReady_ = ImGui::SFML::Init(window_, false);
    if (!imguiReady_) return;
    ImGui::GetIO().IniFilename = nullptr;
    if (!options_.playerNames.empty()) settings_.playerNames = options_.playerNames;
    if (options_.lightMode) settings_.lightMode = true;
    if (options_.fullscreen) settings_.fullscreen = true;
    if (options_.blindfold) settings_.blindfold = true;
    if (options_.moveBadges) settings_.playMoveBadges = true;
    if (!options_.compareEngine.empty()) settings_.compareEnginePath = options_.compareEngine;
    if (!options_.syzygyPath.empty()) settings_.syzygyPath = options_.syzygyPath;
    windowedSize_ = sf::Vector2u{options_.width, options_.height};
    setFontDensity(pixelsPerPoint_);
    applyAppearance();

    const std::string missing = assets_.load(findUpwards("data"));
    if (!missing.empty()) assetWarning_ = "Missing data files: " + missing;
    if (assets_.hasBadges()) ui::setQualityBadgeSheet(assets_.badgesTexture());
    // The user's own sound effects; a file that is gone falls back to the built-in sound.
    assets_.setSound(SoundId::Move, pathFromUtf8(settings_.moveSoundFile));
    assets_.setSound(SoundId::Capture, pathFromUtf8(settings_.captureSoundFile));
    assets_.setSound(SoundId::GameEnd, pathFromUtf8(settings_.gameEndSoundFile));

    const std::filesystem::path databaseFile = databasePath(options_.temporarySettings);
    std::string storeError;
    store_ = storage::GameStore::open(databaseFile, &storeError);

    play_ = std::make_unique<ui::PlayScreen>(settings_, assets_, store_.get());
    play_->setAutoplay(options_.autoplay);
    for (int i = 0; i < options_.hintLevel && i < 2; ++i) play_->requestHint();
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
    // Opened after the game store: the review queries read its table.
    if (store_) reviewStore_ = storage::ReviewStore::open(databaseFile);
    review_ = std::make_unique<ui::ReviewScreen>(settings_, assets_, store_.get(), reviewStore_.get(), &openingBook_);
    statistics_ = std::make_unique<ui::StatisticsScreen>(settings_, reviewStore_.get(), store_.get());
    settingsScreen_ = std::make_unique<ui::SettingsScreen>(settings_, assets_, presence_);

    if (options_.openSettings) section_ = Section::Settings;
    if (options_.openAnalysis) section_ = Section::Analysis;
    if (options_.openLibrary) section_ = Section::Library;
    if (options_.openPuzzles) section_ = Section::Puzzles;
    if (options_.openOpenings) section_ = Section::Openings;
    if (options_.openEndgames) section_ = Section::Endgames;
    if (options_.openReview) section_ = Section::Review;
    if (options_.openStatistics) section_ = Section::Statistics;
    if (!options_.pgnFile.empty()) {
        std::ifstream input(pathFromUtf8(options_.pgnFile), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (std::optional<core::GameRecord> record = core::parsePgn(text)) {
            if (options_.openReview) review_->load(*record);
            analysis_->load(std::move(*record));
        }
    }
    if (!options_.fen.empty() && core::validateFen(options_.fen)) {
        analysis_->load(core::GameRecord{{}, core::GameTree(options_.fen)});
    }
    analysis_->setOverlay(options_.overlay);
    if (options_.makePuzzles) review_->requestPuzzles();
    if (options_.ply >= 0) analysis_->goToPly(options_.ply);
    if (options_.ply >= 0) review_->goToPly(options_.ply);
    if (options_.analyseGame) analysis_->requestGameAnalysis();
    if (!options_.importFile.empty() && store_) store_->importFile(pathFromUtf8(options_.importFile));
    if (options_.reviewAll) review_->reviewAll();
}

App::~App() {
    saveSettings();
    trackUsage(true);
    // Screens hold engine processes and textures; release them before the window goes away.
    settingsScreen_.reset();
    statistics_.reset();
    review_.reset();
    library_.reset();
    puzzles_.reset();
    openings_.reset();
    endgames_.reset();
    analysis_.reset();
    play_.reset();
    reviewStore_.reset();
    store_.reset();
    puzzleStore_.reset();
    trainingStore_.reset();
    if (imguiReady_) ImGui::SFML::Shutdown();
}

// Moves games and requests between the screens, which do not know each other.
void App::routeRequests() {
    if (play_->consumeSettingsChanged() || analysis_->consumeSettingsChanged()) saveSettings();
    if (statistics_->consumeSettingsChanged() || puzzles_->consumeSettingsChanged()) saveSettings();

    // Puzzles made in Game Review go into the puzzle collection.
    if (const std::optional<std::vector<core::Puzzle>> made = review_->takeGeneratedPuzzles()) {
        int added = 0;
        if (puzzleStore_) {
            for (const core::Puzzle& puzzle : *made) {
                if (!puzzleStore_->get(puzzle.id) && puzzleStore_->add(puzzle)) ++added;
            }
        }
        review_->onPuzzlesStored(static_cast<int>(made->size()), added);
    }
    if (play_->consumeLibraryChanged()) {
        library_->markDirty();
        review_->markDirty();
        statistics_->markDirty();  // the time played
    }
    if (library_->consumeChanged()) {
        review_->markDirty();
        statistics_->markDirty();
    }
    if (review_->consumeReviewsChanged()) statistics_->markDirty();
    if (statistics_->consumeReviewRequest()) section_ = Section::Review;

    // Games sent to Game Review.
    if (std::optional<ui::PlayScreen::ReviewRequest> request = play_->takeReviewRequest()) {
        review_->load(std::move(request->record), request->libraryId);
        section_ = Section::Review;
    }
    if (std::optional<ui::LibraryScreen::OpenRequest> request = library_->takeReviewRequest()) {
        review_->load(std::move(request->record), request->id);
        section_ = Section::Review;
    }
    if (std::optional<ui::ReviewScreen::AnalysisRequest> request = review_->takeAnalysisRequest()) {
        analysis_->load(std::move(request->record), request->libraryId);
        section_ = Section::Analysis;
    }

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
        review_->markDirty();
    }
}

void App::updatePresence() {
    // Automated test runs stay out of the user's Discord status unless asked.
    presence_.setEnabled(settings_.discordPresence && (!options_.temporarySettings || options_.forceDiscord));

    PresenceText text;
    switch (section_) {
        case Section::Play: text = play_->presence(); break;
        case Section::Analysis: text = analysis_->presence(); break;
        case Section::Review: text = review_->presence(); break;
        case Section::Statistics:
            text.details = "Looking at statistics";
            break;
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

void App::applyAppearance() {
    appliedLightMode_ = settings_.lightMode;
    applyTheme(settings_.lightMode);
    ui::setColorScheme(settings_.lightMode);
}

// Switches between the window and fullscreen. The window is created anew, so this must
// happen between frames.
void App::applyWindowMode() {
    liveResize_.remove();
    if (settings_.fullscreen) {
        // Window sizes are given in points; getSize() may be in pixels.
        if (!appliedFullscreen_) windowedSize_ = sf::Vector2u(sf::Vector2f(window_.getSize()) / pixelsPerPoint_);
        window_.create(sf::VideoMode::getDesktopMode(), "Chess", sf::State::Fullscreen);
    } else {
        window_.create(sf::VideoMode(windowedSize_), "Chess", sf::Style::Default, sf::State::Windowed);
    }
    appliedFullscreen_ = settings_.fullscreen;
    configureWindow();
    lastCursor_ = ImGuiMouseCursor_COUNT;  // the new window starts with the default cursor
    liveResize_.install(reinterpret_cast<void*>(window_.getNativeHandle()), [this] { frame(); });
}

void App::configureWindow() {
    window_.setVerticalSyncEnabled(true);
    // Before high DPI is on: SFML hands the minimum to macOS in points but also clamps the
    // current size against it, which goes wrong once sizes are in pixels.
    window_.setMinimumSize(sf::Vector2u{900u, 600u});
    platform::enableHighDpi(reinterpret_cast<void*>(window_.getNativeHandle()));
    pixelsPerPoint_ = platform::windowScaleFactor(reinterpret_cast<void*>(window_.getNativeHandle()));
}

void App::beginImGuiFrame() {
    // The scale changes when the window moves between a Retina and a normal display.
    pixelsPerPoint_ = platform::windowScaleFactor(reinterpret_cast<void*>(window_.getNativeHandle()));
    setFontDensity(pixelsPerPoint_);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplayFramebufferScale = ImVec2(pixelsPerPoint_, pixelsPerPoint_);
    if (pixelsPerPoint_ == 1.0f) {
        ImGui::SFML::Update(window_, frameClock_.restart());
        return;
    }
    // imgui-SFML reads the size and mouse position in window pixels; hand it points instead.
    // This overload leaves the mouse cursor alone, so that is done here.
    updateMouseCursor();
    const sf::Vector2f mouse = sf::Vector2f(sf::Mouse::getPosition(window_)) / pixelsPerPoint_;
    ImGui::SFML::Update(sf::Vector2i(mouse), sf::Vector2f(window_.getSize()) / pixelsPerPoint_, frameClock_.restart());
}

void App::updateMouseCursor() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) return;
    const ImGuiMouseCursor cursor = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
    if (cursor == lastCursor_) return;
    lastCursor_ = cursor;
    if (cursor == ImGuiMouseCursor_None) {
        window_.setMouseCursorVisible(false);
        return;
    }
    window_.setMouseCursorVisible(true);
    std::optional<sf::Cursor>& slot = cursors_[static_cast<std::size_t>(cursor)];
    if (!slot) slot = sf::Cursor::createFromSystem(systemCursor(cursor).value_or(sf::Cursor::Type::Arrow));
    if (slot) window_.setMouseCursor(*slot);
}

void App::renderImGui() {
    ImGui::Render();
    // imgui-SFML scales clip rectangles by the framebuffer scale twice. Undo one of them;
    // its own ImGui::Render() call below then does nothing, as the frame is already rendered.
    if (pixelsPerPoint_ != 1.0f) {
        ImGui::GetDrawData()->ScaleClipRects(ImVec2(1.0f / pixelsPerPoint_, 1.0f / pixelsPerPoint_));
    }
    ImGui::SFML::Render(window_);
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
            ImGui::SFML::ProcessEvent(window_, toPoints(*event, pixelsPerPoint_));
            if (event->is<sf::Event::Closed>()) window_.close();
            if (const auto* key = event->getIf<sf::Event::KeyPressed>(); key != nullptr && key->code == sf::Keyboard::Key::F11) {
                settings_.fullscreen = !settings_.fullscreen;
                saveSettings();
            }
        }
        if (!window_.isOpen()) break;
        if (settings_.fullscreen != appliedFullscreen_) applyWindowMode();

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

void App::trackUsage(bool flush) {
    const auto now = std::chrono::steady_clock::now();
    // A long gap means the machine slept; that does not count.
    usagePending_ += std::min(std::chrono::duration<double>(now - usageTick_).count(), 60.0);
    usageTick_ = now;
    if (!store_ || options_.temporarySettings || (!flush && usagePending_ < 30.0)) return;
    const auto whole = static_cast<std::int64_t>(usagePending_);
    store_->addUsage(whole);
    usagePending_ -= static_cast<double>(whole);
    if (statistics_) statistics_->markDirty();
}

void App::frame() {
    trackUsage(false);
    const sf::Vector2u size = window_.getSize();
    if (size.x == 0 || size.y == 0) return;  // minimized

    beginImGuiFrame();

    play_->update();
    analysis_->update(section_ == Section::Analysis);
    library_->update();
    review_->update();
    if (section_ == Section::Puzzles) puzzles_->update();
    if (section_ == Section::Openings) openings_->update();
    routeRequests();
    updatePresence();

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sidebarWidth = 190.0f;
    drawSidebar(sidebarWidth, display.y);
    drawContent(sidebarWidth, display.x - sidebarWidth, display.y);

    window_.clear(settings_.lightMode ? sf::Color(237, 235, 231) : sf::Color(22, 21, 18));
    renderImGui();
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
    } else if (section_ == Section::Review) {
        review_->draw();
    } else if (section_ == Section::Statistics) {
        statistics_->draw();
    } else if (section_ == Section::Settings) {
        const ui::SettingsScreen::Changes changes = settingsScreen_->draw();
        if (changes.any) {
            saveSettings();
            if (settings_.lightMode != appliedLightMode_) applyAppearance();
            statistics_->markDirty();
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
