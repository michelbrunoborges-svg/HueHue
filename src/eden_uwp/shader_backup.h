// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE 0.4.5: the games' shader caches (LocalState/eden/shader/<title id>/: d3d12.bin with
// d3d12_hot.bin, opengl.bin) kept in a folder the player picks on an external drive, under
// <folder>/NS-HUEHUE-shaders/<title id>/, the way the saves are (save_backup.h).
//
// The renderer keeps reading and appending the cache in LocalState (the engine's file code
// cannot reach the drive), so the copies go both ways around a game: before it starts, a larger
// copy on the drive comes in (the game builds the pipelines it already met, instead of stuttering
// into them); when it closes, what grew goes out. The larger file wins each way, so a cache is
// never replaced by a smaller one (another console's, an older install's). Manual copies of every
// game are on Configuracoes > Saves e shaders (USB).
//
// Everything here blocks on WinRT: call it on an MTA worker thread, never on the UI thread.

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>

#include <winrt/Windows.Storage.h>

namespace EdenXbox::ShaderBackup {

inline constexpr std::wstring_view FolderName = L"NS-HUEHUE-shaders";

/// Whether a folder was picked (it may still be on a drive that is not plugged in).
bool HasFolder();
/// The picked folder's path, to show (empty when none).
std::wstring FolderPath();
void RememberFolder(const winrt::Windows::Storage::StorageFolder& folder);
/// Sincronizar shaders ao jogar (on by default once a folder is picked).
bool AutoSync();
void SetAutoSync(bool on);

enum class Outcome : std::uint8_t { Done, NoFolder, FolderUnavailable, NothingToRestore, Stopped, Failed };
struct Result {
    Outcome outcome{Outcome::Failed};
    std::uint32_t games{};  ///< Games with a file copied.
    std::uint32_t files{};
    std::uint64_t bytes{};
    std::string error;
};

/// This console's caches to the drive (one game, or all of them with nullopt).
Result Backup(std::optional<std::uint64_t> title_id, std::stop_token stop = {});
/// The drive's larger caches to this console (one game, or all of them).
Result Restore(std::optional<std::uint64_t> title_id, std::stop_token stop = {});

} // namespace EdenXbox::ShaderBackup
