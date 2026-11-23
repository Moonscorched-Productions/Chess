#include "chess/Pgn.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <istream>
#include <vector>

namespace core {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

std::string_view trim(std::string_view text) {
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    return text;
}

// Collapses runs of whitespace (including line breaks) into single spaces.
std::string normalizeSpaces(std::string_view text) {
    std::string result;
    bool pendingSpace = false;
    for (const char c : trim(text)) {
        if (isSpace(c)) {
            pendingSpace = true;
        } else {
            if (pendingSpace && !result.empty()) result += ' ';
            pendingSpace = false;
            result += c;
        }
    }
    return result;
}

bool fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

// Pulls a "[%eval 0.35]" / "[%eval #-4,18]" command out of a comment.
std::optional<Eval> extractEval(std::string& comment) {
    const std::size_t start = comment.find("[%eval ");
    if (start == std::string::npos) return std::nullopt;
    const std::size_t end = comment.find(']', start);
    if (end == std::string::npos) return std::nullopt;

    std::string_view body = trim(std::string_view(comment).substr(start + 7, end - start - 7));
    std::string_view depthText;
    if (const std::size_t comma = body.find(','); comma != std::string_view::npos) {
        depthText = trim(body.substr(comma + 1));
        body = trim(body.substr(0, comma));
    }

    std::optional<Eval> eval;
    if (!body.empty() && body.front() == '#') {
        int mate = 0;
        const std::string_view number = body.substr(1);
        const auto [ptr, ec] = std::from_chars(number.data(), number.data() + number.size(), mate);
        if (ec == std::errc() && ptr == number.data() + number.size() && mate != 0) {
            eval = Eval{};
            eval->mate = mate;
        }
    } else if (!body.empty()) {
        const std::string number(body);
        char* parsedEnd = nullptr;
        const double pawns = std::strtod(number.c_str(), &parsedEnd);
        if (parsedEnd != number.c_str() && *parsedEnd == '\0' && std::isfinite(pawns)) {
            eval = Eval{};
            eval->cp = static_cast<int>(std::lround(std::clamp(pawns, -1000.0, 1000.0) * 100.0));
        }
    }
    if (eval && !depthText.empty()) {
        int depth = 0;
        const auto [ptr, ec] = std::from_chars(depthText.data(), depthText.data() + depthText.size(), depth);
        if (ec == std::errc() && depth > 0) eval->depth = depth;
        (void)ptr;
    }

    comment.erase(start, end - start + 1);
    comment = normalizeSpaces(comment);
    return eval;
}

// Pulls a "[%best e2e4]" command (the engine's preferred move in the position) out of a comment.
std::string extractBestMove(std::string& comment) {
    const std::size_t start = comment.find("[%best ");
    if (start == std::string::npos) return {};
    const std::size_t end = comment.find(']', start);
    if (end == std::string::npos) return {};
    const std::string move(trim(std::string_view(comment).substr(start + 7, end - start - 7)));
    comment.erase(start, end - start + 1);
    comment = normalizeSpaces(comment);
    const bool plausible = (move.size() == 4 || move.size() == 5) &&
                           std::all_of(move.begin(), move.end(), [](unsigned char c) { return std::isalnum(c) != 0; });
    return plausible ? move : std::string();
}

std::string formatEval(const Eval& eval) {
    char buffer[48];
    if (eval.mate) {
        std::snprintf(buffer, sizeof(buffer), "[%%eval #%d", *eval.mate);
    } else {
        std::snprintf(buffer, sizeof(buffer), "[%%eval %.2f", static_cast<double>(eval.cp) / 100.0);
    }
    std::string text = buffer;
    if (eval.depth > 0) text += "," + std::to_string(eval.depth);
    text += ']';
    return text;
}

int nagForSuffix(std::string_view suffix) {
    if (suffix == "!") return 1;
    if (suffix == "?") return 2;
    if (suffix == "!!") return 3;
    if (suffix == "??") return 4;
    if (suffix == "!?") return 5;
    if (suffix == "?!") return 6;
    return 0;
}

// --- Parser -------------------------------------------------------------------

class Parser {
public:
    Parser(std::string_view text, std::string* error) : text_(text), error_(error) {}

