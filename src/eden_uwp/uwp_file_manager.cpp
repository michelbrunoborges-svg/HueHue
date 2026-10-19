// SPDX-FileCopyrightText: Copyright 2026 JulianDr14
// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden_uwp/uwp_file_manager.h"
#include "eden_uwp/localization.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <mutex>
#include <utility>
#include <stdexcept>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Storage.Search.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/scope_exit.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/loader/loader.h"

namespace EdenXbox {
namespace {
/// Import failures reach the player through e.what(), so they are thrown already translated.
std::string ImportError(I18n::Text id) {
    return I18n::ToUtf8(I18n::Tr(id));
}
using namespace winrt::Windows::Storage;
using winrt::Windows::Storage::AccessCache::StorageApplicationPermissions;
constexpr unsigned PageSize = 64;
constexpr unsigned MaxFirmwareFiles = 4096;
std::wstring BrokerPath(std::filesystem::path path) {
    return path.make_preferred().wstring();
}
void RequireInternalDestination(const std::filesystem::path& destination) {
    auto base = BrokerPath(std::filesystem::path{std::wstring{ApplicationData::Current().LocalFolder().Path()}});
    auto path = BrokerPath(destination.lexically_normal());
    const auto lower = [](wchar_t c) { return std::towlower(c); };
    std::transform(base.begin(), base.end(), base.begin(), lower);
    std::transform(path.begin(), path.end(), path.begin(), lower);
    if (!path.starts_with(base + L"\\")) throw std::runtime_error(ImportError(I18n::Text::ImportDestinationInvalid));
}

std::filesystem::path FirmwarePath() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/Contents/registered";
}
/// NS HUEHUE: the firmware the last install replaced, kept beside it for Restaurar.
std::filesystem::path FirmwareBackupPath() {
    return FirmwarePath().parent_path() / L"registered.backup";
}
/// Makes `previous` the firmware backup (the older backup goes).
void KeepAsBackup(const std::filesystem::path& previous) {
    std::error_code error;
    std::filesystem::remove_all(FirmwareBackupPath(), error);
    std::filesystem::rename(previous, FirmwareBackupPath(), error);
    if (error) std::filesystem::remove_all(previous, error);
}

// Directory publication is confined to the app's internal keys/system-content folders.
// Keep the previous installation until all source files have been copied and checked.
// The steps are rename(destination -> .previous), rename(stage -> destination), then the
// previous one goes (keys) or becomes the backup (firmware); a stop between them is resolved
// here, at the next start: no destination means the previous one comes back, both mean the new
// one was complete.
bool RecoverDirectory(const std::filesystem::path& destination) {
    RequireInternalDestination(destination);
    const auto previous = destination.parent_path() / (destination.filename().wstring() + L".previous");
    if (std::filesystem::exists(previous)) {
        if (!std::filesystem::exists(destination)) {
            std::filesystem::rename(previous, destination);
            return true;
        }
        if (destination == FirmwarePath()) KeepAsBackup(previous);
        else std::filesystem::remove_all(previous);
    }
    return false;
}
void PublishDirectory(StorageFolder stage, const std::filesystem::path& destination) {
    const auto previous = destination.parent_path() / (destination.filename().wstring() + L".previous");
    RecoverDirectory(destination);
    const bool exists = std::filesystem::exists(destination);
    if (exists) std::filesystem::rename(destination, previous);
    try {
        stage.RenameAsync(destination.filename().wstring(), NameCollisionOption::FailIfExists).get();
    } catch (...) {
        if (exists) std::filesystem::rename(previous, destination);
        throw;
    }
    // Failed cleanup is retried by ReadFileSetupStatus on the next library startup.
    std::error_code error;
    if (exists && destination == FirmwarePath()) KeepAsBackup(previous);
    else if (exists) std::filesystem::remove_all(previous, error);
}

/// The version in a SystemVersion data NCA (0100000000000809): the archive's "file" is
/// FirmwareVersionFormat (major, minor, micro at 0, the display version at 0x68).
FirmwareVersion ReadVersion(FileSys::NCA& nca) {
    FirmwareVersion version;
    const auto romfs = nca.GetRomFS();
    if (!romfs) return version;
    const auto extracted = FileSys::ExtractRomFS(romfs);
    const auto file = extracted ? extracted->GetFile("file") : nullptr;
    if (!file) return version;
    const auto data = file->ReadAllBytes();
    if (data.size() < 0x80) return version;
    version.known = true;
    version.major = data[0];
    version.minor = data[1];
    version.micro = data[2];
    const auto* text = reinterpret_cast<const char*>(data.data() + 0x68);
    version.display.assign(text, strnlen(text, 0x18));
    if (version.display.empty())
        version.display = std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.micro);
    return version;
}
bool IsSystemVersion(FileSys::NCA& nca) {
    return nca.GetTitleId() == 0x0100000000000809ULL && nca.GetType() == FileSys::NCAContentType::Data;
}
/// The version of the firmware in `folder` (unknown when it has none or the keys cannot read it).
FirmwareVersion FolderFirmwareVersion(const std::filesystem::path& folder) {
    std::error_code error;
    FileSys::RealVfsFilesystem vfs;
    for (std::filesystem::directory_iterator it{folder, error}, end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || it->path().extension() != ".nca") continue;
        const auto u8path = it->path().u8string();
        auto raw = vfs.OpenFile(std::string{reinterpret_cast<const char*>(u8path.data()), u8path.size()}, FileSys::OpenMode::Read);
        if (!raw) continue;
        FileSys::NCA nca{raw};
        if (nca.GetStatus() == Loader::ResultStatus::Success && IsSystemVersion(nca)) return ReadVersion(nca);
    }
    return {};
}
std::mutex staged_mutex;
std::optional<StagedFirmware> staged_firmware;
bool ValidProductionKeys(const std::filesystem::path& file) {
    // Match Eden's text format, but reject a missing/zero/duplicated XTS header key
    // before replacing working data. Never log key material.
    std::ifstream input(file);
    std::string line;
    while (std::getline(input, line)) {
        line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char c) { return std::isspace(c); }), line.end());
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!line.starts_with("header_key=")) continue;
        const auto key = std::string_view{line}.substr(11);
        return key.size() == 64 && std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isxdigit(c); }) &&
               key.substr(0, 32) != key.substr(32);
    }
    return false;
}
}

