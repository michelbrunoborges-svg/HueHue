// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eden_uwp/shader_backup.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <vector>

#include <fmt/format.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Storage.FileProperties.h>

#include "common/fs/path_util.h"
#include "common/logging.h"

namespace EdenXbox::ShaderBackup {
namespace {
using namespace winrt::Windows::Storage;
using winrt::Windows::Storage::AccessCache::StorageApplicationPermissions;
namespace fs = std::filesystem;

constexpr std::wstring_view Token = L"ns-huehue-shaders";
constexpr auto SyncKey = L"ns_shader_sync";

fs::path LocalRoot() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir);
}
/// A title's folder name, as the pipeline caches make it (16 lower-case hex digits).
std::wstring TitleName(std::uint64_t title_id) {
    const auto text = fmt::format("{:016x}", title_id);
    return std::wstring(text.begin(), text.end());
}
bool IsTitleName(std::wstring_view name) {
    return name.size() == 16 && std::all_of(name.begin(), name.end(), [](wchar_t c) {
               return c < 128 && std::isxdigit(static_cast<unsigned char>(c));
           });
}
/// The cache files of a title folder: the pipelines (*.bin) and their hot lists (*_hot.bin).
bool IsCacheName(std::wstring_view name) {
    return name.size() > 4 && name.ends_with(L".bin");
}
bool IsHotName(std::wstring_view name) {
    return name.ends_with(L"_hot.bin");
}
/// "d3d12.bin" -> "d3d12_hot.bin": copied with its cache, whatever its size.
std::wstring HotFor(std::wstring_view name) {
    return std::wstring{name.substr(0, name.size() - 4)} + L"_hot.bin";
}

std::optional<StorageFolder> PickedFolder() {
    try {
        auto list = StorageApplicationPermissions::FutureAccessList();
        if (!list.ContainsItem(winrt::hstring{Token})) return std::nullopt;
        return list.GetFolderAsync(winrt::hstring{Token}).get();
    } catch (const winrt::hresult_error& e) {
        LOG_WARNING(Frontend, "shaders: the folder cannot be opened: {}", winrt::to_string(e.message()));
        return std::nullopt;
    }
}
std::optional<StorageFolder> Child(const StorageFolder& parent, std::wstring_view name, bool create) {
    if (create) return parent.CreateFolderAsync(winrt::hstring{name}, CreationCollisionOption::OpenIfExists).get();
    const auto item = parent.TryGetItemAsync(winrt::hstring{name}).get();
    if (!item) return std::nullopt;
    if (auto folder = item.try_as<StorageFolder>()) return folder;
    return std::nullopt;
}
std::optional<StorageFile> FileIn(const StorageFolder& folder, std::wstring_view name) {
    const auto item = folder.TryGetItemAsync(winrt::hstring{name}).get();
    if (!item) return std::nullopt;
    if (auto file = item.try_as<StorageFile>()) return file;
    return std::nullopt;
}
std::uint64_t SizeOf(const StorageFile& file) {
    return file.GetBasicPropertiesAsync().get().Size();
}
std::uint64_t LocalSize(const fs::path& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    return ec ? 0 : static_cast<std::uint64_t>(size);
}

/// A local file to the drive, through a .part file so a pulled drive keeps the old copy whole.
void CopyOut(const fs::path& source, const StorageFolder& folder, std::wstring_view name) {
    auto file = StorageFile::GetFileFromPathAsync(winrt::hstring{source.wstring()}).get();
    auto copy = file.CopyAsync(folder, winrt::hstring{std::wstring{name} + L".part"},
                               NameCollisionOption::ReplaceExisting).get();
    copy.RenameAsync(winrt::hstring{name}, NameCollisionOption::ReplaceExisting).get();
}
/// A file on the drive to the console, likewise.
void CopyIn(const StorageFile& file, const fs::path& folder, std::wstring_view name) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    auto local = StorageFolder::GetFolderFromPathAsync(winrt::hstring{folder.wstring()}).get();
    const std::wstring part = std::wstring{name} + L".part";
    file.CopyAsync(local, winrt::hstring{part}, NameCollisionOption::ReplaceExisting).get();
    fs::rename(folder / part, folder / std::wstring{name}, ec);
    if (ec) throw std::runtime_error("rename " + ec.message());
}

/// The titles to look at on this console.
std::vector<std::wstring> LocalTitles(std::optional<std::uint64_t> title_id) {
    if (title_id) return {TitleName(*title_id)};
    std::vector<std::wstring> titles;
    std::error_code ec;
    for (fs::directory_iterator it{LocalRoot(), ec}, end; !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        if (!it->is_directory(entry_ec)) continue;
        auto name = it->path().filename().wstring();
        if (IsTitleName(name)) titles.push_back(std::move(name));
    }
    return titles;
}

Result Fail(Outcome outcome, std::string error = {}) {
    Result result;
    result.outcome = outcome;
    result.error = std::move(error);
    return result;
}
} // namespace

bool HasFolder() {
    try {
        return StorageApplicationPermissions::FutureAccessList().ContainsItem(winrt::hstring{Token});
    } catch (...) { return false; }
}

std::wstring FolderPath() {
    try {
        for (const auto& entry : StorageApplicationPermissions::FutureAccessList().Entries())
            if (std::wstring_view{entry.Token} == Token) return std::wstring{entry.Metadata};
    } catch (...) {}
    return {};
}

