#include "engine/UciEngine.h"

#include <atomic>
#include <charconv>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include "platform/Process.h"

namespace engine {

namespace {

std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        std::size_t end = pos;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t') ++end;
        if (end > pos) tokens.push_back(line.substr(pos, end - pos));
        pos = end;
    }
    return tokens;
}

template <typename T>
std::optional<T> parseNumber(std::string_view text) {
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return value;
}

std::string joinTokens(const std::vector<std::string_view>& tokens, std::size_t begin, std::size_t end) {
    std::string result;
    for (std::size_t i = begin; i < end && i < tokens.size(); ++i) {
        if (!result.empty()) result += ' ';
        result += tokens[i];
    }
    return result;
}

}  // namespace

std::optional<SearchInfo> parseInfoLine(std::string_view line) {
    const std::vector<std::string_view> tokens = tokenize(line);
    if (tokens.empty() || tokens[0] != "info") return std::nullopt;

    SearchInfo info;
    bool hasData = false;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string_view key = tokens[i];
        const auto next = [&]() -> std::string_view { return i + 1 < tokens.size() ? tokens[i + 1] : std::string_view(); };

        if (key == "string") {
            break;  // free text follows
        } else if (key == "depth") {
            if (const auto value = parseNumber<int>(next())) {
                info.depth = *value;
                hasData = true;
            }
            ++i;
        } else if (key == "multipv") {
            if (const auto value = parseNumber<int>(next())) info.multiPv = *value;
            ++i;
        } else if (key == "nodes") {
            if (const auto value = parseNumber<std::uint64_t>(next())) info.nodes = *value;
            ++i;
        } else if (key == "tbhits") {
            if (const auto value = parseNumber<std::uint64_t>(next())) info.tbHits = *value;
            ++i;
        } else if (key == "nps") {
            if (const auto value = parseNumber<std::uint64_t>(next())) info.nodesPerSecond = *value;
            ++i;
        } else if (key == "score") {
            if (i + 2 < tokens.size()) {
                const auto value = parseNumber<int>(tokens[i + 2]);
                if (value && tokens[i + 1] == "cp") {
                    info.scoreCp = *value;
                    hasData = true;
                } else if (value && tokens[i + 1] == "mate") {
                    info.mateIn = *value;
                    hasData = true;
                }
            }
            i += 2;
            if (i + 1 < tokens.size() && (tokens[i + 1] == "lowerbound" || tokens[i + 1] == "upperbound")) {
                info.isBound = true;
                ++i;
            }
        } else if (key == "pv") {
            info.pv = joinTokens(tokens, i + 1, tokens.size());
            hasData = true;
            break;  // the principal variation runs to the end of the line
        }
    }
    if (!hasData) return std::nullopt;
    return info;
}

std::optional<EngineOption> parseOptionLine(std::string_view line) {
    const std::vector<std::string_view> tokens = tokenize(line);
    if (tokens.size() < 5 || tokens[0] != "option" || tokens[1] != "name") return std::nullopt;

    // The name may contain spaces and runs until the "type" keyword.
    std::size_t typeIndex = 2;
    while (typeIndex < tokens.size() && tokens[typeIndex] != "type") ++typeIndex;
    if (typeIndex == 2 || typeIndex + 1 >= tokens.size()) return std::nullopt;

    EngineOption option;
    option.name = joinTokens(tokens, 2, typeIndex);
    option.type = std::string(tokens[typeIndex + 1]);
    for (std::size_t i = typeIndex + 2; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == "default") {
            option.defaultValue = std::string(tokens[i + 1]);
        } else if (tokens[i] == "min") {
            option.min = parseNumber<int>(tokens[i + 1]);
        } else if (tokens[i] == "max") {
            option.max = parseNumber<int>(tokens[i + 1]);
        }
    }
    return option;
}

// --- UciEngine::Impl --------------------------------------------------------------

struct UciEngine::Impl {
    std::unique_ptr<platform::Process> process;
    std::thread reader;

    // Shared with the reader thread.
    mutable std::mutex mutex;
    std::vector<EngineEvent> events;
    std::deque<int> pendingSearches;  // ids of searches whose bestmove has not arrived yet
    std::map<std::string, EngineOption, std::less<>> options;
    std::string name;
    std::atomic<State> state{State::Stopped};
    std::atomic<bool> shuttingDown{false};

    int nextSearchId = 1;  // main thread only

    void send(std::string_view command) {
        if (!process) return;
        std::string line(command);
        line += '\n';
        if (!process->write(line)) fail("Lost the connection to the engine");
    }

    void fail(const char* message) {
        if (shuttingDown.load()) return;
        State expected = state.load();
        while (expected != State::Failed && expected != State::Stopped) {
            if (state.compare_exchange_weak(expected, State::Failed)) {
                std::lock_guard lock(mutex);
                pendingSearches.clear();
                EngineEvent event;
                event.type = EngineEvent::Type::Terminated;
                event.message = message;
                events.push_back(std::move(event));
                return;
            }
        }
    }

