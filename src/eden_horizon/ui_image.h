// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Cover art for the game picker (odd/tasks/b5-cover-art.md): PNG loading on both the
// device and the host test, through stb_image (public domain).

#include <cstdint>
#include <vector>

namespace UiImage {

/// An RGBA image, 8 bits per channel, memory order R, G, B, A.
struct Image {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;

    bool IsValid() const {
        return !rgba.empty();
    }
};

/// Largest accepted cover, to keep a stray big file from eating the heap.
constexpr int MaxDimension = 2048;

/// Decodes the PNG at `path` into `image`. Returns false and leaves it empty on any
/// failure (missing file, not a PNG, larger than MaxDimension, corrupt).
bool LoadPng(const char* path, Image& image);

/// Decodes PNG bytes already in memory; same rules as LoadPng.
bool LoadPngFromMemory(const std::uint8_t* bytes, std::size_t size, Image& image);

} // namespace UiImage
