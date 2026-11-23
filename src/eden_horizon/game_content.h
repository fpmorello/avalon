// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/file_sys/registered_cache.h"
#include "eden_horizon/platform.h"

namespace GameContent {

// Adds local content choices to the existing per-game options dialog.
void EditOptions(std::vector<Platform::MenuOption>& options, const std::string& title,
                 const std::string& game_path);

class LaunchProvider final : public FileSys::ManualContentProvider {
public:
    void Register(const FileSys::VirtualFilesystem& vfs, const std::string& game_path,
                  FileSys::ContentProviderUnion& content);
    std::optional<u32> GetEntryVersion(u64 title_id) const override;

private:
    u64 update_title{};
    u32 update_version{};
};

} // namespace GameContent
