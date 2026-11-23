#include <doctest/doctest.h>

#include "engine/UciEngine.h"

using namespace engine;

TEST_CASE("UCI info lines") {
    SUBCASE("centipawn score with principal variation") {
        const auto info = parseInfoLine(
            "info depth 18 seldepth 24 multipv 2 score cp -31 nodes 1234567 nps 987654 hashfull 12 tbhits 0 time 1250 "
            "pv e7e5 g1f3 b8c6");
        REQUIRE(info.has_value());
        CHECK(info->depth == 18);
        CHECK(info->multiPv == 2);
        REQUIRE(info->scoreCp.has_value());
        CHECK(*info->scoreCp == -31);
        CHECK_FALSE(info->mateIn.has_value());
        CHECK(info->nodes == 1234567);
        CHECK(info->nodesPerSecond == 987654);
        CHECK(info->pv == "e7e5 g1f3 b8c6");
    }
    SUBCASE("mate score with bound") {
        const auto info = parseInfoLine("info depth 5 score mate -3 upperbound nodes 10 pv a1a2");
        REQUIRE(info.has_value());
        REQUIRE(info->mateIn.has_value());
        CHECK(*info->mateIn == -3);
        CHECK_FALSE(info->scoreCp.has_value());
        CHECK(info->pv == "a1a2");
        CHECK(info->isBound);
        CHECK_FALSE(parseInfoLine("info depth 5 score cp 10 pv a1a2")->isBound);
    }
    SUBCASE("lines without search data are ignored") {
        CHECK_FALSE(parseInfoLine("info string NNUE evaluation using nn-1234.nnue depth 5").has_value());
        CHECK_FALSE(parseInfoLine("bestmove e2e4").has_value());
        CHECK_FALSE(parseInfoLine("").has_value());
        CHECK_FALSE(parseInfoLine("info").has_value());
    }
    SUBCASE("malformed numbers do not crash") {
        const auto info = parseInfoLine("info depth x score cp y nodes z pv e2e4");
        REQUIRE(info.has_value());
        CHECK(info->depth == 0);
        CHECK_FALSE(info->scoreCp.has_value());
        CHECK(info->pv == "e2e4");
        CHECK_FALSE(parseInfoLine("info score cp").has_value());
    }
}

TEST_CASE("UCI option lines") {
    SUBCASE("spin option") {
        const auto option = parseOptionLine("option name UCI_Elo type spin default 1320 min 1320 max 3190");
        REQUIRE(option.has_value());
        CHECK(option->name == "UCI_Elo");
        CHECK(option->type == "spin");
        CHECK(option->defaultValue == "1320");
        CHECK(option->min == 1320);
        CHECK(option->max == 3190);
    }
    SUBCASE("name with spaces") {
        const auto option = parseOptionLine("option name Skill Level type spin default 20 min 0 max 20");
        REQUIRE(option.has_value());
        CHECK(option->name == "Skill Level");
        CHECK(option->max == 20);
    }
    SUBCASE("button and malformed lines") {
        const auto option = parseOptionLine("option name Clear Hash type button");
        REQUIRE(option.has_value());
        CHECK(option->name == "Clear Hash");
        CHECK(option->type == "button");
        CHECK_FALSE(parseOptionLine("option name type spin").has_value());
        CHECK_FALSE(parseOptionLine("option").has_value());
        CHECK_FALSE(parseOptionLine("id name Stockfish").has_value());
    }
}