    std::optional<GameRecord> run() {
        std::vector<std::pair<std::string, std::string>> tags;
        if (!parseTags(tags)) return std::nullopt;

        std::string startFen(kStartFen);
        for (const auto& [name, value] : tags) {
            if (name == "FEN" && !value.empty()) startFen = value;
        }
        std::string fenError;
        if (!validateFen(startFen, &fenError)) {
            fail(error_, "Invalid start position: " + fenError);
            return std::nullopt;
        }

        GameRecord record{std::move(tags), GameTree(startFen)};
        if (!parseMoves(record)) return std::nullopt;

        if (record.tags.empty() && record.tree.moveCount() == 0) {
            fail(error_, "No game found");
            return std::nullopt;
        }
        if (record.tag("Result").empty()) record.setTag("Result", result_.empty() ? "*" : result_);
        return record;
    }

private:
    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return text_[pos_]; }

    void skipSpace() {
        while (!atEnd()) {
            if (isSpace(peek())) {
                ++pos_;
            } else if (peek() == '%' && (pos_ == 0 || text_[pos_ - 1] == '\n')) {
                skipLine();  // escape line
            } else {
                break;
            }
        }
    }

    void skipLine() {
        while (!atEnd() && peek() != '\n') ++pos_;
    }

    bool parseTags(std::vector<std::pair<std::string, std::string>>& tags) {
        for (;;) {
            skipSpace();
            if (atEnd() || peek() != '[' || pos_ + 1 >= text_.size() || !isAlpha(text_[pos_ + 1])) return true;
            ++pos_;
            std::string name;
            while (!atEnd() && !isSpace(peek()) && peek() != '"' && peek() != ']') name += text_[pos_++];
            while (!atEnd() && peek() != '"' && peek() != ']' && peek() != '\n') ++pos_;
            std::string value;
            if (!atEnd() && peek() == '"') {
                ++pos_;
                while (!atEnd() && peek() != '"' && peek() != '\n') {
                    if (peek() == '\\' && pos_ + 1 < text_.size() && (text_[pos_ + 1] == '"' || text_[pos_ + 1] == '\\')) ++pos_;
                    value += text_[pos_++];
                }
            }
            while (!atEnd() && peek() != ']' && peek() != '\n') ++pos_;
            if (!atEnd() && peek() == ']') ++pos_;
            if (!name.empty()) tags.emplace_back(std::move(name), std::move(value));
        }
    }

    void addComment(GameRecord& record, NodeId node, std::string_view raw) {
        std::string comment = normalizeSpaces(raw);
        if (const std::optional<Eval> eval = extractEval(comment)) record.tree.setEval(node, eval);
        if (std::string best = extractBestMove(comment); !best.empty()) record.tree.setBestMove(node, std::move(best));
        if (comment.empty()) return;
        const std::string& existing = record.tree.node(node).comment;
        record.tree.setComment(node, existing.empty() ? comment : existing + " " + comment);
    }

