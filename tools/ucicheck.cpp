// Console check for the engine module: starts a UCI engine, asks for a move
// from the start position and prints what comes back.
//
//   ucicheck <path-to-engine> [movetime-ms]

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "engine/UciEngine.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: ucicheck <path-to-engine> [movetime-ms]\n");
        return 2;
    }
    const int moveTime = argc > 2 ? std::atoi(argv[2]) : 500;

    engine::UciEngine uci;
    std::string error;
    if (!uci.start(argv[1], &error)) {
        std::fprintf(stderr, "start failed: %s\n", error.c_str());
        return 1;
    }

    const char* startFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    int searchId = 0;
    int lastDepth = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

    while (std::chrono::steady_clock::now() < deadline) {
        for (const engine::EngineEvent& event : uci.poll()) {
            switch (event.type) {
                case engine::EngineEvent::Type::Ready: {
                    std::printf("ready: %s\n", uci.name().c_str());
                    if (const auto elo = uci.option("UCI_Elo"))
                        std::printf("UCI_Elo range: %d..%d\n", elo->min.value_or(0), elo->max.value_or(0));
                    uci.newGame();
                    engine::SearchLimits limits;
                    limits.moveTimeMs = moveTime;
                    searchId = uci.search(startFen, "e2e4 e7e5", limits);
                    break;
                }
                case engine::EngineEvent::Type::Info:
                    if (event.searchId == searchId && event.info.depth != lastDepth) {
                        lastDepth = event.info.depth;
                        std::printf("depth %d score %d pv %s\n", event.info.depth, event.info.scoreCp.value_or(0),
                                    event.info.pv.c_str());
                    }
                    break;
                case engine::EngineEvent::Type::BestMove:
                    std::printf("bestmove %s (search %d)\n", event.bestMove.c_str(), event.searchId);
                    return event.searchId == searchId && event.bestMove.size() >= 4 ? 0 : 1;
                case engine::EngineEvent::Type::Terminated:
                    std::fprintf(stderr, "engine terminated: %s\n", event.message.c_str());
                    return 1;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::fprintf(stderr, "timed out waiting for the engine\n");
    return 1;
}