void RememberFolder(const StorageFolder& folder) {
    StorageApplicationPermissions::FutureAccessList().AddOrReplace(winrt::hstring{Token}, folder, folder.Path());
    LOG_INFO(Frontend, "shaders: folder {}", winrt::to_string(folder.Path()));
}

bool AutoSync() {
    try {
        const auto values = ApplicationData::Current().LocalSettings().Values();
        if (values.HasKey(SyncKey)) return winrt::unbox_value_or<bool>(values.Lookup(SyncKey), true);
    } catch (...) {}
    return true;
}

void SetAutoSync(bool on) {
    try {
        ApplicationData::Current().LocalSettings().Values().Insert(SyncKey, winrt::box_value(on));
    } catch (...) {}
}

Result Backup(std::optional<std::uint64_t> title_id, std::stop_token stop) {
    if (!HasFolder()) return Fail(Outcome::NoFolder);
    const auto picked = PickedFolder();
    if (!picked) return Fail(Outcome::FolderUnavailable);
    Result result;
    try {
        std::optional<StorageFolder> root;
        for (const auto& title : LocalTitles(title_id)) {
            if (stop.stop_requested()) return Fail(Outcome::Stopped);
            const auto local = LocalRoot() / title;
            std::error_code ec;
            if (!fs::is_directory(local, ec)) continue;
            std::optional<StorageFolder> target;
            bool copied_any = false;
            for (fs::directory_iterator it{local, ec}, end; !ec && it != end; it.increment(ec)) {
                const auto name = it->path().filename().wstring();
                if (!IsCacheName(name) || IsHotName(name)) continue;
                const std::uint64_t size = LocalSize(it->path());
                if (size == 0) continue;
                if (!root) root = Child(*picked, FolderName, true);
                if (!target) target = Child(*root, title, true);
                const auto existing = FileIn(*target, name);
                if (existing && SizeOf(*existing) >= size) continue; // the larger one stays
                CopyOut(it->path(), *target, name);
                ++result.files;
                result.bytes += size;
                copied_any = true;
                const auto hot = local / HotFor(name);
                if (LocalSize(hot) != 0) {
                    CopyOut(hot, *target, HotFor(name));
                    ++result.files;
                    result.bytes += LocalSize(hot);
                }
            }
            if (copied_any) ++result.games;
        }
        result.outcome = Outcome::Done;
        LOG_INFO(Frontend, "shaders: {} files of {} games to the drive ({} MiB)", result.files, result.games,
                 result.bytes >> 20);
    } catch (const winrt::hresult_error& e) {
        result.outcome = Outcome::Failed;
        result.error = winrt::to_string(e.message());
        LOG_ERROR(Frontend, "shaders: copy to the drive failed: {}", result.error);
    } catch (const std::exception& e) {
        result.outcome = Outcome::Failed;
        result.error = e.what();
        LOG_ERROR(Frontend, "shaders: copy to the drive failed: {}", result.error);
    }
    return result;
}

Result Restore(std::optional<std::uint64_t> title_id, std::stop_token stop) {
    if (!HasFolder()) return Fail(Outcome::NoFolder);
    const auto picked = PickedFolder();
    if (!picked) return Fail(Outcome::FolderUnavailable);
    Result result;
    try {
        const auto root = Child(*picked, FolderName, false);
        if (!root) return Fail(Outcome::NothingToRestore);
        std::vector<StorageFolder> titles;
        if (title_id) {
            if (auto folder = Child(*root, TitleName(*title_id), false)) titles.push_back(*folder);
        } else {
            for (const auto& folder : root->GetFoldersAsync().get())
                if (IsTitleName(folder.Name())) titles.push_back(folder);
        }
        bool any_cache = false;
        for (const auto& folder : titles) {
            if (stop.stop_requested()) return Fail(Outcome::Stopped);
            const auto local = LocalRoot() / std::wstring{folder.Name()};
            bool copied_any = false;
            for (const auto& file : folder.GetFilesAsync().get()) {
                const std::wstring name{file.Name()};
                if (!IsCacheName(name) || IsHotName(name)) continue;
                any_cache = true;
                const std::uint64_t size = SizeOf(file);
                if (size == 0 || LocalSize(local / name) >= size) continue; // the larger one stays
                CopyIn(file, local, name);
                ++result.files;
                result.bytes += size;
                copied_any = true;
                if (const auto hot = FileIn(folder, HotFor(name))) {
                    CopyIn(*hot, local, HotFor(name));
                    ++result.files;
                    result.bytes += SizeOf(*hot);
                }
            }
            if (copied_any) ++result.games;
        }
        result.outcome = any_cache ? Outcome::Done : Outcome::NothingToRestore;
        LOG_INFO(Frontend, "shaders: {} files of {} games from the drive ({} MiB)", result.files, result.games,
                 result.bytes >> 20);
    } catch (const winrt::hresult_error& e) {
        result.outcome = Outcome::Failed;
        result.error = winrt::to_string(e.message());
        LOG_ERROR(Frontend, "shaders: copy from the drive failed: {}", result.error);
    } catch (const std::exception& e) {
        result.outcome = Outcome::Failed;
        result.error = e.what();
        LOG_ERROR(Frontend, "shaders: copy from the drive failed: {}", result.error);
    }
    return result;
}

} // namespace EdenXbox::ShaderBackup
