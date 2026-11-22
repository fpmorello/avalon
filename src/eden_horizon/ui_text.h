// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Text the game picker shows for a file. Pure functions, so the host test
// (tools/switch2/tests/ui_text_test.cpp) checks the exact code the NRO runs.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace UiText {

namespace Detail {

/// Position of the extension's dot, or npos: a short alphanumeric tail after the last dot.
inline std::size_t ExtensionDot(std::string_view file_name) {
    const std::size_t dot = file_name.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= file_name.size() ||
        file_name.size() - dot - 1 > 4) {
        return std::string_view::npos;
    }
    for (std::size_t i = dot + 1; i < file_name.size(); ++i) {
        if (std::isalnum(static_cast<unsigned char>(file_name[i])) == 0) {
            return std::string_view::npos;
        }
    }
    return dot;
}

/// Underscores to spaces, runs of spaces to one, no spaces at the edges.
inline std::string Tidy(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '_') {
            c = ' ';
        }
        if (c == ' ' && (out.empty() || out.back() == ' ')) {
            continue;
        }
        out += c;
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

} // namespace Detail

/// The file name without its extension: what to show when nothing better is left.
inline std::string StemOf(std::string_view file_name) {
    const std::size_t dot = Detail::ExtensionDot(file_name);
    return std::string{dot == std::string_view::npos ? file_name : file_name.substr(0, dot)};
}

/// A game's name as people read it: no extension and no dump tags such as
/// "[0100E6D00E81C000][v0][Base]". Parentheses (region, edition) are kept.
inline std::string GameDisplayName(std::string_view file_name) {
    const std::string stem = StemOf(file_name);
    std::string without_tags;
    for (std::size_t i = 0; i < stem.size(); ++i) {
        if (stem[i] == '[') {
            const std::size_t close = stem.find(']', i);
            if (close != std::string::npos) {
                i = close;
                without_tags += ' ';
                continue;
            }
        }
        without_tags += stem[i];
    }
    std::string name = Detail::Tidy(without_tags);
    return name.empty() ? Detail::Tidy(stem) : name;
}

/// The Title ID of a dump's name: the first `[16 hex digits]` group, lower case and
/// without brackets, or empty when there is none. This is the name cover art uses
/// (`0100e6d00e81c000.png`), the convention Eden's own icons follow.
inline std::string TitleIdOf(std::string_view file_name) {
    for (std::size_t open = file_name.find('['); open != std::string_view::npos;
         open = file_name.find('[', open + 1)) {
        const std::size_t digits = open + 1;
        const std::size_t close = file_name.find(']', digits);
        if (close != digits + 16) {
            continue;
        }
        std::string id;
        for (std::size_t i = digits; i < close; ++i) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(file_name[i])));
            if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
                id.clear();
                break;
            }
            id += c;
        }
        if (!id.empty()) {
            return id;
        }
    }
    return {};
}

/// "NSP", "XCI"...: the extension in capitals, or empty.
inline std::string FileTypeLabel(std::string_view file_name) {
    const std::size_t dot = file_name.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= file_name.size()) {
        return {};
    }
    std::string type{file_name.substr(dot + 1)};
    for (char& c : type) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return type;
}

/// "304 MB", "1.5 GB": one decimal below 10, whole numbers above.
inline std::string SizeLabel(std::uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    constexpr const char* Units[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / 1024.0;
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(Units)) {
        value /= 1024.0;
        ++unit;
    }
    char text[32];
    if (value < 9.95) {
        std::snprintf(text, sizeof(text), "%.1f", value);
        std::string number = text;
        if (number.size() > 2 && number.compare(number.size() - 2, 2, ".0") == 0) {
            number.resize(number.size() - 2);
        }
        return number + " " + Units[unit];
    }
    std::snprintf(text, sizeof(text), "%.0f", value);
    return std::string{text} + " " + Units[unit];
}

} // namespace UiText
