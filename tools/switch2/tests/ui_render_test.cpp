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

/// A synthetic vertical cover: violet-to-teal gradient with a white frame.
UiImage::Image SyntheticCover(int width, int height) {
    UiImage::Image image;
    image.width = width;
    image.height = height;
    image.rgba.resize(std::size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t* p = image.rgba.data() + (std::size_t(y) * width + x) * 4;
            const float t = static_cast<float>(y) / (height - 1);
            p[0] = std::uint8_t(124 + (61 - 124) * t);
            p[1] = std::uint8_t(108 + (214 - 108) * t);
            p[2] = std::uint8_t(242 - (242 - 196) * t);
            p[3] = 255;
        }
    }
    return image;
}

void TestPicker() {
    Screen screen;
    SetBuildLabel("b5");
    const UiImage::Image cover = SyntheticCover(300, 400);
    PickerView view = SamplePicker(8, 2);
    view.cover = &cover;
    DrawPicker(screen.canvas(), view);
    CHECK(screen.GuardsIntact());
    CHECK(screen.Busy(40, 18, 104, 82) > 800);   // logo
    CHECK(screen.Busy(120, 20, 260, 70) > 300);  // "Avalon" title
    CHECK(screen.Busy(40, 146, 760, 146 + 9 * 54) > 4000); // list rows, left column
    CHECK(screen.Busy(776, 132, 1240, 652) > 30000);       // the selected game's cover
    CHECK(screen.Busy(40, 668, 900, 710) > 300); // button hints
    // The selected row (index 2) is highlighted: its left edge differs from an unselected row's.
    CHECK(screen.At(60, 146 + 2 * 54 + 24)[2] != screen.At(60, 146 + 3 * 54 + 24)[2]);
    screen.Save("picker");

    // Without a cover the same game shows the placeholder with the convention.
    Screen no_cover_screen;
    view.cover = nullptr;
    DrawPicker(no_cover_screen.canvas(), view);
    CHECK(no_cover_screen.Busy(790, 190, 1230, 640) > 400); // hint text
    no_cover_screen.Save("picker_nocover");

    // A folder selected: no cover and no placeholder.
    Screen folder_screen;
    view.selected = 0;
    DrawPicker(folder_screen.canvas(), view);
    CHECK(folder_screen.Busy(790, 190, 1230, 640) < 400);
    folder_screen.Save("picker_folder");
}

void TestPickerScrolled() {
    Screen screen;
    DrawPicker(screen.canvas(), SamplePicker(40, 30));
    CHECK(screen.GuardsIntact());
    CHECK(screen.Busy(752, 146, 760, 146 + 9 * 54) > 40); // scroll bar at the list edge
    CHECK(screen.Busy(560, 104, 752, 140) > 50);           // "31 / 40"
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

void TestOptionsScrollToSelectedValue() {
    Screen off;
    Screen on;
    OptionsView view{"Game options", std::vector<OptionRow>(20, {"Local DLC", "Off"}), 19, ""};
    DrawOptions(off.canvas(), view);
    view.rows.back().value = "v0  DLC.nsp";
    DrawOptions(on.canvas(), view);
    CHECK(off.bytes != on.bytes);
    CHECK(off.GuardsIntact() && on.GuardsIntact());
}

void TestLaunchError() {
    Screen missing_keys;
    Screen bad_package;
    DrawLaunchError(missing_keys.canvas(), "Game.nsp", "Missing or outdated keys",
                    "Check your own prod.keys and restart Avalon.", "Avalon error 0x0015");
    DrawLaunchError(bad_package.canvas(), "Game.nsp", "Invalid game package",
                    "Check the copy on the SD card.", "Avalon error 0x0012");
    CHECK(missing_keys.GuardsIntact() && bad_package.GuardsIntact());
    CHECK(missing_keys.bytes != bad_package.bytes);
    CHECK(missing_keys.Busy(40, 668, 700, 710) > 100); // Back-to-list action
    missing_keys.Save("launch_error");
}

void TestStartupFeedback() {
    using Stage = StartupStage;
    CHECK(StartupStatus({"Game", Stage::Open, 0, 0, 0}) == "Reading the game and local content.");
    CHECK(StartupStatus({"Game", Stage::Initialize, 0, 0, 0}) == "Preparing the emulator.");
    CHECK(StartupStatus({"Game", Stage::Shaders, 0, 0, 0}) == "Reading the shader cache.");
    CHECK(StartupStatus({"Game", Stage::Shaders, 2, 8, 0}) == "Processed 2 / 8 cached pipelines.");
    CHECK(StartupStatus({"Game", Stage::Shaders, 8, 8, 0}) == "Processed 8 / 8 cached pipelines.");
    CHECK(StartupStatus({"Game", Stage::Shaders, 9, 8, 0}) == "Reading the shader cache.");
    CHECK(StartupStatus({"Game", Stage::FirstFrame, 2, 8, 0}) == "No guest frame has been presented yet.");

    Screen reading;
    Screen processed;
    Screen waiting;
    DrawStartupScreen(reading.canvas(), {"Game", Stage::Shaders, 0, 0, 0});
    DrawStartupScreen(processed.canvas(), {"Game", Stage::Shaders, 2, 8, 1});
    DrawStartupScreen(waiting.canvas(), {"Game", Stage::FirstFrame, 0, 0, 0});
    CHECK(reading.bytes != processed.bytes && processed.bytes != waiting.bytes);
    CHECK(reading.GuardsIntact() && processed.GuardsIntact() && waiting.GuardsIntact());
    waiting.Save("startup_waiting");
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
    TestOptionsScrollToSelectedValue();
    TestLaunchError();
    TestStartupFeedback();
    TestTinyCanvasIsClipped();
    if (failures != 0) {
        std::printf("ui_render_test: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ui_render_test: all checks passed\n");
    return 0;
}
