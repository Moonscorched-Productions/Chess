#include "chess/OpeningBook.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include "chess/Repertoire.h"

namespace core {

int OpeningBook::loadText(std::string_view tsv) {
    int added = 0;
    Game game;
    game.setEnforceDraws(false);

    std::size_t pos = 0;
    while (pos < tsv.size()) {
        const std::size_t end = std::min(tsv.find('\n', pos), tsv.size());
        std::string_view line = tsv.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        const std::size_t tab1 = line.find('\t');
        const std::size_t tab2 = tab1 == std::string_view::npos ? tab1 : line.find('\t', tab1 + 1);
        if (tab2 == std::string_view::npos) continue;
        const std::string_view eco = line.substr(0, tab1);
        const std::string_view name = line.substr(tab1 + 1, tab2 - tab1 - 1);
        std::string_view moves = line.substr(tab2 + 1);
        if (const std::size_t tab3 = moves.find('\t'); tab3 != std::string_view::npos) moves = moves.substr(0, tab3);
        if (eco == "eco" || moves.empty()) continue;  // header

        // Play the moves ("1. e4 e5 2. Nf3"), skipping the move numbers.
        game.reset();
        bool ok = true;
        std::size_t m = 0;
        while (ok && m < moves.size()) {
            while (m < moves.size() && moves[m] == ' ') ++m;
            std::size_t tokenEnd = m;
            while (tokenEnd < moves.size() && moves[tokenEnd] != ' ') ++tokenEnd;
            const std::string_view token = moves.substr(m, tokenEnd - m);
            m = tokenEnd;
            if (token.empty() || token.back() == '.') continue;
            ok = game.makeSanMove(token);
        }
        if (!ok || game.ply() == 0) continue;

        OpeningLine opening{std::string(eco), std::string(name), {}, std::string(moves)};
        for (const MoveRecord& record : game.history()) opening.moves.push_back(record.uci);
        lines_.push_back(std::move(opening));

        // Later (longer) tables may refine a name; keep the first one seen for a position.
        if (names_.try_emplace(positionKey(game.fen()), OpeningName{std::string(eco), std::string(name)}).second) ++added;
    }
    return added;
}

int OpeningBook::load(const std::filesystem::path& directory) {
    int added = 0;
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".tsv") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& file : files) {
        std::ifstream input(file, std::ios::binary);
        if (!input) continue;
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        added += loadText(text);
    }
    return added;
}

const OpeningName* OpeningBook::find(std::string_view fen) const {
    const auto it = names_.find(positionKey(fen));
    return it == names_.end() ? nullptr : &it->second;
}

const OpeningName* OpeningBook::identify(const GameTree& tree, NodeId id) const {
    if (names_.empty()) return nullptr;
    for (NodeId current = id; tree.contains(current) && current != kRootNode; current = tree.node(current).parent) {
        if (const OpeningName* name = find(tree.node(current).fen)) return name;
    }
    return nullptr;
}

}  // namespace core
