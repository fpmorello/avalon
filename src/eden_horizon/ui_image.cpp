// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eden_horizon/ui_image.h"

#include <cstdio>

// stb_image v2.34, public domain (see the licence block inside): PNG only, with a size
// limit, so a stray file cannot make the loader allocate the heap away.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS UiImage::MaxDimension
#define STBI_ASSERT(x) ((void)0)
#include "eden_horizon/stb_image.h"

namespace UiImage {
namespace {

/// stb gives no way to pass STBI_MAX_DIMENSIONS through stbi_load's size check cleanly
/// with a non-literal, so the limit is enforced again here after the header is read.
bool WithinLimits(int width, int height) {
    return width > 0 && height > 0 && width <= MaxDimension && height <= MaxDimension;
}

bool Decode(stbi_uc* data, Image& image) {
    if (data == nullptr) {
        return false;
    }
    image.rgba.assign(data, data + std::size_t(image.width) * image.height * 4);
    stbi_image_free(data);
    return true;
}

} // namespace

bool LoadPngFromMemory(const std::uint8_t* bytes, std::size_t size, Image& image) {
    image = {};
    int width = 0, height = 0, channels = 0;
    stbi_uc* const data =
        stbi_load_from_memory(bytes, int(size), &width, &height, &channels, 4);
    if (!WithinLimits(width, height)) {
        stbi_image_free(data);
        return false;
    }
    image.width = width;
    image.height = height;
    return Decode(data, image);
}

bool LoadPng(const char* path, Image& image) {
    image = {};
    std::FILE* const file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (length <= 0) {
        std::fclose(file);
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    const bool read = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    return read && LoadPngFromMemory(bytes.data(), bytes.size(), image);
}

} // namespace UiImage
