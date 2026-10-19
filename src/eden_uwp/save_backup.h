// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE: a copy of the saves in a folder the player picks on an external drive (Configuracoes >
// Saves), so uninstalling the app or a broken install does not take them along.
//
// The games keep saving in LocalState as before: on the Xbox a UWP app reaches a folder outside its
// own only through the folder picker and WinRT (broadFileSystemAccess is not offered there), which
// the engine's own file code cannot write through. So the copy is made by the frontend, in the
// library: after every game (and when the app opens) the files that changed since the last copy go
// to <folder>/NS-HUEHUE-saves, and Restaurar brings them back after a reinstall.
//
// Copied: the game saves (nand/user/save, but its temp folder) and the console's accounts
// (nand/system/save/8000000000000010: profiles.dat and the avatars), since the saves are kept under
// the account's id and a new install makes a new account.
//
// A folder written by another installation (the one before a reinstall, or another console) is
// not written over by the automatic copy: Restaurar takes it, or the player confirms replacing it.
// Restaurar first keeps the saves it replaces in LocalState/ns_saves_before_restore.
//
// Everything here blocks on WinRT: call it on an MTA worker thread, never on the UI thread.

#include <atomic>
#include <cstdint>
#include <ctime>
#include <cwchar>
#include <stop_token>
#include <string>

#include <winrt/Windows.Storage.h>

namespace EdenXbox::SaveBackup {

/// The folder's name inside the one the player picks.
inline constexpr std::wstring_view FolderName = L"NS-HUEHUE-saves";

/// What LocalState/ns_save_backup.txt keeps.
struct Settings {
    bool automatic{true};       ///< Copy after every game and when the app opens.
    std::string install;        ///< This installation's id (made once).
    std::int64_t last_time{};   ///< When the folder last matched this console (backup or restore), Unix seconds.
    std::uint32_t last_files{}; ///< Files in the folder then.
    std::uint32_t last_games{}; ///< Games with saves in the folder then.
    std::wstring folder;        ///< The picked folder's path, to show.
};
Settings LoadSettings();
bool SaveSettings(const Settings& settings);

/// Whether a folder was picked (it may still be on a drive that is not plugged in).
bool HasFolder();
/// Keeps `folder` as the one the copies go to.
void RememberFolder(const winrt::Windows::Storage::StorageFolder& folder);

/// What the picked folder holds.
struct FolderInfo {
    bool available{};     ///< The folder could be opened.
    bool has_backup{};    ///< A copy is there.
    bool other_install{}; ///< Written by another installation.
    std::int64_t time{};  ///< When it was written, Unix seconds.
    std::uint32_t files{}, games{};
};
FolderInfo Inspect();

enum class Outcome : std::uint8_t {
    Done,              ///< Copied (or nothing had changed).
    NoFolder,          ///< No folder was picked.
    FolderUnavailable, ///< The drive is not plugged in or the folder is gone.
    OtherInstall,      ///< The folder holds another installation's copy (automatic copy only).
    NothingToRestore,  ///< The folder holds no copy.
    Stopped,           ///< Asked to stop (the library closed).
    Failed,            ///< A file could not be read or written.
};
struct Result {
    Outcome outcome{Outcome::Failed};
    std::uint32_t copied{};  ///< Files written.
    std::uint32_t files{};   ///< Files in the copy afterwards.
    std::uint32_t games{};   ///< Games with saves in it.
    std::string error;
};
struct Progress {
    std::atomic<std::uint32_t> done{}, total{};
};

/// Copies the saves that changed to the folder. `replace_other` writes over another
/// installation's copy (the player confirmed it); without it such a folder is left alone.
Result Backup(bool replace_other, Progress& progress, std::stop_token stop);
/// Puts the folder's saves back over this console's, keeping the replaced ones first.
Result Restore(Progress& progress, std::stop_token stop);

/// "19/10/2026 06:40" in local time, for the page and its notices.
inline std::wstring FormatTime(std::int64_t seconds) {
    const std::time_t time = static_cast<std::time_t>(seconds);
    std::tm local{};
    if (localtime_s(&local, &time) != 0) return {};
    wchar_t text[32]{};
    return std::wcsftime(text, 32, L"%d/%m/%Y %H:%M", &local) ? std::wstring{text} : std::wstring{};
}

} // namespace EdenXbox::SaveBackup