bool HasUsableHeaderKey() {
    auto& keys = Core::Crypto::KeyManager::Instance();
    if (!keys.HasKey(Core::Crypto::S256KeyType::Header)) return false;
    const auto key = keys.GetKey(Core::Crypto::S256KeyType::Header);
    return !std::equal(key.begin(), key.begin() + 16, key.begin() + 16);
}

FileSetupStatus ReadFileSetupStatus() {
    const auto keys = Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir);
    const bool restored_keys = RecoverDirectory(keys);
    RecoverDirectory(FirmwarePath());
    if (restored_keys) Core::Crypto::KeyManager::Instance().ReloadKeys();
    FileSetupStatus result;
    result.keys_ready = HasUsableHeaderKey();
    std::error_code error;
    for (std::filesystem::directory_iterator it{FirmwarePath(), error}, end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && it->path().extension() == ".nca") ++result.firmware_files;
    }
    {
        // Staging folders left by a session that closed before its confirmation.
        std::scoped_lock lock{staged_mutex};
        std::vector<std::filesystem::path> stale;
        for (std::filesystem::directory_iterator it{FirmwarePath().parent_path(), error}, end; !error && it != end;
             it.increment(error)) {
            const auto name = it->path().filename().wstring();
            if (name.starts_with(L"registered.import") && (!staged_firmware || staged_firmware->stage != it->path()))
                stale.push_back(it->path());
        }
        error.clear();
        for (const auto& folder : stale) {
            std::error_code remove_error;
            std::filesystem::remove_all(folder, remove_error);
        }
    }
    if (result.keys_ready && result.firmware_files) result.firmware = FolderFirmwareVersion(FirmwarePath());
    result.has_backup = std::filesystem::is_directory(FirmwareBackupPath(), error);
    if (result.keys_ready && result.has_backup) result.firmware_backup = FolderFirmwareVersion(FirmwareBackupPath());
    for (const auto& entry : StorageApplicationPermissions::FutureAccessList().Entries()) {
        const auto token = winrt::to_string(entry.Token);
        if (!token.starts_with("eden-games-")) continue;
        auto name = std::filesystem::path{std::wstring{entry.Metadata}}.filename().wstring();
        result.sources.push_back({token, name.empty() ? std::wstring{I18n::Tr(I18n::Text::UsbFolder)} : name});
    }
    return result;
}
void ForgetGameFolder(std::string_view token) {
    if (!token.starts_with("eden-games-")) throw std::invalid_argument("not a game folder token");
    StorageApplicationPermissions::FutureAccessList().Remove(winrt::to_hstring(token));
}

