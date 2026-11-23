// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eden_horizon/game_content.h"

#include <cctype>
#include <filesystem>
#include <format>
#include <map>

#include "common/hex_util.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/loader/loader.h"
#include "eden_horizon/game_content_policy.h"

namespace GameContent {
namespace {

using FileSys::CNMT;
using FileSys::ContentRecordType;
using FileSys::NSP;
using FileSys::TitleType;
using FileSys::VirtualFilesystem;
using Choices = std::map<u64, std::vector<PackageId>>;
namespace fs = std::filesystem;

constexpr char SelectionDir[] = "sdmc:/switch/avalon/games";

std::shared_ptr<NSP> OpenPackage(const VirtualFilesystem& vfs, const std::string& path) {
    return FileSys::OpenContainerAsNsp(Core::GetGameFileFromPath(vfs, path),
                                       Loader::FileType::Unknown);
}

std::string ContentDir(const std::string& game, bool update) {
    return (fs::path{game}.parent_path() / (update ? "updates" : "dlc")).string();
}

std::string SelectionPath(u64 base) {
    return std::format("{}/{:016x}.content", SelectionDir, base);
}

void Save(u64 base, const std::vector<PackageId>& selection) {
    std::error_code ec;
    fs::create_directories(SelectionDir, ec);
    if (ec || !SaveSelection(SelectionPath(base), selection)) {
        LOG_WARNING(Frontend, "Could not save local content selection for {:016X}", base);
    }
}

bool MatchesRecord(const CNMT& cnmt, ContentRecordType type, const FileSys::NCA& nca) {
    const auto raw = nca.GetBaseFile();
    if (!raw) {
        return false;
    }
    const auto name = raw->GetName();
    return std::ranges::any_of(cnmt.GetContentRecords(), [&](const auto& record) {
        return record.type == type && name == Common::HexToString(record.nca_id, false) + ".nca";
    });
}

std::optional<CNMT> CandidateMetadata(const NSP& pkg, u64 base, u64 title, bool update,
                                      const FileSys::NCA& base_program) {
    if (!(update ? MatchesUpdate(base, title) : MatchesDlc(base, title))) {
        return std::nullopt;
    }
    const auto type = update ? TitleType::Update : TitleType::AOC;
    const auto record = update ? ContentRecordType::Program : ContentRecordType::Data;
    const auto nca = pkg.GetNCA(title, record, type);
    const auto meta = pkg.GetNCA(title, ContentRecordType::Meta, type);
    if (!nca || !meta) {
        return std::nullopt;
    }
    using FileSys::NCAContentType;
    using Loader::ResultStatus;
    if (update) {
        if (nca->GetType() != NCAContentType::Program ||
            (nca->GetTitleId() != title && nca->GetTitleId() != base)) {
            return std::nullopt;
        }
        const FileSys::NCA patched{nca->GetBaseFile(), &base_program};
        if (patched.GetStatus() != ResultStatus::Success ||
            (!base_program.GetExeFS() && !nca->GetExeFS())) {
            return std::nullopt;
        }
    } else if ((nca->GetType() != NCAContentType::Data &&
                nca->GetType() != NCAContentType::PublicData) || nca->GetTitleId() != title ||
               nca->GetStatus() != ResultStatus::Success || !nca->GetRomFS()) {
        return std::nullopt;
    }

    for (const auto& section : meta->GetSubdirectories()) {
        for (const auto& file : section->GetFiles()) {
            if (file->GetExtension() != "cnmt") {
                continue;
            }
            const CNMT cnmt{file};
            if (cnmt.GetTitleID() == title && cnmt.GetType() == type &&
                cnmt.GetContentRecords().size() == cnmt.GetHeader().number_content_entries &&
                MatchesRecord(cnmt, record, *nca)) {
                return cnmt;
            }
        }
    }
    return std::nullopt;
}

bool IsLocalPackage(const fs::path& path) {
    auto ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".nsp" || ext == ".nsz";
}

Choices Discover(const VirtualFilesystem& vfs, const std::string& game, u64 base,
                 const FileSys::NCA& base_program) {
    Choices choices;
    for (const bool update : {true, false}) {
        std::error_code ec;
        for (fs::directory_iterator it{ContentDir(game, update), ec}, end; !ec && it != end;
             it.increment(ec)) {
            const auto& path = it->path();
            std::error_code file_ec;
            if (!it->is_regular_file(file_ec) || !IsLocalPackage(path)) {
                continue;
            }
            const auto pkg = OpenPackage(vfs, path.string());
            if (!pkg) {
                continue;
            }
            for (const auto& [title, entries] : pkg->GetNCAs()) {
                if (const auto meta = CandidateMetadata(*pkg, base, title, update, base_program)) {
                    choices[title].push_back({title, meta->GetTitleVersion(), path.filename().string()});
                }
            }
        }
    }
    for (auto& [title, packages] : choices) {
        std::ranges::sort(packages, [](const PackageId& a, const PackageId& b) {
            return a.version != b.version ? a.version > b.version : a.filename < b.filename;
        });
    }
    return choices;
}

void AddRecords(FileSys::ManualContentProvider& provider, const NSP& pkg, u64 title, TitleType type,
                const CNMT* metadata = nullptr) {
    for (const auto& [entry_title, records] : pkg.GetNCAs()) {
        if (entry_title != title) {
            continue;
        }
        for (const auto& [key, nca] : records) {
            if (key.first == type && nca && nca->GetBaseFile() &&
                (!metadata || key.second == ContentRecordType::Meta ||
                 MatchesRecord(*metadata, key.second, *nca))) {
                provider.AddEntry(type, key.second, title, nca->GetBaseFile());
            }
        }
    }
}

} // namespace

void EditOptions(std::vector<Platform::MenuOption>& options, const std::string& title,
                 const std::string& game_path) {
    const auto vfs = std::make_shared<FileSys::RealVfsFilesystem>();
    const auto pkg = OpenPackage(vfs, game_path);
    const auto base = pkg ? pkg->GetProgramTitleID() : 0;
    const auto program = base ? pkg->GetNCA(base, ContentRecordType::Program) : nullptr;
    if (!program) {
        Platform::EditOptions(options, title);
        return;
    }

    const auto choices = Discover(vfs, game_path, base, *program);
    const auto saved = LoadSelection(SelectionPath(base));
    const auto start = options.size();
    for (const auto& [id, packages] : choices) {
        const bool update = MatchesUpdate(base, id);
        Platform::MenuOption row{
            .key = {},
            .label = update ? "Local update" : std::format("Local DLC {}", FileSys::GetAOCID(id)),
            .help = update ? "Choose a matching package from updates/."
                           : "Choose a matching package from dlc/ for this add-on.",
            .choices = {update ? "No local update" : "Off"},
            .selected = 0,
        };
        for (const auto& package : packages) {
            row.choices.push_back(std::format("v{}  {}", package.version, package.filename));
        }
        if (const auto it = std::ranges::find(saved, id, &PackageId::title_id); it != saved.end()) {
            row.selected = FindChoice(packages, *it);
        }
        options.push_back(std::move(row));
    }
    if (Platform::EditOptions(options, title) && !choices.empty()) {
        std::vector<PackageId> selected;
        auto row = start;
        for (const auto& [id, packages] : choices) {
            if (const auto choice = options[row++].selected; choice > 0) {
                selected.push_back(packages[choice - 1]);
            }
        }
        Save(base, selected);
    }
    options.resize(start);
}

void LaunchProvider::Register(const VirtualFilesystem& vfs, const std::string& game_path,
                              FileSys::ContentProviderUnion& content) {
    // Keep the previous launch path untouched when there are no saved selections.
    std::error_code ec;
    if (!fs::exists(ContentDir(game_path, true), ec) &&
        !fs::exists(ContentDir(game_path, false), ec)) {
        return;
    }
    const auto pkg = OpenPackage(vfs, game_path);
    const auto base = pkg ? pkg->GetProgramTitleID() : 0;
    const auto program = base ? pkg->GetNCA(base, ContentRecordType::Program) : nullptr;
    if (!program) {
        return;
    }

    const auto selection = LoadSelection(SelectionPath(base));
    bool registered = false;
    for (const auto& selected : selection) {
        const auto& [title, version, filename] = selected;
        const bool update = MatchesUpdate(base, title);
        const fs::path name{filename};
        if (name != name.filename() || !IsLocalPackage(name) ||
            (update ? update_title != 0 : HasEntry(title, ContentRecordType::Data))) {
            continue;
        }
        const auto path = fs::path{ContentDir(game_path, update)} / name;
        ec.clear();
        const auto addon = fs::is_regular_file(path, ec) ? OpenPackage(vfs, path.string()) : nullptr;
        const auto metadata = addon ? CandidateMetadata(*addon, base, title, update, *program)
                                    : std::nullopt;
        if (!metadata || metadata->GetTitleVersion() != version) {
            LOG_WARNING(Frontend, "Skipping unavailable local content {:016X}: {}", title, filename);
            continue;
        }
        AddRecords(*this, *addon, title, update ? TitleType::Update : TitleType::AOC, &*metadata);
        content.PinLaunchTitle(title);
        if (update) {
            update_title = title;
            update_version = version;
        }
        registered = true;
        LOG_INFO(Frontend, "Using local {} {:016X} v{}: {}", update ? "update" : "DLC", title,
                 version, filename);
    }
    if (registered) {
        AddRecords(*this, *pkg, base, TitleType::Application);
        content.SetSlot(FileSys::ContentProviderUnionSlot::FrontendManual, this);
        content.PinLaunchTitle(base);
    }
}

std::optional<u32> LaunchProvider::GetEntryVersion(u64 title_id) const {
    return update_title != 0 && title_id == update_title ? std::optional{update_version}
                                                       : std::nullopt;
}

} // namespace GameContent