    bool parseMoves(GameRecord& record) {
        Game game;
        game.setEnforceDraws(false);
        if (!game.reset(record.tree.startFen())) return fail(error_, "Invalid start position");

        NodeId current = kRootNode;
        std::vector<NodeId> stack;

        for (;;) {
            skipSpace();
            if (atEnd()) return true;
            const char c = peek();

            if (c == '{') {
                const std::size_t end = text_.find('}', pos_);
                const std::size_t stop = end == std::string_view::npos ? text_.size() : end;
                addComment(record, current, text_.substr(pos_ + 1, stop - pos_ - 1));
                pos_ = end == std::string_view::npos ? text_.size() : end + 1;
            } else if (c == ';') {
                const std::size_t start = pos_ + 1;
                skipLine();
                addComment(record, current, text_.substr(start, pos_ - start));
            } else if (c == '(') {
                ++pos_;
                if (current == kRootNode) return fail(error_, "A variation appears before the first move");
                stack.push_back(current);
                current = record.tree.node(current).parent;
                if (!loadPosition(record.tree, current, game)) return fail(error_, "Could not replay the game");
            } else if (c == ')') {
                ++pos_;
                if (stack.empty()) continue;  // stray bracket
                current = stack.back();
                stack.pop_back();
                if (!loadPosition(record.tree, current, game)) return fail(error_, "Could not replay the game");
            } else if (c == '$') {
                ++pos_;
                int nag = 0;
                while (!atEnd() && isDigit(peek())) nag = std::min(nag * 10 + (text_[pos_++] - '0'), 999);
                if (current != kRootNode && nag > 0) {
                    std::vector<int> nags = record.tree.node(current).nags;
                    nags.push_back(nag);
                    record.tree.setNags(current, std::move(nags));
                }
            } else if (c == '*') {
                ++pos_;
                result_ = "*";
                return true;
            } else if (c == '[') {
                return true;  // the next game starts here
            } else if (isDigit(c) && !startsWith("0-0") && !startsWith("O-O")) {
                for (const std::string_view result : {"1-0", "0-1", "1/2-1/2"}) {
                    if (startsWith(result)) {
                        pos_ += result.size();
                        result_ = std::string(result);
                        return true;
                    }
                }
                while (!atEnd() && isDigit(peek())) ++pos_;  // move number
                while (!atEnd() && peek() == '.') ++pos_;
            } else if (isAlpha(c) || c == '0') {
                std::string token;
                while (!atEnd() && !isSpace(peek()) && std::string_view("(){};$").find(peek()) == std::string_view::npos)
                    token += text_[pos_++];

                std::size_t sanEnd = token.size();
                while (sanEnd > 0 && (token[sanEnd - 1] == '!' || token[sanEnd - 1] == '?')) --sanEnd;
                const std::string suffix = token.substr(sanEnd);
                std::string san = token.substr(0, sanEnd);
                if (san.starts_with("0-0")) {
                    for (char& ch : san)
                        if (ch == '0') ch = 'O';
                }

                if (!game.makeSanMove(san)) {
                    return fail(error_, "Illegal or unreadable move \"" + token + "\" at move " +
                                            std::to_string(game.fullMoveNumber()));
                }
                current = record.tree.addMove(current, game.history().back(), game.fen());
                if (const int nag = nagForSuffix(suffix); nag != 0) {
                    std::vector<int> nags = record.tree.node(current).nags;
                    nags.push_back(nag);
                    record.tree.setNags(current, std::move(nags));
                }
            } else {
                ++pos_;  // stray punctuation such as "." or "!"
            }
        }
    }

    bool startsWith(std::string_view prefix) const { return text_.substr(pos_).starts_with(prefix); }

    std::string_view text_;
    std::string* error_;
    std::size_t pos_ = 0;
    std::string result_;
};

// --- Writer -------------------------------------------------------------------

std::string escapeTag(std::string_view value) {
    std::string result;
    for (const char c : value) {
        if (c == '"' || c == '\\') result += '\\';
        if (c != '\n' && c != '\r') result += c;
    }
    return result;
}

class Writer {
public:
    explicit Writer(const GameTree& tree) : tree_(tree) {}

    std::vector<std::string> tokens(std::string_view result) {
        emitComment(kRootNode);
        if (!tree_.node(kRootNode).children.empty()) writeLine(tree_.node(kRootNode).children.front(), true);
        tokens_.emplace_back(result);
        return std::move(tokens_);
    }

private:
    // Returns true if something was written.
    bool emitComment(NodeId id) {
        const TreeNode& node = tree_.node(id);
        std::string text;
        // A checkmate position needs no evaluation ("mate in 0" has no PGN form).
        if (node.eval && !(node.eval->mate && *node.eval->mate == 0)) text = formatEval(*node.eval);
        if (!node.bestMove.empty()) {
            if (!text.empty()) text += ' ';
            text += "[%best " + node.bestMove + "]";
        }
        if (!node.comment.empty()) {
            if (!text.empty()) text += ' ';
            for (const char c : node.comment) text += c == '}' ? ')' : c == '{' ? '(' : c;
        }
        if (text.empty()) return false;

        tokens_.emplace_back("{");
        std::size_t pos = 0;
        while (pos < text.size()) {
            const std::size_t end = std::min(text.find(' ', pos), text.size());
            if (end > pos) tokens_.push_back(text.substr(pos, end - pos));
            pos = end + 1;
        }
        tokens_.emplace_back("}");
        return true;
    }

