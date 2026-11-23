// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string_view>

#include "core/core.h"
#include "core/loader/loader.h"

namespace LaunchError {

enum class Recovery { None, NeedsShutdown, AlreadyShutDown };

struct Message {
    Recovery recovery;
    std::string_view title;
    std::string_view hint;
};

inline std::optional<Loader::ResultStatus> DecodeLoaderStatus(Core::SystemResultStatus result) {
    constexpr u32 first = static_cast<u32>(Core::SystemResultStatus::ErrorLoader);
    const auto code = static_cast<u32>(result);
    if (code <= first || code - first > static_cast<u32>(Loader::ResultStatus::ErrorUnsupportedSwitch2)) {
        return std::nullopt;
    }
    return static_cast<Loader::ResultStatus>(code - first);
}

inline Message Describe(Core::SystemResultStatus result) {
    using Core::SystemResultStatus;
    using Loader::ResultStatus;
    if (result == SystemResultStatus::ErrorGetLoader) {
        return {Recovery::NeedsShutdown, "Could not read the game file",
                "Check that the file is still on the SD card and that it is a supported game dump."};
    }

    const auto loader_status = DecodeLoaderStatus(result);
    if (!loader_status) {
        return {Recovery::None, "Avalon could not start the game", "See avalon_log.txt for details."};
    }

    constexpr auto recovery = Recovery::AlreadyShutDown;
    switch (*loader_status) {
    case ResultStatus::ErrorMissingProductionKeyFile:
    case ResultStatus::ErrorMissingHeaderKey:
    case ResultStatus::ErrorMissingTitlekek:
    case ResultStatus::ErrorMissingKeyAreaKey:
        return {recovery, "Missing or outdated keys",
                "Check your own prod.keys in Avalon's keys folder. After changing the file, restart Avalon."};
    case ResultStatus::ErrorMissingTitlekey:
        return {recovery, "Missing title key",
                "Check your own title.keys in Avalon's keys folder. After changing the file, restart Avalon."};
    case ResultStatus::ErrorIncorrectHeaderKey:
    case ResultStatus::ErrorIncorrectKeyAreaKey:
    case ResultStatus::ErrorIncorrectTitlekeyOrTitlekek:
    case ResultStatus::ErrorBadNCAHeader:
        return {recovery, "Could not read the game archive",
                "The dump may be damaged or your keys may not match. Check both; restart Avalon if you replace the keys."};
    case ResultStatus::ErrorNSPMissingProgramNCA:
    case ResultStatus::ErrorXCIMissingProgramNCA:
    case ResultStatus::ErrorNCANotProgram:
        return {recovery, "No bootable base game found",
                "Choose the base game, not an update or DLC. If this is the base game, check the dump and your keys."};
    case ResultStatus::ErrorBadPFSHeader:
    case ResultStatus::ErrorIncorrectPFSFileSize:
    case ResultStatus::ErrorBadXCIHeader:
    case ResultStatus::ErrorXCIMissingPartition:
        return {recovery, "Invalid or incomplete game package",
                "Check that the copy on the SD card is complete and matches your original dump."};
    case ResultStatus::ErrorNullFile:
        return {recovery, "Game file not found", "Check that the file is still on the SD card."};
    case ResultStatus::ErrorMissingNPDM:
        return {recovery, "Missing game metadata",
                "The executable is missing main.npdm. Check the base game and its matching update."};
    case ResultStatus::ErrorNoExeFS:
        return {recovery, "Missing game executable",
                "This base game may need its matching update. Press Y on the game to choose a local update."};
    case ResultStatus::ErrorMissingBKTRBaseRomFS:
        return {recovery, "This update needs the base game",
                "Select the base game and apply the matching update from its Y options."};
    case ResultStatus::ErrorUnsupportedSwitch2:
        return {recovery, "Switch 2 games are not supported",
                "Avalon runs Switch 1 games. A Switch 2 game will not start even with matching keys."};
    default:
        return {recovery, "The game could not be loaded", {}};
    }
}

} // namespace LaunchError
