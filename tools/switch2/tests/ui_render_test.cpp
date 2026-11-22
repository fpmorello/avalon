// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test and preview of Avalon's menu screens (src/eden_horizon/ui_render.cpp,
// odd/tasks/b4-ui.md T2). The renderer has no platform code, so the exact drawing the NRO
// does runs here. Checks that every screen draws inside the canvas (ASan) and puts its
// parts where expected; with UI_PREVIEW_DIR set it also writes each screen as a PPM image.
//
// Build & run: tools/switch2/tests/run.sh ui_render_test

#include "eden_horizon/ui_render.cpp" // one translation unit: run.sh builds a single file

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace UiRender;

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

struct Screen {
    // A stride wider than the row, as a real framebuffer may have, plus guard bytes.
    static constexpr int Stride = ScreenWidth * 4 + 64;
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(std::size_t(Stride) * ScreenHeight, 0xAB);
    Canvas canvas() {
        return {bytes.data(), ScreenWidth, ScreenHeight, Stride};
    }
    const std::uint8_t* At(int x, int y) const {
        return bytes.data() + std::size_t(y) * Stride + std::size_t(x) * 4;
    }
    /// Pixels in the box that differ from the box's first pixel.
    int Busy(int x0, int y0, int x1, int y1) const {
        const std::uint8_t* ref = At(x0, y0);
        int busy = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const std::uint8_t* p = At(x, y);
                busy += (std::abs(p[0] - ref[0]) + std::abs(p[1] - ref[1]) + std::abs(p[2] - ref[2])) > 40;
            }
        }
        return busy;
    }
    bool GuardsIntact() const {
        for (int y = 0; y < ScreenHeight; ++y) {
            for (int x = ScreenWidth * 4; x < Stride; ++x) {
                if (bytes[std::size_t(y) * Stride + std::size_t(x)] != 0xAB) {
                    return false;
                }
            }
        }
        return true;
    }
    void Save(const char* name) const {
        const char* dir = std::getenv("UI_PREVIEW_DIR");
        if (dir == nullptr) {
            return;
        }
        const std::string path = std::string{dir} + "/" + name + ".ppm";
        std::FILE* out = std::fopen(path.c_str(), "wb");
        if (out == nullptr) {
            return;
        }
        std::fprintf(out, "P6\n%d %d\n255\n", ScreenWidth, ScreenHeight);
        for (int y = 0; y < ScreenHeight; ++y) {
            for (int x = 0; x < ScreenWidth; ++x) {
                std::fwrite(At(x, y), 1, 3, out);
            }
        }
        std::fclose(out);
    }
};

PickerView SamplePicker(int count, int selected) {
    PickerView view{"sdmc:/switch/eden/roms/", {}, selected, 0};
    view.items.push_back({"saves", "", "", true});
    const char* names[] = {"Little Racer", "Minecraft Dungeons 2", "Super Mario 3D World + Bowsers Fury",
                           "Kirby and the Forgotten Land", "The Legend of Zelda: Echoes of Wisdom",
                           "Pokémon Légendes", "A game with a very long name that does not fit in one row of the list at all"};
    const char* sizes[] = {"304 MB", "1.5 GB", "2.9 GB", "5.4 GB", "6.1 GB", "13 GB", "1 GB"};
    for (int i = 1; i < count; ++i) {
        view.items.push_back({names[(i - 1) % 7], i % 3 == 0 ? "XCI" : "NSP", sizes[(i - 1) % 7], false});
    }
    view.top = std::max(0, selected - PickerVisibleRows + 1);
    return view;
}

void TestPicker() {
    Screen screen;
    SetBuildLabel("b4");
    DrawPicker(screen.canvas(), SamplePicker(8, 2));
    CHECK(screen.GuardsIntact());
    CHECK(screen.Busy(40, 18, 104, 82) > 800);   // logo
    CHECK(screen.Busy(120, 20, 260, 70) > 300);  // "Avalon" title
    CHECK(screen.Busy(40, 146, 1240, 146 + 9 * 54) > 5000); // list rows
    CHECK(screen.Busy(40, 668, 900, 710) > 300); // button hints
    // The selected row (index 2) is highlighted: its left edge differs from an unselected row's.
    CHECK(screen.At(60, 146 + 2 * 54 + 24)[2] != screen.At(60, 146 + 3 * 54 + 24)[2]);
    screen.Save("picker");
}

void TestPickerScrolled() {
    Screen screen;
    DrawPicker(screen.canvas(), SamplePicker(40, 30));
    CHECK(screen.GuardsIntact());
    CHECK(screen.Busy(1246, 146, 1256, 146 + 9 * 54) > 40); // scroll bar
    CHECK(screen.Busy(1100, 104, 1240, 140) > 50);          // "31 / 40"
    screen.Save("picker_scrolled");
}

void TestPickerEmpty() {
    Screen screen;
    DrawPicker(screen.canvas(), PickerView{"sdmc:/", {}, 0, 0});
    CHECK(screen.GuardsIntact());
    CHECK(screen.Busy(300, 290, 980, 370) > 300); // empty-folder message
    screen.Save("picker_empty");
}

void TestOptions() {
    Screen screen;
    OptionsView view{"Options for every game",
                     {{"Internal resolution", "1x"}, {"Console mode", "handheld"},
                      {"Show FPS and speed", "on"}, {"Address space", "full"},
                      {"GPU emulation", "accurate"}, {"Return to the game list", "on"}},
                     3,
                     "How much address space most games get. The table that covers it is only\n"
                     "allocated where the game maps memory, so the size costs almost nothing.\n\n"
                     "full     512 GiB, as on the console. What games expect.\n"
                     "game     256 GiB, with the console's layout.\n"
                     "compact  64 GiB, with a different layout. A few games abort when they\n"
                     "         start with it.\n\n"
                     "Start with full. The smaller ones remain for a game that misbehaves\n"
                     "with it: give that game its own value with Y."};
    DrawOptions(screen.canvas(), view);
    CHECK(screen.GuardsIntact());
    CHECK(screen.Busy(40, 156, 700, 156 + 6 * 42) > 2000); // option rows
    CHECK(screen.Busy(750, 220, 1220, 400) > 1000);        // help text
    screen.Save("options");

    // A selection past the rows draws no help title and does not read out of bounds.
    Screen out_of_range;
    view.selected = 99;
    DrawOptions(out_of_range.canvas(), view);
    CHECK(out_of_range.GuardsIntact());
}

void TestTinyCanvasIsClipped() {
    // Everything clips to the canvas: a canvas smaller than the layout must not overflow.
    std::vector<std::uint8_t> bytes(200 * 100 * 4, 0);
    const Canvas canvas{bytes.data(), 200, 100, 200 * 4};
    DrawPicker(canvas, SamplePicker(12, 5));
    OptionsView view{"x", {{"a", "b"}}, 0, "help"};
    DrawOptions(canvas, view);
    CHECK(bytes[0] != 0 || bytes[1] != 0 || bytes[2] != 0); // background drawn
}

} // namespace

int main() {
    TestPicker();
    TestPickerScrolled();
    TestPickerEmpty();
    TestOptions();
    TestTinyCanvasIsClipped();
    if (failures != 0) {
        std::printf("ui_render_test: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ui_render_test: all checks passed\n");
    return 0;
}
