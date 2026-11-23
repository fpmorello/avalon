// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>

#include "eden_horizon/launch_error.h"

int main() {
    using Core::SystemResultStatus;
    using Loader::ResultStatus;
    constexpr auto Encode = [](ResultStatus status) {
        return static_cast<SystemResultStatus>(static_cast<u32>(SystemResultStatus::ErrorLoader) +
                                               static_cast<u32>(status));
    };
    const auto missing = LaunchError::Describe(Encode(ResultStatus::ErrorMissingProductionKeyFile));
    assert(missing.recovery == LaunchError::Recovery::AlreadyShutDown);
    assert(missing.title == "Missing or outdated keys");
    assert(missing.hint.find("prod.keys") != std::string_view::npos);
    assert(missing.hint.find("restart Avalon") != std::string_view::npos);
    const auto titlekey = LaunchError::Describe(Encode(ResultStatus::ErrorMissingTitlekey));
    assert(titlekey.title == "Missing title key");
    assert(titlekey.hint.find("title.keys") != std::string_view::npos);
    const auto metadata = LaunchError::Describe(Encode(ResultStatus::ErrorMissingNPDM));
    assert(metadata.recovery == LaunchError::Recovery::AlreadyShutDown);
    assert(metadata.hint.contains("main.npdm"));
    const auto ambiguous = LaunchError::Describe(Encode(ResultStatus::ErrorNSPMissingProgramNCA));
    assert(ambiguous.title == "No bootable base game found");
    assert(ambiguous.hint.find("dump") != std::string_view::npos);
    assert(LaunchError::Describe(Encode(ResultStatus::ErrorNoExeFS)).title == "Missing game executable");
    assert(LaunchError::Describe(Encode(ResultStatus::ErrorUnsupportedSwitch2)).title ==
           "Switch 2 games are not supported");
    assert(LaunchError::Describe(SystemResultStatus::ErrorGetLoader).recovery ==
           LaunchError::Recovery::NeedsShutdown);
    assert(LaunchError::Describe(SystemResultStatus::ErrorVideoCore).recovery ==
           LaunchError::Recovery::None);
    assert(LaunchError::Describe(SystemResultStatus::Success).recovery == LaunchError::Recovery::None);
    assert(LaunchError::Describe(Encode(static_cast<ResultStatus>(65535))).recovery ==
           LaunchError::Recovery::None);
}