std::string ImportSystemFiles(const StorageFolder& source, FileImport kind,
                             FileImportProgress& progress, std::stop_token stop, bool stage_firmware) {
    StorageFolder stage{nullptr};
    try {
        const bool keys = kind == FileImport::Keys;
        if (!keys && !HasUsableHeaderKey()) return I18n::ToUtf8(I18n::Tr(I18n::Text::KeysFirst));
        const auto destination = keys ? Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir) : FirmwarePath();
        RecoverDirectory(destination);
        std::filesystem::create_directories(destination.parent_path());
        auto parent = StorageFolder::GetFolderFromPathAsync(BrokerPath(destination.parent_path())).get();
        stage = parent.CreateFolderAsync(destination.filename().wstring() + L".import",
                                         CreationCollisionOption::GenerateUniqueName).get();
        std::vector<StorageFile> files;
        if (keys) {
            files.push_back(source.GetFileAsync(L"prod.keys").get());
            if (auto title = source.TryGetItemAsync(L"title.keys").get()) files.push_back(title.as<StorageFile>());
            // Preserve unrelated/generated key files in a complete staged directory.
            if (std::filesystem::exists(destination)) {
                auto existing = StorageFolder::GetFolderFromPathAsync(BrokerPath(destination)).get();
                for (const auto& file : existing.GetFilesAsync().get()) {
                    if (file.Name() != L"prod.keys" && (files.size() == 1 || file.Name() != L"title.keys"))
                        file.CopyAsync(stage, file.Name(), NameCollisionOption::FailIfExists).get();
                }
            }
        } else {
            for (unsigned start = 0;; start += PageSize) {
                auto page = source.GetFilesAsync(Search::CommonFileQuery::DefaultQuery, start, PageSize).get();
                for (const auto& file : page) {
                    std::wstring extension{file.FileType()};
                    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return std::towlower(c); });
                    if (extension == L".nca") files.push_back(file);
                    if (files.size() > MaxFirmwareFiles) throw std::runtime_error(ImportError(I18n::Text::TooManyFirmwareFiles));
                }
                if (page.Size() < PageSize) break;
                if (start >= 16384) throw std::runtime_error(ImportError(I18n::Text::FirmwareFolderOnly));
                if (stop.stop_requested()) throw std::runtime_error(ImportError(I18n::Text::ImportCancelled));
            }
            if (files.empty()) throw std::runtime_error(ImportError(I18n::Text::NoFirmwareInFolder));
        }
        progress.total.store(static_cast<unsigned>(files.size()), std::memory_order_relaxed);
        // The copy needs room for itself next to the installed set (and its backup).
        std::uint64_t total_bytes = 0;
        for (const auto& file : files) total_bytes += file.GetBasicPropertiesAsync().get().Size();
        {
            std::error_code space_error;
            const auto space = std::filesystem::space(destination.parent_path(), space_error);
            if (!space_error && space.available < total_bytes + (64ULL << 20))
                throw std::runtime_error(ImportError(I18n::Text::NsFirmwareNoSpace));
        }
        FirmwareVersion staged_version;
        bool system_version = false, mii_editor = false;
        unsigned unreadable = 0, foreign = 0;
        FileSys::RealVfsFilesystem vfs;
        for (const auto& file : files) {
            if (stop.stop_requested()) throw std::runtime_error(ImportError(I18n::Text::ImportCancelledKept));
            const auto bytes = file.GetBasicPropertiesAsync().get().Size();
            if (keys && bytes > 4 * 1024 * 1024) throw std::runtime_error(ImportError(I18n::Text::KeysFileTooLarge));
            auto copied = file.CopyAsync(stage, file.Name(), NameCollisionOption::FailIfExists).get();
            if (copied.GetBasicPropertiesAsync().get().Size() != bytes) throw std::runtime_error(ImportError(I18n::Text::CopyIncomplete));
            if (!keys) {
                const auto path = std::filesystem::path{std::wstring{copied.Path()}};
                const auto u8path = path.u8string();
                auto raw = vfs.OpenFile(std::string{reinterpret_cast<const char*>(u8path.data()), u8path.size()}, FileSys::OpenMode::Read);
                FileSys::NCA nca{raw};
                // Reported, not refused: the file stays in the copy.
                if (nca.GetStatus() != Loader::ResultStatus::Success) {
                    ++unreadable;
                    LOG_WARNING(Frontend, "File manager: firmware file {} unreadable, copied anyway", winrt::to_string(file.Name()));
                    progress.completed.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                const auto id = nca.GetTitleId();
                if ((id >> 16) != (0x0100000000000000ULL >> 16)) {
                    ++foreign;
                    LOG_WARNING(Frontend, "File manager: firmware file {} has title {:016X}, copied anyway",
                                winrt::to_string(file.Name()), id);
                }
                if (IsSystemVersion(nca)) {
                    system_version = true;
                    staged_version = ReadVersion(nca);
                }
                mii_editor |= id == 0x0100000000001009ULL && nca.GetType() == FileSys::NCAContentType::Program;
            }
            progress.completed.fetch_add(1, std::memory_order_relaxed);
        }
        if (keys && !ValidProductionKeys(std::filesystem::path{std::wstring{stage.Path()}} / "prod.keys"))
            throw std::runtime_error(ImportError(I18n::Text::KeysMissingHeaderKey));
        const bool incomplete = !keys && (!system_version || !mii_editor);
        if (incomplete) LOG_WARNING(Frontend, "File manager: firmware copy lacks SystemVersion or the Mii editor");
        if (stop.stop_requested()) throw std::runtime_error(ImportError(I18n::Text::ImportCancelledKept));
        if (!keys && stage_firmware) {
            // Copied: the player sees what the checks found and confirms before it replaces the installed one.
            std::scoped_lock lock{staged_mutex};
            staged_firmware = StagedFirmware{std::filesystem::path{std::wstring{stage.Path()}}, staged_version,
                                             static_cast<unsigned>(files.size()), total_bytes,
                                             unreadable, foreign, incomplete};
            stage = nullptr;
            LOG_INFO(Frontend, "File manager: firmware {} staged ({} files)", staged_version.display, files.size());
            return std::string{FirmwareStagedMarker};
        }
        PublishDirectory(stage, destination);
        stage = nullptr;
        if (keys) Core::Crypto::KeyManager::Instance().ReloadKeys();
        LOG_INFO(Frontend, "File manager: imported {} files ({})", files.size(), keys ? "keys" : "firmware");
        return {};
    } catch (const winrt::hresult_error& e) {
        LOG_ERROR(Frontend, "File manager: import failed HRESULT={:08X}", static_cast<unsigned>(e.code().value));
        if (stage) { try { stage.DeleteAsync().get(); } catch (...) {} }
        return I18n::ToUtf8(I18n::Tr(I18n::Text::ImportFailed));
    } catch (const std::exception& e) {
        if (stage) { try { stage.DeleteAsync().get(); } catch (...) {} }
        return e.what();
    }
}