    void writeLine(NodeId first, bool needNumber) {
        NodeId id = first;
        for (;;) {
            const TreeNode& node = tree_.node(id);
            const bool white = tree_.mover(id) == Color::White;
            if (white) {
                tokens_.push_back(std::to_string(tree_.moveNumber(id)) + ".");
            } else if (needNumber) {
                tokens_.push_back(std::to_string(tree_.moveNumber(id)) + "...");
            }
            tokens_.push_back(node.record.san);
            for (const int nag : node.nags) tokens_.push_back("$" + std::to_string(nag));
            needNumber = emitComment(id);

            // Alternatives to this move follow it, each in brackets.
            const std::vector<NodeId>& siblings = tree_.node(node.parent).children;
            if (siblings.front() == id) {
                for (std::size_t i = 1; i < siblings.size(); ++i) {
                    tokens_.emplace_back("(");
                    writeLine(siblings[i], true);
                    tokens_.emplace_back(")");
                    needNumber = true;
                }
            }

            if (node.children.empty()) return;
            id = node.children.front();
        }
    }

    const GameTree& tree_;
    std::vector<std::string> tokens_;
};

}  // namespace

std::optional<GameRecord> parsePgn(std::string_view text, std::string* error) {
    // Skip a UTF-8 byte order mark.
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    return Parser(text, error).run();
}

std::string writePgn(const GameRecord& record) {
    std::string out;
    const auto writeTag = [&](std::string_view name, std::string_view value) {
        out += '[';
        out += name;
        out += " \"";
        out += escapeTag(value);
        out += "\"]\n";
    };

    // The seven standard tags come first, in their fixed order.
    constexpr std::array<std::string_view, 7> roster = {"Event", "Site", "Date", "Round", "White", "Black", "Result"};
    std::string result = record.tag("Result");
    if (result != "1-0" && result != "0-1" && result != "1/2-1/2") result = "*";
    for (const std::string_view name : roster) {
        std::string value = name == "Result" ? result : record.tag(name);
        if (value.empty()) value = name == "Date" ? "????.??.??" : "?";
        writeTag(name, value);
    }

    const bool customStart = record.tree.startFen() != kStartFen;
    for (const auto& [name, value] : record.tags) {
        if (std::find(roster.begin(), roster.end(), name) != roster.end()) continue;
        if (name == "FEN" || name == "SetUp") continue;
        writeTag(name, value);
    }
    if (customStart) {
        writeTag("SetUp", "1");
        writeTag("FEN", record.tree.startFen());
    }
    out += '\n';

    // Movetext, wrapped at 80 columns.
    std::size_t column = 0;
    for (const std::string& token : Writer(record.tree).tokens(result)) {
        if (column > 0 && column + 1 + token.size() > 80) {
            out += '\n';
            column = 0;
        } else if (column > 0) {
            out += ' ';
            ++column;
        }
        out += token;
        column += token.size();
    }
    out += '\n';
    return out;
}

bool PgnSplitter::next(std::string& game) {
    game.clear();
    bool sawMoves = false;
    bool sawAnything = false;

    std::string line;
    for (;;) {
        if (hasPendingLine_) {
            line = std::move(pendingLine_);
            hasPendingLine_ = false;
        } else if (!std::getline(input_, line)) {
            break;
        }

        const std::string_view content = trim(line);
        const bool isTag = content.size() > 1 && content.front() == '[' && isAlpha(content[1]);
        if (isTag && sawMoves) {
            // This tag opens the next game.
            pendingLine_ = std::move(line);
            hasPendingLine_ = true;
            break;
        }
        if (!content.empty()) {
            sawAnything = true;
            if (!isTag) sawMoves = true;
        }
        game += line;
        game += '\n';
    }
    return sawAnything;
}

std::string pgnDateToday() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d.%02d.%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return buffer;
}

}  // namespace core
