// SPDX-FileCopyrightText: Copyright 2026 JulianDr14
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <memory>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>
#include <winrt/Windows.Storage.h>
#include "core/file_sys/vfs/vfs_types.h"
#include "eden_uwp/game_library.h"

namespace EdenXbox {
// FutureAccessList is the persisted source catalog. Unavailable tokens remain
// there so reconnecting a USB does not silently forget the user's library.
std::string RememberGameFolder(const winrt::Windows::Storage::StorageFolder& folder);
std::string RememberContentFolder(const winrt::Windows::Storage::StorageFolder& folder);
LibraryScan ScanExternalGameFolders(std::stop_token stop);
FileSys::VirtualFilesystem MakeUwpFilesystem();
struct StorageBrowserEntry { std::string path; std::wstring name; bool folder{}; std::uint64_t size{}; };
std::vector<StorageBrowserEntry> ContentFolderSources();
// NS HUEHUE: where the mods page keeps a copy of each finished download (a USB folder the player
// picked; empty name when none). The install itself still reads the copy in LocalCache, and the
// installed mod lives in load/, where the loader reads it.
void SetModDownloadFolder(const winrt::Windows::Storage::StorageFolder& folder);
std::wstring ModDownloadFolderName();
/// Copies `file` there as `name`; false when no folder is set or the copy failed (MTA threads).
bool CopyToModDownloadFolder(const std::filesystem::path& file, std::wstring name);
std::vector<StorageBrowserEntry> BrowseStorageFolder(std::string_view path, std::stop_token stop);
/// The folders in `path`, and the files `keep` accepts (by their UTF-8 name).
std::vector<StorageBrowserEntry> BrowseStorageFolder(std::string_view path, std::stop_token stop,
                                                     const std::function<bool(std::string_view)>& keep);
std::uint64_t StorageFileStamp(std::string_view path);
bool ConsumeStorageReadFailure();
// Bounded preflight, run on an MTA worker before starting the guest. Reports
// access/removal errors in the library instead of closing the application.
std::string CheckExternalGame(std::string_view path);
// Opt-in AppContainer gate against a sparse fixture prepared by rom-storage-gate.ps1.
bool RunRomStorageGate(bool restore, const std::function<void(std::string)>& diagnostic);
} // namespace EdenXbox