std::optional<StagedFirmware> TakeStagedFirmware() {
    std::scoped_lock lock{staged_mutex};
    return std::exchange(staged_firmware, std::nullopt);
}

void DiscardStagedFirmware(const StagedFirmware& staged) {
    std::error_code error;
    if (staged.stage.filename().wstring().starts_with(L"registered.import")) std::filesystem::remove_all(staged.stage, error);
}

std::string CommitStagedFirmware(const StagedFirmware& staged) {
    try {
        if (!staged.stage.filename().wstring().starts_with(L"registered.import") || !std::filesystem::is_directory(staged.stage))
            return I18n::ToUtf8(I18n::Tr(I18n::Text::ImportFailed));
        RequireInternalDestination(staged.stage);
        auto stage = StorageFolder::GetFolderFromPathAsync(BrokerPath(staged.stage)).get();
        PublishDirectory(stage, FirmwarePath());
        // Checked after the swap: the installed set must name the version that was confirmed.
        const auto installed = FolderFirmwareVersion(FirmwarePath());
        if (staged.version.known && (!installed.known || installed.Packed() != staged.version.Packed())) {
            LOG_ERROR(Frontend, "File manager: installed firmware reads {}, expected {}; restoring the previous one",
                      installed.display, staged.version.display);
            RestoreFirmwareBackup();
            return I18n::ToUtf8(I18n::Tr(I18n::Text::NsFirmwareVerifyFailed));
        }
        LOG_INFO(Frontend, "File manager: firmware {} installed", staged.version.display);
        return {};
    } catch (const winrt::hresult_error& e) {
        LOG_ERROR(Frontend, "File manager: firmware install failed HRESULT={:08X}", static_cast<unsigned>(e.code().value));
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "File manager: firmware install failed: {}", e.what());
    }
    DiscardStagedFirmware(staged);
    return I18n::ToUtf8(I18n::Tr(I18n::Text::ImportFailed));
}

