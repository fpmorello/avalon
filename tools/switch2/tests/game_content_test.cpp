// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>

#include "eden_horizon/game_content_policy.h"

int main(int argc, char** argv) {
    using namespace GameContent;
    constexpr u64 game = 0x0100123456780000;
    static_assert(MatchesUpdate(game, game + 0x800));
    static_assert(!MatchesUpdate(game, game + 0x801));
    static_assert(!MatchesUpdate(game, game + 0x2800));
    static_assert(MatchesDlc(game, game + 0x1001));
    static_assert(MatchesDlc(game, game + 0x17ff));
    static_assert(!MatchesDlc(game, game + 0x800));
    static_assert(!MatchesDlc(game, game + 0x1000));
    static_assert(!MatchesDlc(game, game + 0x1801));
    static_assert(!MatchesDlc(game, game + 0x3001));

    const PackageId selected{game + 0x800, 42, "Update \"2\".nsz"};
    const PackageId other_version{selected.title_id, 43, selected.filename};
    const PackageId other_file{selected.title_id, selected.version, "same version.nsp"};
    const std::vector<PackageId> candidates{other_version, other_file, selected};
    assert(FindChoice(candidates, selected) == 3);
    assert(FindChoice({other_version, other_file}, selected) == 0);
    const PackageId dlc1{game + 0x1001, 0, "DLC Pack.nsp"};
    const PackageId dlc2{game + 0x1002, 0, "DLC Pack.nsp"};
    const std::vector<PackageId> saved{selected, dlc1, dlc2};
    std::stringstream file;
    WriteSelection(file, saved);
    file << "broken line\n";
    assert(ReadSelection(file) == saved);

    assert(argc == 2);
    const std::filesystem::path path = std::filesystem::path{argv[1]} / "game.content";
    const auto backup = path.string() + ".bak";
    assert(LoadSelection(path).empty());
    assert(SaveSelection(path, saved));
    assert(LoadSelection(path) == saved);
    std::filesystem::rename(path, backup); // Interrupted between moving old and publishing new.
    assert(LoadSelection(path) == saved);
    assert(SaveSelection(path, {dlc2}));
    assert(LoadSelection(path) == std::vector{dlc2});
    assert(!std::filesystem::exists(backup));
    assert(SaveSelection(path, {}));
    assert(LoadSelection(path).empty());
    assert(!std::filesystem::exists(backup));
}
