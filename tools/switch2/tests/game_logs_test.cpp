// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/horizon/game_logs.h"

#include <cassert>
#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
    assert(argc == 2);
    namespace fs = std::filesystem;
    using Common::Log::GameLogs;
    const fs::path root{argv[1]};
    for (int i = 1; i <= 11; ++i) {
        GameLogs run;
        const auto path = run.Begin(root, "Racer", "/roms/Racer.nsp");
        assert(!path.empty());
        run.Write(std::to_string(i));
        run.Flush();
        std::ifstream input{path};
        assert(std::string(std::istreambuf_iterator<char>{input}, {}) == std::to_string(i));
        if (i == 1) {
            std::ofstream{path.parent_path() / "notes.txt"} << "keep";
        }
    }
    GameLogs other;
    const auto distinct = other.Begin(root, "Racer", "/other/Racer.nsp");
    assert(!distinct.empty());
    for (const auto& game : fs::directory_iterator(root)) {
        if (game.path() == distinct.parent_path()) {
            continue;
        }
        int count = 0;
        int sum = 0;
        for (const auto& log : fs::directory_iterator(game)) {
            if (log.path().filename() == "notes.txt") {
                assert(fs::file_size(log) == 4);
                continue;
            }
            std::ifstream input{log.path()};
            int value{};
            input >> value;
            ++count;
            sum += value;
        }
        assert(count == 10 && sum == 65);
    }
    GameLogs capped;
    const auto capped_path = capped.Begin(root, "Verbose", "/roms/Verbose.nsp");
    assert(!capped_path.empty());
    assert(capped.Write(std::string(16 * 1024 * 1024, 'x')));
    assert(!capped.Write("launch error"));
    assert(fs::file_size(capped_path) == 16 * 1024 * 1024);

    GameLogs unavailable;
    assert(unavailable.Begin(distinct, "Racer", "test").empty());
    assert(!unavailable.Write("fallback"));
}