std::string RestoreFirmwareBackup() {
    try {
        const auto active = FirmwarePath();
        const auto backup = FirmwareBackupPath();
        RequireInternalDestination(backup);
        RecoverDirectory(active);
        if (!std::filesystem::is_directory(backup)) return I18n::ToUtf8(I18n::Tr(I18n::Text::NsFirmwareNoBackup));
        // The backup takes the place through the same steps as an install (and the same
        // recovery): the active set becomes .previous, then the backup becomes active.
        const auto previous = active.parent_path() / L"registered.previous";
        const bool exists = std::filesystem::exists(active);
        if (exists) std::filesystem::rename(active, previous);
        std::error_code error;
        std::filesystem::rename(backup, active, error);
        if (error) {
            if (exists) std::filesystem::rename(previous, active);
            return I18n::ToUtf8(I18n::Tr(I18n::Text::ImportFailed));
        }
        if (exists) std::filesystem::rename(previous, backup, error);
        LOG_INFO(Frontend, "File manager: firmware backup restored");
        return {};
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "File manager: restoring the firmware backup failed: {}", e.what());
        return I18n::ToUtf8(I18n::Tr(I18n::Text::ImportFailed));
    }
}

bool RunFileManagerGate(const std::function<void(std::string)>& diagnostic) {
    using namespace winrt::Windows::Storage;
    const auto old_keys = Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir);
    const auto old_nand = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir);
    SCOPE_EXIT {
        Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, old_keys);
        Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, old_nand);
        Core::Crypto::KeyManager::Instance().ReloadKeys();
    };
    std::string step = "prepare fixture";
    try {
        auto local = ApplicationData::Current().LocalFolder();
        auto fixture = local.CreateFolderAsync(L"file-manager-gate", CreationCollisionOption::GenerateUniqueName).get();
        // The opt-in gate leaves its tiny fixture for inspection; never changes live data.
        auto key_source = StorageFolder::GetFolderFromPathAsync(BrokerPath(old_keys)).get();
        auto firmware_source = fixture.CreateFolderAsync(L"source-firmware").get();
        FileSys::RealVfsFilesystem vfs;
        unsigned required = 0;
        for (const auto& file : std::filesystem::directory_iterator{FirmwarePath()}) {
            if (file.path().extension() != ".nca") continue;
            const auto path = file.path().u8string();
            FileSys::NCA nca{vfs.OpenFile(std::string{reinterpret_cast<const char*>(path.data()), path.size()}, FileSys::OpenMode::Read)};
            if (nca.GetStatus() != Loader::ResultStatus::Success) continue;
            if ((nca.GetTitleId() == 0x0100000000000809ULL && nca.GetType() == FileSys::NCAContentType::Data) ||
                (nca.GetTitleId() == 0x0100000000001009ULL && nca.GetType() == FileSys::NCAContentType::Program)) {
                auto source = StorageFile::GetFileFromPathAsync(BrokerPath(file.path())).get();
                source.CopyAsync(firmware_source).get();
                ++required;
            }
        }
        if (required < 2) throw std::runtime_error("Gate needs the user's installed firmware fixture.");
        const auto fixture_path = std::filesystem::path{std::wstring{fixture.Path()}};
        std::filesystem::create_directories(fixture_path / "keys");
        std::filesystem::create_directories(fixture_path / "nand");
        Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, fixture_path / "keys");
        Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, fixture_path / "nand");
        FileImportProgress progress;
        auto require = [](bool ok, const char* error) { if (!ok) throw std::runtime_error(error); };
        step = "keys import";
        const auto keys_error = ImportSystemFiles(key_source, FileImport::Keys, progress, {});
        if (!keys_error.empty()) throw std::runtime_error(keys_error);
        require(HasUsableHeaderKey(), "Imported keys not reloaded");
        step = "firmware import";
        const auto firmware_error = ImportSystemFiles(firmware_source, FileImport::Firmware, progress, {});
        if (!firmware_error.empty()) throw std::runtime_error(firmware_error);
        const auto before = ReadFileSetupStatus();
        require(before.keys_ready && before.firmware_files == required, "Setup status mismatch");
        auto bad = fixture.CreateFolderAsync(L"invalid-keys").get();
        auto invalid = bad.CreateFileAsync(L"prod.keys").get();
        FileIO::WriteTextAsync(invalid, L"header_key=0000\n").get();
        require(!ImportSystemFiles(bad, FileImport::Keys, progress, {}).empty(), "Invalid keys accepted");
        require(ValidProductionKeys(fixture_path / "keys/prod.keys"), "Working keys replaced by invalid input");
        std::stop_source cancellation;
        cancellation.request_stop();
        require(!ImportSystemFiles(firmware_source, FileImport::Firmware, progress, cancellation.get_token()).empty(), "Cancelled import committed");
        require(ReadFileSetupStatus().firmware_files == required, "Cancellation changed installed firmware");
        const auto firmware = FirmwarePath();
        const auto previous = firmware.parent_path() / L"registered.previous";
        std::filesystem::rename(firmware, previous);
        require(ReadFileSetupStatus().firmware_files == required, "Interrupted publication recovery failed");
        diagnostic("File manager gate PASS: broker copies, NCA validation, key reload, invalid input, cancellation and interrupted publication recovery; live data preserved");
        return true;
    } catch (const std::exception& e) {
        diagnostic("File manager gate FAIL " + step + ": " + std::string{e.what()});
    } catch (const winrt::hresult_error& e) {
        diagnostic("File manager gate FAIL " + step + " HRESULT=" + std::to_string(e.code().value));
    }
    return false;
}
} // namespace EdenXbox