    // Runs on the reader thread.
    void readLoop() {
        std::string buffer;
        while (process->read(buffer)) {
            std::size_t start = 0;
            for (;;) {
                const std::size_t end = buffer.find('\n', start);
                if (end == std::string::npos) break;
                std::string_view line(buffer.data() + start, end - start);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
                if (!line.empty()) handleLine(line);
                start = end + 1;
            }
            buffer.erase(0, start);
        }
        fail("The engine process exited");
    }

    // Runs on the reader thread.
    void handleLine(std::string_view line) {
        if (line.starts_with("info ")) {
            std::optional<SearchInfo> info = parseInfoLine(line);
            if (!info) return;
            std::lock_guard lock(mutex);
            if (pendingSearches.empty()) return;
            EngineEvent event;
            event.type = EngineEvent::Type::Info;
            event.searchId = pendingSearches.front();
            event.info = std::move(*info);
            events.push_back(std::move(event));
        } else if (line.starts_with("bestmove")) {
            const std::vector<std::string_view> tokens = tokenize(line);
            std::lock_guard lock(mutex);
            if (pendingSearches.empty()) return;
            EngineEvent event;
            event.type = EngineEvent::Type::BestMove;
            event.searchId = pendingSearches.front();
            pendingSearches.pop_front();
            event.bestMove = tokens.size() > 1 ? std::string(tokens[1]) : std::string("(none)");
            events.push_back(std::move(event));
        } else if (line.starts_with("id name ")) {
            std::lock_guard lock(mutex);
            name = std::string(line.substr(8));
        } else if (line.starts_with("option ")) {
            if (std::optional<EngineOption> option = parseOptionLine(line)) {
                std::lock_guard lock(mutex);
                std::string key = option->name;
                options.insert_or_assign(std::move(key), std::move(*option));
            }
        } else if (line == "readyok") {
            State expected = State::Starting;
            if (state.compare_exchange_strong(expected, State::Ready)) {
                std::lock_guard lock(mutex);
                EngineEvent event;
                event.type = EngineEvent::Type::Ready;
                events.push_back(std::move(event));
            }
        }
    }
};

// --- UciEngine --------------------------------------------------------------------

UciEngine::UciEngine() : impl_(std::make_unique<Impl>()) {}

UciEngine::~UciEngine() { shutdown(); }

bool UciEngine::start(const std::filesystem::path& executable, std::string* error) {
    shutdown();

    impl_->process = platform::Process::launch(executable, error);
    if (!impl_->process) {
        impl_->state = State::Failed;
        return false;
    }

    impl_->shuttingDown = false;
    impl_->state = State::Starting;
    impl_->reader = std::thread([impl = impl_.get()] { impl->readLoop(); });

    // The engine answers "uci" with its options and "isready" with "readyok",
    // in order, so both can be queued right away.
    impl_->send("uci");
    impl_->send("isready");
    return true;
}

void UciEngine::shutdown() {
    impl_->shuttingDown = true;
    if (impl_->process) {
        impl_->process->write("stop\nquit\n");
        if (!impl_->process->waitForExit(1000)) impl_->process->terminate();
    }
    // The process is gone, so the reader's blocking read has returned.
    if (impl_->reader.joinable()) impl_->reader.join();
    impl_->process.reset();

    std::lock_guard lock(impl_->mutex);
    impl_->events.clear();
    impl_->pendingSearches.clear();
    impl_->options.clear();
    impl_->name.clear();
    impl_->state = State::Stopped;
}

UciEngine::State UciEngine::state() const { return impl_->state.load(); }

std::string UciEngine::name() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->name;
}

std::optional<EngineOption> UciEngine::option(std::string_view name) const {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->options.find(name);
    if (it == impl_->options.end()) return std::nullopt;
    return it->second;
}

void UciEngine::setOption(std::string_view name, std::string_view value) {
    std::string command = "setoption name ";
    command += name;
    command += " value ";
    command += value;
    impl_->send(command);
}

void UciEngine::newGame() {
    stopSearch();
    impl_->send("ucinewgame");
}

int UciEngine::search(std::string_view startFen, std::string_view uciMoves, const SearchLimits& limits) {
    stopSearch();

    const int id = impl_->nextSearchId++;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->pendingSearches.push_back(id);
    }

    std::string position = "position fen ";
    position += startFen;
    if (!uciMoves.empty()) {
        position += " moves ";
        position += uciMoves;
    }
    impl_->send(position);

    std::string go = "go";
    if (limits.depth > 0) go += " depth " + std::to_string(limits.depth);
    if (limits.moveTimeMs > 0) go += " movetime " + std::to_string(limits.moveTimeMs);
    if (limits.depth <= 0 && limits.moveTimeMs <= 0) go += " infinite";
    impl_->send(go);
    return id;
}

void UciEngine::stopSearch() {
    if (searching()) impl_->send("stop");
}

bool UciEngine::searching() const {
    std::lock_guard lock(impl_->mutex);
    return !impl_->pendingSearches.empty();
}

std::vector<EngineEvent> UciEngine::poll() {
    std::lock_guard lock(impl_->mutex);
    return std::exchange(impl_->events, {});
}

}  // namespace engine
