// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace Common::Log {

// One writer per launch. The logging backend serializes calls from emulator threads.
class GameLogs {
public:
    GameLogs() = default;
    GameLogs(const GameLogs&) = delete;
    GameLogs& operator=(const GameLogs&) = delete;
    ~GameLogs() {
        if (file) {
            std::fclose(file);
        }
    }

    std::filesystem::path Begin(const std::filesystem::path& root, std::string_view label,
                                std::string_view game_path) {
        namespace fs = std::filesystem;
        if (file) {
            return {};
        }
        // Use the selected base path, not its display name or its selected add-ons.
        std::uint64_t hash = 14695981039346656037ULL;
        for (unsigned char c : game_path) {
            hash = (hash ^ c) * 1099511628211ULL;
        }
        std::string name;
        for (const char c : label.substr(0, 60)) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_') {
                name += c;
            }
        }
        const auto dir = root / ((name.empty() ? "game" : name) + "-" + Hex(hash));
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec || fs::symlink_status(dir, ec).type() != fs::file_type::directory || ec) {
            return {};
        }
        std::vector<std::uint64_t> runs;
        for (fs::directory_iterator it{dir, ec}, end; !ec && it != end; it.increment(ec)) {
            const auto filename = it->path().filename().string();
            if (filename.size() != 24 || !filename.starts_with("run-") ||
                !filename.ends_with(".log")) {
                continue;
            }
            std::uint64_t run{};
            const auto first = filename.data() + 4;
            const auto last = first + 16;
            const auto parsed = std::from_chars(first, last, run, 16);
            if (parsed.ec == std::errc{} && parsed.ptr == last && filename == Filename(run) &&
                it->symlink_status(ec).type() == fs::file_type::regular) {
                runs.push_back(run);
            }
        }
        if (ec) {
            return {};
        }
        std::sort(runs.begin(), runs.end());
        const auto latest = runs.empty() ? 0 : runs.back();
        if (latest == std::numeric_limits<std::uint64_t>::max()) {
            return {};
        }
        const auto path = dir / Filename(latest + 1);
        const int fd = ::open(path.string().c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) {
            return {};
        }
        file = ::fdopen(fd, "wb");
        if (!file) {
            ::close(fd);
            fs::remove(path, ec);
            return {};
        }
        // Never prune if the new file could not be created. Leave unrelated files alone.
        for (std::size_t i = 0; i + 10 <= runs.size(); ++i) {
            fs::remove(dir / Filename(runs[i]), ec);
            if (ec) {
                break;
            }
        }
        return path;
    }

    bool Write(std::string_view message) {
        if (!file || failed) {
            return false;
        }
        constexpr std::size_t limit = 16 * 1024 * 1024;
        if (capped) {
            return false;
        }
        if (message.size() > limit - bytes) {
            capped = true;
            Flush();
            return false;
        }
        const auto written = std::fwrite(message.data(), 1, message.size(), file);
        bytes += written;
        failed = written != message.size();
        return !failed;
    }

    bool Flush() {
        if (file && !failed) {
            failed = std::fflush(file) != 0;
        }
        return file && !failed;
    }

private:
    static std::string Hex(std::uint64_t value) {
        std::string result(16, '0');
        for (auto it = result.rbegin(); it != result.rend(); ++it, value >>= 4) {
            *it = "0123456789abcdef"[value & 0xf];
        }
        return result;
    }

    static std::string Filename(std::uint64_t run) {
        return "run-" + Hex(run) + ".log";
    }

    std::FILE* file{};
    std::size_t bytes{};
    bool capped{};
    bool failed{};
};

} // namespace Common::Log
