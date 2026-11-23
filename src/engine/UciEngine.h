#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

struct SearchLimits {
    int moveTimeMs = 0;  // 0 = no time limit
    int depth = 0;       // 0 = no depth limit
    // Clock times for a game with a time control (0 = not used); the engine budgets its time.
    int whiteTimeMs = 0;
    int blackTimeMs = 0;
    int whiteIncrementMs = 0;
    int blackIncrementMs = 0;
    // With no limit at all the search runs until stopSearch() is called.
};

// One "info" line from the engine. Scores are from the point of view of the side to move.
struct SearchInfo {
    int depth = 0;
    int multiPv = 1;
    std::optional<int> scoreCp;
    std::optional<int> mateIn;  // positive: side to move mates, negative: gets mated
    bool isBound = false;       // the score is only a lower or upper bound, not exact
    std::uint64_t nodes = 0;
    std::uint64_t nodesPerSecond = 0;
    std::uint64_t tbHits = 0;  // positions found in endgame tablebases
    std::string pv;  // space separated UCI moves
};

struct EngineEvent {
    enum class Type {
        Ready,       // handshake finished, the engine accepts commands
        Info,        // search progress
        BestMove,    // search finished
        Terminated,  // the engine process exited or could not be reached
    };
    Type type = Type::Info;
    int searchId = 0;      // Info, BestMove: which search() call this belongs to
    SearchInfo info;       // Info
    std::string bestMove;  // BestMove: UCI move, or "(none)" when there is no legal move
    std::string message;   // Terminated: reason
};

struct EngineOption {
    std::string name;
    std::string type;  // "spin", "check", "combo", "string", "button"
    std::string defaultValue;
    std::optional<int> min;
    std::optional<int> max;
};

// Parses a single UCI "info" line. Returns nullopt for lines that carry no
// search data (for example "info string ...").
std::optional<SearchInfo> parseInfoLine(std::string_view line);

// Parses a UCI "option name ... type ..." line.
std::optional<EngineOption> parseOptionLine(std::string_view line);

// Asynchronous client for a UCI chess engine running as a child process.
//
// All methods are called from one thread (the application's main thread) and
// return immediately. Engine output is read on a background thread and handed
// over through poll().
class UciEngine {
public:
    enum class State { Stopped, Starting, Ready, Failed };

    UciEngine();
    ~UciEngine();
    UciEngine(const UciEngine&) = delete;
    UciEngine& operator=(const UciEngine&) = delete;

    // Launches the engine and begins the UCI handshake. A Ready event follows.
    bool start(const std::filesystem::path& executable, std::string* error = nullptr);
    // Asks the engine to quit and releases the process.
    void shutdown();

    State state() const;
    std::string name() const;
    std::optional<EngineOption> option(std::string_view name) const;

    void setOption(std::string_view name, std::string_view value);
    void newGame();

    // Starts searching the position reached from `startFen` after `uciMoves`
    // (space separated, may be empty). Any search still running is stopped first.
    // Returns the id carried by the resulting Info and BestMove events.
    int search(std::string_view startFen, std::string_view uciMoves, const SearchLimits& limits);
    // Stops the current search; its BestMove event still arrives.
    void stopSearch();
    bool searching() const;

    // Returns the events received since the last call. Never blocks.
    std::vector<EngineEvent> poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine
