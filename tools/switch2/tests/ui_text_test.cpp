// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone host test for the game picker's display helpers (src/eden_horizon/ui_text.h,
// odd/tasks/b4-ui.md T1). They are pure string functions, shared by the device build.
//
// Build & run: tools/switch2/tests/run.sh ui_text_test

#include "eden_horizon/ui_text.h"

#include <cstdio>
#include <string>

using UiText::FileTypeLabel;
using UiText::TitleIdOf;
using UiText::GameDisplayName;
using UiText::SizeLabel;

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

void TestGameDisplayName() {
    // Dump tags in square brackets and the extension go away.
    CHECK(GameDisplayName("Little Racer [0100E6D00E81C000][v0][Base].nsp") == "Little Racer");
    CHECK(GameDisplayName("Super Mario 3D World + Bowsers Fury [010028600EBDA000] [v0].nsp") ==
          "Super Mario 3D World + Bowsers Fury");
    CHECK(GameDisplayName("Minecraft Dungeons 2 [0100A7C01B792000][v0][Base].xci") ==
          "Minecraft Dungeons 2");
    // Underscores become spaces, repeated spaces collapse, edges are trimmed.
    CHECK(GameDisplayName("my_game__v2.nro") == "my game v2");
    CHECK(GameDisplayName("  Spaced   Out  .nsp") == "Spaced Out");
    // Parentheses carry meaning (region, edition) and stay.
    CHECK(GameDisplayName("Game (USA).xci") == "Game (USA)");
    // Nothing left after cleaning: fall back to the file name without extension.
    CHECK(GameDisplayName("[0100E6D00E81C000].nsp") == "[0100E6D00E81C000]");
    // No extension, no tags.
    CHECK(GameDisplayName("Plain") == "Plain");
    CHECK(GameDisplayName("") == "");
    // An unclosed bracket is kept as text.
    CHECK(GameDisplayName("Odd [name.nsp") == "Odd [name");
}

void TestTitleIdOf() {
    CHECK(TitleIdOf("Little Racer [0100E6D00E81C000][v0][Base].nsp") == "0100e6d00e81c000");
    CHECK(TitleIdOf("Game [010028600EBDA000] [v0].nsp") == "010028600ebda000");
    CHECK(TitleIdOf("Minecraft Dungeons 2 [0100A7C01B792000][v0][Base].xci") == "0100a7c01b792000");
    // Too short, not hex, too long: no title id.
    CHECK(TitleIdOf("Game [0100E6D].nsp") == "");
    CHECK(TitleIdOf("Game [notatitleid0000].nsp") == "");
    CHECK(TitleIdOf("Game [0100E6D00E81C0000].nsp") == "");
    CHECK(TitleIdOf("Little Racer.nsp") == "");
    // Uppercase hex in the name still comes out lower case.
    CHECK(TitleIdOf("G [0100ABCDEF012345].nsp") == "0100abcdef012345");
}

void TestFileTypeLabel() {
    CHECK(FileTypeLabel("a.nsp") == "NSP");
    CHECK(FileTypeLabel("a.XCI") == "XCI");
    CHECK(FileTypeLabel("a.b.nro") == "NRO");
    CHECK(FileTypeLabel("noext") == "");
    CHECK(FileTypeLabel("trailing.") == "");
}

void TestSizeLabel() {
    CHECK(SizeLabel(0) == "0 B");
    CHECK(SizeLabel(1023) == "1023 B");
    CHECK(SizeLabel(1024) == "1 KB");
    CHECK(SizeLabel(1536) == "1.5 KB");
    CHECK(SizeLabel(318993344) == "304 MB");
    CHECK(SizeLabel(1662600274) == "1.5 GB");
    CHECK(SizeLabel(3086483216ULL) == "2.9 GB");
    CHECK(SizeLabel(10ULL * 1024 * 1024 * 1024) == "10 GB");
}

} // namespace

int main() {
    TestGameDisplayName();
    TestTitleIdOf();
    TestFileTypeLabel();
    TestSizeLabel();
    if (failures != 0) {
        std::printf("ui_text_test: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ui_text_test: all checks passed\n");
    return 0;
}
