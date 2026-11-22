// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Avalon's menu screens, drawn in software into an RGBA buffer (odd/tasks/b4-ui.md, T2).
// No platform code: the NRO hands it a framebuffer, the host preview test a plain buffer.

#include <cstdint>
#include <string>
#include <vector>

namespace UiImage {
struct Image;
}

namespace UiRender {

constexpr int ScreenWidth = 1280;
constexpr int ScreenHeight = 720;

/// Pixels in memory order R, G, B, A (PIXEL_FORMAT_RGBA_8888).
struct Canvas {
    std::uint8_t* pixels;
    int width;
    int height;
    int stride; ///< Bytes from one row to the next
};

struct ListItem {
    std::string title;  ///< Folder name or readable game name
    std::string type;   ///< "NSP", "XCI"... empty for folders
    std::string detail; ///< Size, or empty
    bool is_folder;
};

struct PickerView {
    std::string folder;
    std::vector<ListItem> items;
    int selected;
    int top;  ///< First visible item
    /// Cover of the selected game, or null when it has none (folders never do).
    const UiImage::Image* cover = nullptr;
};

struct OptionRow {
    std::string label;
    std::string value;
};

struct OptionsView {
    std::string title;
    std::vector<OptionRow> rows;
    int selected;
    std::string help; ///< Of the selected row; '\n' breaks lines
};

/// Items that fit in the picker's list.
constexpr int PickerVisibleRows = 9;

/// The build shown in the header, e.g. "b4".
void SetBuildLabel(std::string label);

void DrawPicker(const Canvas& canvas, const PickerView& view);
void DrawOptions(const Canvas& canvas, const OptionsView& view);

} // namespace UiRender
