#pragma once

#include <string>
#include <vector>

#include "chess/GameTree.h"
#include "chess/Review.h"

namespace core {

// Plain-language sentences about the move leading to `id`, built from the positions and
// the engine results stored in the tree: what the move does, which threats it creates,
// what it allows when it is weak, and why the engine's alternative was stronger.
// The rules are simple and concrete (captures, checks, loose pieces, mates); they describe
// what can be seen on the board rather than deep plans. Returns nothing for the root.
std::vector<std::string> explainMove(const GameTree& tree, NodeId id, MoveQuality quality);

}  // namespace core
