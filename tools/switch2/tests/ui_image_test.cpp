// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone host test for the picker's PNG loader (src/eden_horizon/ui_image.cpp,
// odd/tasks/b5-cover-art.md T2). The image it decodes is embedded, so the test needs
// no files on disk.
//
// Build & run: tools/switch2/tests/run.sh ui_image_test

#include "eden_horizon/ui_image.cpp" // one translation unit: run.sh builds a single file

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                       \
    } while (false)

/// 3x2 RGB PNG: red green blue / yellow magenta cyan (made with Pillow).
constexpr unsigned char kPng[] = {
    137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,3,0,0,0,2,8,2,0,0,0,18,22,241,77,
    0,0,0,24,73,68,65,84,120,218,5,193,1,1,0,0,8,195,32,110,255,206,19,132,208,73,75,30,
    64,236,6,253,0,162,183,0,0,0,0,0,73,69,78,68,174,66,96,130,
};

const std::uint8_t* Pixel(const UiImage::Image& image, int x, int y) {
    return image.rgba.data() + (std::size_t(y) * image.width + x) * 4;
}

void TestDecode() {
    UiImage::Image image;
    CHECK(UiImage::LoadPngFromMemory(kPng, sizeof kPng, image));
    CHECK(image.width == 3);
    CHECK(image.height == 2);
    CHECK(image.rgba.size() == 3 * 2 * 4);
    // Top row: red, green, blue; forced to four channels, opaque.
    CHECK(Pixel(image, 0, 0)[0] == 255 && Pixel(image, 0, 0)[1] == 0 && Pixel(image, 0, 0)[3] == 255);
    CHECK(Pixel(image, 1, 0)[1] == 255 && Pixel(image, 1, 0)[2] == 0);
    CHECK(Pixel(image, 2, 0)[2] == 255 && Pixel(image, 2, 0)[0] == 0);
    // Bottom row: yellow, magenta, cyan.
    CHECK(Pixel(image, 0, 1)[0] == 255 && Pixel(image, 0, 1)[1] == 255);
    CHECK(Pixel(image, 1, 1)[0] == 255 && Pixel(image, 1, 1)[2] == 255);
    CHECK(Pixel(image, 2, 1)[1] == 255 && Pixel(image, 2, 1)[2] == 255);
}

void TestRejects() {
    UiImage::Image image;
    // Not a PNG.
    const unsigned char junk[] = "this is not a png at all, just text";
    CHECK(!UiImage::LoadPngFromMemory(junk, sizeof junk, image));
    CHECK(!image.IsValid());
    // Truncated PNG.
    CHECK(!UiImage::LoadPngFromMemory(kPng, 20, image));
    // Missing file.
    CHECK(!UiImage::LoadPng("/nonexistent/cover.png", image));
}

void TestSizeLimit() {
    // A 1-pixel-tall 2049-pixel-wide PNG: above MaxDimension (2048).
    // Built as a raw type-0 IHDR/IDAT pair with stored (uncompressed) deflate.
    std::vector<unsigned char> png{137,80,78,71,13,10,26,10};
    const auto chunk = [&png](const char* type, const std::vector<unsigned char>& data) {
        const unsigned length = static_cast<unsigned>(data.size());
        png.push_back(length >> 24); png.push_back(length >> 16);
        png.push_back(length >> 8); png.push_back(length);
        std::size_t crc_at = png.size();
        for (char c : std::string{type, 4}) png.push_back(static_cast<unsigned char>(c));
        png.insert(png.end(), data.begin(), data.end());
        // CRC is not checked on rejection paths that fail earlier; zero passes here only
        // because the decoder refuses the size after reading the header.
        (void)crc_at;
        png.push_back(0); png.push_back(0); png.push_back(0); png.push_back(0);
    };
    std::vector<unsigned char> ihdr{0,0,8,1, 0,0,0,1, 8, 0, 0, 0, 0};
    chunk("IHDR", ihdr);
    // Stored-deflate scanline (filter byte + one byte stretched is invalid data; the size
    // check must reject it before the data matters).
    std::vector<unsigned char> idat{0x78,0x01,0x01,0x00,0xff,0xff,0x00,0x00,0x00,0x01};
    chunk("IDAT", idat);
    chunk("IEND", {});
    UiImage::Image image;
    CHECK(!UiImage::LoadPngFromMemory(png.data(), png.size(), image));
}

} // namespace

int main() {
    TestDecode();
    TestRejects();
    TestSizeLimit();
    if (failures != 0) {
        std::printf("ui_image_test: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ui_image_test: all checks passed\n");
    return 0;
}
