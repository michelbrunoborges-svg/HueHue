// SPDX-FileCopyrightText: Copyright 2026 JulianDr14
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>
#include <winrt/Windows.Storage.h>

namespace EdenXbox {
struct GameFolderSource { std::string token; std::wstring name; };
/// A firmware's version, read from its SystemVersion archive (0100000000000809).
struct FirmwareVersion {
    bool known{};
    unsigned major{}, minor{}, micro{};
    std::string display; ///< "19.0.1" as the firmware names itself.
    [[nodiscard]] unsigned Packed() const { return major << 16 | minor << 8 | micro; }
};
/// NS HUEHUE: a firmware copied to a staging folder, waiting for the player to confirm it
/// replaces the installed one (Arquivos e USB > Importar firmware). What the checks found is only
/// reported: every .nca of the folder is copied, as the player asked (19/10/2026).
struct StagedFirmware {
    std::filesystem::path stage;
    FirmwareVersion version;
    unsigned files{};
    std::uint64_t bytes{};
    unsigned unreadable{}; ///< NCAs the keys could not open.
    unsigned foreign{};    ///< NCAs of a game (title id outside the system range).
    bool incomplete{};     ///< SystemVersion (0809) or the Mii editor (1009) is missing.
    [[nodiscard]] bool HasWarnings() const { return unreadable || foreign || incomplete; }
};
struct FileSetupStatus {
    bool keys_ready{};
    unsigned firmware_files{};
    FirmwareVersion firmware;        ///< The installed one.
    FirmwareVersion firmware_backup; ///< The one kept before the last install (Restaurar).
    bool has_backup{};
    std::vector<GameFolderSource> sources;
};
struct ConfigurationPanel {
    bool open{};
    bool files{};
    bool busy{};
    size_t selected{};
    FileSetupStatus status;
    std::wstring notice;
    unsigned completed{}, total{};
};
/// Rows of the settings home, in order.
enum ConfigurationHomeRow : size_t { HomeSystem, HomeControllers, HomeFiles, HomeSaves, HomeOnline, HomeRowCount };
/// Arquivos e USB rows: games folder, keys, firmware, updates, DLC, restore the previous
/// firmware, online firmware (not configured), then one row per game folder.
inline constexpr size_t FileRowRestoreFirmware = 5, FileRowOnlineFirmware = 6, FileRowSources = 7;
inline size_t ConfigurationRowCount(const ConfigurationPanel& panel) {
    return panel.files ? FileRowSources + panel.status.sources.size() : HomeRowCount;
}
inline size_t ConfigurationFirstRow(const ConfigurationPanel& panel) {
    return (panel.selected / 5) * 5;
}
// Share geometry between painting and pointer hit testing.
struct ConfigurationLayout {
    float x, width, top, stride, row_height, footer;
};
inline ConfigurationLayout GetConfigurationLayout(const ConfigurationPanel& panel) {
    return panel.files ? ConfigurationLayout{126, 1028, 246, 62, 54, 622}
                       : ConfigurationLayout{180, 920, 226, 92, 80, 584};
}
enum class FileImport { Keys, Firmware };
struct FileImportProgress {
    std::atomic<unsigned> completed{};
    std::atomic<unsigned> total{};
};
// These operations run on an MTA worker while the guest is stopped.
FileSetupStatus ReadFileSetupStatus();
void ForgetGameFolder(std::string_view token);
/// With `stage_firmware`, a firmware import stops once the copy is validated and returns
/// FirmwareStagedMarker; TakeStagedFirmware then gives it for the player to confirm.
inline constexpr std::string_view FirmwareStagedMarker = "\x01firmware-staged";
std::string ImportSystemFiles(const winrt::Windows::Storage::StorageFolder& source,
                             FileImport kind, FileImportProgress& progress, std::stop_token stop,
                             bool stage_firmware = false);
std::optional<StagedFirmware> TakeStagedFirmware();
/// Installs the staged firmware, keeping the one it replaces as the backup. "" on success.
std::string CommitStagedFirmware(const StagedFirmware& staged);
void DiscardStagedFirmware(const StagedFirmware& staged);
/// Puts the backup back as the installed firmware (and the installed one as the backup).
std::string RestoreFirmwareBackup();
bool HasUsableHeaderKey();
bool RunFileManagerGate(const std::function<void(std::string)>& diagnostic);
} // namespace EdenXbox
