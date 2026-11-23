// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/file_sys/common_funcs.h"

namespace GameContent {

constexpr bool MatchesUpdate(u64 base, u64 title) {
    return base != 0 && (base | 0x800) == title;
}

constexpr bool MatchesDlc(u64 base, u64 title) {
    return base != 0 && FileSys::GetBaseTitleID(base) == FileSys::GetBaseTitleID(title) &&
           (title & 0x1800) == FileSys::AOC_TITLE_ID_OFFSET && FileSys::GetAOCID(title) != 0;
}

struct PackageId {
    u64 title_id;
    u32 version;
    std::string filename;

    bool operator==(const PackageId&) const = default;
};

inline int FindChoice(const std::vector<PackageId>& candidates, const PackageId& selected) {
    const auto it = std::ranges::find(candidates, selected);
    return it == candidates.end() ? 0 : static_cast<int>(it - candidates.begin()) + 1;
}

inline std::vector<PackageId> ReadSelection(std::istream& in) {
    std::vector<PackageId> selection;
    for (std::string line; std::getline(in, line);) {
        std::istringstream row{line};
        PackageId id{};
        if (row >> id.title_id >> id.version >> std::quoted(id.filename) &&
            !id.filename.empty() && (row >> std::ws).eof()) {
            selection.push_back(std::move(id));
        }
    }
    return selection;
}

inline void WriteSelection(std::ostream& out, const std::vector<PackageId>& selection) {
    for (const auto& [title, version, filename] : selection) {
        out << title << ' ' << version << ' ' << std::quoted(filename) << '\n';
    }
}

inline std::vector<PackageId> LoadSelection(const std::filesystem::path& path) {
    std::ifstream file{path};
    if (!file) {
        file = std::ifstream{path.string() + ".bak"};
    }
    return ReadSelection(file);
}

inline bool SaveSelection(const std::filesystem::path& path,
                          const std::vector<PackageId>& selection) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto temp = path.string() + ".tmp";
    const auto backup = path.string() + ".bak";
    std::ofstream file{temp, std::ios::trunc};
    WriteSelection(file, selection);
    file.close();
    if (file) {
        const bool have_current = fs::exists(path, ec);
        if (!ec && have_current) {
            // Horizon rename cannot replace an existing destination. Keep a recoverable copy.
            fs::remove(backup, ec);
            if (!ec) {
                fs::rename(path, backup, ec);
            }
        }
        if (!ec) {
            fs::rename(temp, path, ec);
            if (!ec) {
                fs::remove(backup, ec);
                return true;
            }
            // The previous selection remains readable from backup if restore also fails.
            if (have_current) {
                fs::rename(backup, path, ec);
            }
        }
    }
    fs::remove(temp, ec);
    return false;
}

} // namespace GameContent
