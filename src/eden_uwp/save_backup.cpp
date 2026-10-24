// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eden_uwp/save_backup.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

#include <windows.h>
#include <combaseapi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Storage.Streams.h>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "eden_uwp/atomic_file.h"

namespace EdenXbox::SaveBackup {
namespace {
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;
using winrt::Windows::Storage::AccessCache::StorageApplicationPermissions;
namespace fs = std::filesystem;

constexpr std::wstring_view Token = L"ns-huehue-saves";
constexpr std::wstring_view ManifestName = L"manifest.tsv";
constexpr std::uint32_t Chunk = 1024 * 1024;

/// The trees copied, relative to the NAND, with '/' separators.
constexpr std::string_view UserSaves = "user/save";
constexpr std::string_view Accounts = "system/save/8000000000000010";

fs::path LocalFolder() {
    try {
        return fs::path{ApplicationData::Current().LocalFolder().Path().c_str()};
    } catch (...) { return {}; }
}
fs::path SettingsFile() {
    const auto folder = LocalFolder();
    return folder.empty() ? fs::path{} : folder / L"ns_save_backup.txt";
}
/// Where a relative path lives on this console: the game saves under SaveDir, the rest under NAND.
fs::path LocalPath(std::string_view relative) {
    const auto base = relative.starts_with(UserSaves) ? Common::FS::GetEdenPath(Common::FS::EdenPath::SaveDir)
                                                      : Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir);
    return base / fs::path{winrt::to_hstring(relative).c_str()};
}
bool Copied(std::string_view relative) {
    const auto under = [&](std::string_view root) {
        return relative.size() > root.size() && relative.starts_with(root) && relative[root.size()] == '/';
    };
    if (under("user/save/temp")) return false;
    return under(UserSaves) || under(Accounts);
}
std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string MakeInstallId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return std::to_string(Now());
    const auto* bytes = reinterpret_cast<const unsigned char*>(&guid);
    constexpr char hex[] = "0123456789abcdef";
    std::string id;
    for (size_t i = 0; i < sizeof(guid); ++i) { id += hex[bytes[i] >> 4]; id += hex[bytes[i] & 15]; }
    return id;
}

/// A file of the copy: its size and the local write time it was copied from.
struct Stamp {
    std::uint64_t size{};
    std::int64_t time{};
    bool operator==(const Stamp&) const = default;
};
struct Manifest {
    std::string install;
    std::int64_t time{};
    std::map<std::string, Stamp> files;
};
std::string WriteManifest(const Manifest& manifest) {
    std::ostringstream out;
    out << "#ns-huehue-saves 1\ninstall\t" << manifest.install << "\ntime\t" << manifest.time << '\n';
    for (const auto& [path, stamp] : manifest.files) out << "file\t" << path << '\t' << stamp.size << '\t' << stamp.time << '\n';
    return out.str();
}
Manifest ReadManifest(std::string_view text) {
    Manifest manifest;
    std::istringstream in{std::string{text}};
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream fields{line};
        std::string kind;
        std::getline(fields, kind, '\t');
        if (kind == "install") std::getline(fields, manifest.install);
        else if (kind == "time") fields >> manifest.time;
        else if (kind == "file") {
            std::string path;
            Stamp stamp;
            if (std::getline(fields, path, '\t') && fields >> stamp.size >> stamp.time && Copied(path) &&
                path.find("..") == std::string::npos)
                manifest.files[path] = stamp;
        }
    }
    return manifest;
}
/// Games with saves: the title ids (16 hex digits, 01...) among the copy's folders.
std::uint32_t CountGames(const Manifest& manifest) {
    std::set<std::string> titles;
    for (const auto& [path, stamp] : manifest.files) {
        if (!path.starts_with(UserSaves)) continue;
        size_t start = 0;
        while (start < path.size()) {
            const auto slash = path.find('/', start);
            const auto part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (slash != std::string::npos && part.size() == 16 && part.starts_with("01") &&
                std::all_of(part.begin(), part.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
                std::string upper = part;
                for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                titles.insert(upper.substr(0, 13)); // a game's update/DLC ids share the first 13
                break;
            }
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
    }
    return static_cast<std::uint32_t>(titles.size());
}

constexpr std::wstring_view ConfigToken = L"ns-huehue-config";

/// 0.4.7: the settings' own folder (Pasta das configuracoes), picked apart from the saves'.
std::optional<StorageFolder> ConfigFolder() {
    try {
        auto list = StorageApplicationPermissions::FutureAccessList();
        if (!list.ContainsItem(winrt::hstring{ConfigToken})) return std::nullopt;
        return list.GetFolderAsync(winrt::hstring{ConfigToken}).get();
    } catch (const winrt::hresult_error& e) {
        LOG_WARNING(Frontend, "settings: the backup folder cannot be opened: {}", winrt::to_string(e.message()));
        return std::nullopt;
    }
}

std::optional<StorageFolder> PickedFolder() {
    try {
        auto list = StorageApplicationPermissions::FutureAccessList();
        if (!list.ContainsItem(winrt::hstring{Token})) return std::nullopt;
        return list.GetFolderAsync(winrt::hstring{Token}).get();
    } catch (const winrt::hresult_error& e) {
        LOG_WARNING(Frontend, "saves: the backup folder cannot be opened: {}", winrt::to_string(e.message()));
        return std::nullopt;
    }
}
std::optional<StorageFolder> ChildFolder(const StorageFolder& parent, std::wstring_view name, bool create) {
    if (create) return parent.CreateFolderAsync(winrt::hstring{name}, CreationCollisionOption::OpenIfExists).get();
    const auto item = parent.TryGetItemAsync(winrt::hstring{name}).get();
    if (!item) return std::nullopt;
    if (auto folder = item.try_as<StorageFolder>()) return folder;
    return std::nullopt;
}
std::string ReadAll(const StorageFile& file) {
    const auto buffer = FileIO::ReadBufferAsync(file).get();
    std::string bytes(buffer.Length(), '\0');
    if (!bytes.empty()) {
        auto reader = DataReader::FromBuffer(buffer);
        reader.ReadBytes(winrt::array_view<std::uint8_t>(reinterpret_cast<std::uint8_t*>(bytes.data()),
                                                         reinterpret_cast<std::uint8_t*>(bytes.data()) + bytes.size()));
    }
    return bytes;
}
std::optional<Manifest> LoadManifest(const StorageFolder& root) {
    const auto item = root.TryGetItemAsync(winrt::hstring{ManifestName}).get();
    if (!item) return std::nullopt;
    const auto file = item.try_as<StorageFile>();
    if (!file) return std::nullopt;
    return ReadManifest(ReadAll(file));
}
/// Writes `name` in `folder` through a .part file, so a drive pulled out mid-copy leaves the
/// previous copy whole. `fill` writes the bytes to the stream.
template <typename Fill>
void WriteFile(const StorageFolder& folder, std::wstring_view name, Fill&& fill) {
    const std::wstring part = std::wstring{name} + L".part";
    auto file = folder.CreateFileAsync(winrt::hstring{part}, CreationCollisionOption::ReplaceExisting).get();
    {
        auto stream = file.OpenAsync(FileAccessMode::ReadWrite).get();
        auto output = stream.GetOutputStreamAt(0);
        DataWriter writer{output};
        fill(writer);
        output.FlushAsync().get();
        writer.DetachStream();
        stream.Close();
    }
    file.RenameAsync(winrt::hstring{name}, NameCollisionOption::ReplaceExisting).get();
}
void WriteText(const StorageFolder& folder, std::wstring_view name, std::string_view text) {
    WriteFile(folder, name, [&](DataWriter& writer) {
        writer.WriteBytes(winrt::array_view<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                                                reinterpret_cast<const std::uint8_t*>(text.data()) + text.size()));
        writer.StoreAsync().get();
    });
}

/// This console's files to copy, by relative path.
std::map<std::string, Stamp> ScanLocal() {
    std::map<std::string, Stamp> files;
    for (const auto root : {UserSaves, Accounts}) {
        const auto base = LocalPath(root);
        std::error_code ec;
        if (!fs::is_directory(base, ec)) continue;
        for (fs::recursive_directory_iterator it{base, fs::directory_options::skip_permission_denied, ec}, end;
             !ec && it != end; it.increment(ec)) {
            std::error_code file_ec;
            if (!it->is_regular_file(file_ec)) continue;
            const auto name = it->path().filename().wstring();
            if (name.ends_with(L".part") || name.ends_with(L".tmp")) continue;
            // Lexical: fs::relative canonicalizes both paths, which fails on the Xbox's LocalState
            // (its volume path differs from the one the app is given), and every save was skipped:
            // the copies held only the manifest (0.3.x to 0.4.3).
            auto relative = std::string{root} + "/" + winrt::to_string(it->path().lexically_relative(base).generic_wstring());
            if (!Copied(relative) || relative.find("..") != std::string::npos) continue;
            const auto size = it->file_size(file_ec);
            const auto time = it->last_write_time(file_ec).time_since_epoch().count();
            if (file_ec) continue;
            files[std::move(relative)] = {static_cast<std::uint64_t>(size), static_cast<std::int64_t>(time)};
        }
    }
    return files;
}

/// The folder of `relative`'s file inside the copy, made as needed (cached by folder path).
StorageFolder FolderFor(const StorageFolder& root, std::string_view relative,
                        std::map<std::string, StorageFolder, std::less<>>& cache) {
    const auto slash = relative.rfind('/');
    const auto directory = relative.substr(0, slash);
    if (const auto found = cache.find(directory); found != cache.end()) return found->second;
    StorageFolder folder = root;
    std::string built;
    size_t start = 0;
    while (start <= directory.size()) {
        const auto next = directory.find('/', start);
        const auto part = directory.substr(start, next == std::string_view::npos ? std::string_view::npos : next - start);
        if (!built.empty()) built += '/';
        built += part;
        if (const auto found = cache.find(built); found != cache.end()) folder = found->second;
        else {
            folder = *ChildFolder(folder, winrt::to_hstring(part), true);
            cache.emplace(built, folder);
        }
        if (next == std::string_view::npos) break;
        start = next + 1;
    }
    return folder;
}

void Remember(Settings& settings, const Manifest& manifest) {
    settings.last_time = manifest.time;
    settings.last_files = static_cast<std::uint32_t>(manifest.files.size());
    settings.last_games = CountGames(manifest);
    SaveSettings(settings);
}
} // namespace

Settings LoadSettings() {
    Settings settings;
    const auto file = SettingsFile();
    if (!file.empty()) {
        if (const auto text = ReadWholeFile(file)) {
            std::istringstream in{*text};
            for (std::string line; std::getline(in, line);) {
                const auto tab = line.find('\t');
                if (tab == std::string::npos) continue;
                const auto key = line.substr(0, tab), value = line.substr(tab + 1);
                try {
                    if (key == "automatic") settings.automatic = value != "0";
                    else if (key == "install") settings.install = value;
                    else if (key == "time") settings.last_time = std::stoll(value);
                    else if (key == "files") settings.last_files = static_cast<std::uint32_t>(std::stoul(value));
                    else if (key == "games") settings.last_games = static_cast<std::uint32_t>(std::stoul(value));
                    else if (key == "folder") settings.folder = winrt::to_hstring(value).c_str();
                } catch (...) {}
            }
        }
    }
    if (settings.install.empty()) {
        settings.install = MakeInstallId();
        SaveSettings(settings);
    }
    return settings;
}

bool SaveSettings(const Settings& settings) {
    const auto file = SettingsFile();
    if (file.empty()) return false;
    std::ostringstream out;
    out << "automatic\t" << (settings.automatic ? 1 : 0) << "\ninstall\t" << settings.install << "\ntime\t"
        << settings.last_time << "\nfiles\t" << settings.last_files << "\ngames\t" << settings.last_games
        << "\nfolder\t" << winrt::to_string(settings.folder) << '\n';
    return WriteFileAtomically(file, out.str());
}

bool HasConfigFolder() {
    try {
        return StorageApplicationPermissions::FutureAccessList().ContainsItem(winrt::hstring{ConfigToken});
    } catch (...) { return false; }
}

std::wstring ConfigFolderPath() {
    try {
        for (const auto& entry : StorageApplicationPermissions::FutureAccessList().Entries())
            if (std::wstring_view{entry.Token} == ConfigToken) return std::wstring{entry.Metadata};
    } catch (...) {}
    return {};
}

void RememberConfigFolder(const StorageFolder& folder) {
    StorageApplicationPermissions::FutureAccessList().AddOrReplace(winrt::hstring{ConfigToken}, folder, folder.Path());
    LOG_INFO(Frontend, "settings: backup folder {}", winrt::to_string(folder.Path()));
}

bool HasFolder() {
    try {
        return StorageApplicationPermissions::FutureAccessList().ContainsItem(winrt::hstring{Token});
    } catch (...) { return false; }
}

void RememberFolder(const StorageFolder& folder) {
    StorageApplicationPermissions::FutureAccessList().AddOrReplace(winrt::hstring{Token}, folder, folder.Path());
    auto settings = LoadSettings();
    settings.folder = folder.Path().c_str();
    settings.last_time = 0;
    settings.last_files = settings.last_games = 0;
    SaveSettings(settings);
    LOG_INFO(Frontend, "saves: backup folder {}", winrt::to_string(folder.Path()));
}

FolderInfo Inspect() {
    FolderInfo info;
    try {
        const auto picked = PickedFolder();
        if (!picked) return info;
        info.available = true;
        const auto root = ChildFolder(*picked, FolderName, false);
        if (!root) return info;
        const auto manifest = LoadManifest(*root);
        if (!manifest) return info;
        info.has_backup = !manifest->files.empty();
        info.other_install = manifest->install != LoadSettings().install;
        info.time = manifest->time;
        info.files = static_cast<std::uint32_t>(manifest->files.size());
        info.games = CountGames(*manifest);
    } catch (const winrt::hresult_error& e) {
        LOG_WARNING(Frontend, "saves: reading the backup folder failed: {}", winrt::to_string(e.message()));
        info.available = false;
    }
    return info;
}

Result Backup(bool replace_other, Progress& progress, std::stop_token stop) {
    Result result;
    progress.done = progress.total = 0;
    auto settings = LoadSettings();
    if (!HasFolder()) { result.outcome = Outcome::NoFolder; return result; }
    const auto picked = PickedFolder();
    if (!picked) { result.outcome = Outcome::FolderUnavailable; return result; }
    try {
        const auto root = *ChildFolder(*picked, FolderName, true);
        auto manifest = LoadManifest(root).value_or(Manifest{});
        if (!manifest.files.empty() && manifest.install != settings.install && !replace_other) {
            result.outcome = Outcome::OtherInstall;
            result.files = static_cast<std::uint32_t>(manifest.files.size());
            result.games = CountGames(manifest);
            return result;
        }
        const auto local = ScanLocal();
        LOG_INFO(Frontend, "saves: {} files on this console to compare", local.size());
        std::vector<std::pair<std::string, Stamp>> changed;
        for (const auto& [path, stamp] : local) {
            const auto found = manifest.files.find(path);
            if (found == manifest.files.end() || !(found->second == stamp)) changed.emplace_back(path, stamp);
        }
        progress.total = static_cast<std::uint32_t>(changed.size());
        manifest.install = settings.install;
        std::map<std::string, StorageFolder, std::less<>> folders;
        std::vector<char> buffer(Chunk);
        for (const auto& [path, stamp] : changed) {
            if (stop.stop_requested()) break;
            const auto folder = FolderFor(root, path, folders);
            const auto name = winrt::to_hstring(path.substr(path.rfind('/') + 1));
            std::ifstream in{LocalPath(path), std::ios::binary};
            if (!in) {
                LOG_WARNING(Frontend, "saves: cannot read {}", path);
                continue; // removed while copying: the next copy sees it gone
            }
            WriteFile(folder, name, [&](DataWriter& writer) {
                while (in) {
                    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                    const auto read = static_cast<size_t>(in.gcount());
                    if (read == 0) break;
                    writer.WriteBytes(winrt::array_view<const std::uint8_t>(
                        reinterpret_cast<const std::uint8_t*>(buffer.data()),
                        reinterpret_cast<const std::uint8_t*>(buffer.data()) + read));
                    writer.StoreAsync().get();
                }
            });
            manifest.files[path] = stamp;
            ++result.copied;
            progress.done = result.copied;
        }
        // The manifest goes last: a copy cut short is redone from where it stopped.
        manifest.time = Now();
        WriteText(root, ManifestName, WriteManifest(manifest));
        Remember(settings, manifest);
        result.files = static_cast<std::uint32_t>(manifest.files.size());
        result.games = CountGames(manifest);
        result.outcome = stop.stop_requested() ? Outcome::Stopped : Outcome::Done;
        LOG_INFO(Frontend, "saves: backup copied {} of {} files ({} games)", result.copied, result.files, result.games);
    } catch (const winrt::hresult_error& e) {
        result.outcome = Outcome::Failed;
        result.error = winrt::to_string(e.message());
        LOG_ERROR(Frontend, "saves: backup failed: {}", result.error);
    } catch (const std::exception& e) {
        result.outcome = Outcome::Failed;
        result.error = e.what();
        LOG_ERROR(Frontend, "saves: backup failed: {}", result.error);
    }
    return result;
}

Result Restore(Progress& progress, std::stop_token stop) {
    Result result;
    progress.done = progress.total = 0;
    auto settings = LoadSettings();
    if (!HasFolder()) { result.outcome = Outcome::NoFolder; return result; }
    const auto picked = PickedFolder();
    if (!picked) { result.outcome = Outcome::FolderUnavailable; return result; }
    try {
        const auto root = ChildFolder(*picked, FolderName, false);
        if (!root) { result.outcome = Outcome::NothingToRestore; return result; }
        // Every file of the copy, walking its folders (the manifest may be missing or behind).
        std::vector<std::pair<std::string, StorageFile>> files;
        std::vector<std::pair<std::string, StorageFolder>> pending;
        for (const auto top : {UserSaves, Accounts}) {
            StorageFolder folder = *root;
            bool found = true;
            size_t start = 0;
            while (found && start < top.size()) {
                const auto slash = top.find('/', start);
                const auto part = top.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
                const auto child = ChildFolder(folder, winrt::to_hstring(part), false);
                if (!child) found = false;
                else folder = *child;
                if (slash == std::string_view::npos) break;
                start = slash + 1;
            }
            if (found) pending.emplace_back(std::string{top}, folder);
        }
        while (!pending.empty()) {
            if (stop.stop_requested()) { result.outcome = Outcome::Stopped; return result; }
            auto [path, folder] = pending.back();
            pending.pop_back();
            for (const auto& child : folder.GetFoldersAsync().get())
                pending.emplace_back(path + "/" + winrt::to_string(child.Name()), child);
            for (const auto& file : folder.GetFilesAsync().get()) {
                const auto name = std::wstring{file.Name()};
                if (name.ends_with(L".part")) continue;
                auto relative = path + "/" + winrt::to_string(file.Name());
                if (Copied(relative) && relative.find("..") == std::string::npos) files.emplace_back(std::move(relative), file);
            }
        }
        if (files.empty()) { result.outcome = Outcome::NothingToRestore; return result; }
        progress.total = static_cast<std::uint32_t>(files.size());

        // The saves being replaced are kept first (one generation, in LocalState).
        const auto keep = LocalFolder() / L"ns_saves_before_restore";
        {
            std::error_code ec;
            fs::remove_all(keep, ec);
            for (const auto top : {UserSaves, Accounts}) {
                const auto from = LocalPath(top);
                if (!fs::is_directory(from, ec)) continue;
                const auto to = keep / fs::path{winrt::to_hstring(top).c_str()};
                fs::create_directories(to, ec);
                fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    result.outcome = Outcome::Failed;
                    result.error = "keeping the current saves: " + ec.message();
                    LOG_ERROR(Frontend, "saves: {}", result.error);
                    return result;
                }
            }
        }

        Manifest manifest;
        manifest.install = settings.install;
        for (const auto& [relative, file] : files) {
            if (stop.stop_requested()) break;
            const auto target = LocalPath(relative);
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            auto part = target;
            part += ".part";
            {
                auto stream = file.OpenReadAsync().get();
                DataReader reader{stream.GetInputStreamAt(0)};
                std::ofstream out{part, std::ios::binary | std::ios::trunc};
                if (!out) throw std::runtime_error("cannot write " + relative);
                std::vector<std::uint8_t> buffer;
                for (;;) {
                    const auto loaded = reader.LoadAsync(Chunk).get();
                    if (loaded == 0) break;
                    buffer.resize(loaded);
                    reader.ReadBytes(buffer);
                    out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(loaded));
                }
                if (!out.flush()) throw std::runtime_error("cannot write " + relative);
            }
            fs::rename(part, target, ec);
            if (ec) {
                fs::remove(part, ec);
                throw std::runtime_error("cannot replace " + relative);
            }
            const auto size = fs::file_size(target, ec);
            const auto time = fs::last_write_time(target, ec).time_since_epoch().count();
            manifest.files[relative] = {static_cast<std::uint64_t>(size), static_cast<std::int64_t>(time)};
            ++result.copied;
            progress.done = result.copied;
        }
        if (stop.stop_requested()) {
            // Cut short: the folder stays another installation's, so no automatic copy writes over
            // the files not yet back. Restaurar again finishes it.
            result.outcome = Outcome::Stopped;
            return result;
        }
        // The folder now belongs to this installation; what came back is not copied again.
        manifest.time = Now();
        WriteText(*root, ManifestName, WriteManifest(manifest));
        Remember(settings, manifest);
        result.files = static_cast<std::uint32_t>(manifest.files.size());
        result.games = CountGames(manifest);
        result.outcome = stop.stop_requested() ? Outcome::Stopped : Outcome::Done;
        LOG_INFO(Frontend, "saves: restored {} files ({} games)", result.copied, result.games);
    } catch (const winrt::hresult_error& e) {
        result.outcome = Outcome::Failed;
        result.error = winrt::to_string(e.message());
        LOG_ERROR(Frontend, "saves: restore failed: {}", result.error);
    } catch (const std::exception& e) {
        result.outcome = Outcome::Failed;
        result.error = e.what();
        LOG_ERROR(Frontend, "saves: restore failed: {}", result.error);
    }
    return result;
}

namespace {
constexpr std::wstring_view ConfigFolderName = L"NS-HUEHUE-config";
constexpr std::wstring_view ConfigValuesName = L"settings.tsv";
/// The LocalState files the settings copy carries (not the install id, the session marker nor
/// the scan cache: those belong to this console).
constexpr std::array<std::wstring_view, 4> ConfigFiles{L"ns_profiles.tsv", L"ns_favorites.txt", L"ns_recent.txt",
                                                       L"ns_online.cfg"};
std::string Escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}
std::string Unescape(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) { out += text[i]; continue; }
        const char next = text[++i];
        out += next == 't' ? '\t' : next == 'n' ? '\n' : next == 'r' ? '\r' : next;
    }
    return out;
}
} // namespace

Result BackupSettings() {
    Result result;
    if (!HasConfigFolder()) { result.outcome = Outcome::NoFolder; return result; }
    const auto picked = ConfigFolder();
    if (!picked) { result.outcome = Outcome::FolderUnavailable; return result; }
    try {
        const auto root = *ChildFolder(*picked, ConfigFolderName, true);
        // LocalSettings: every value of a plain type, as "key<TAB>type<TAB>value".
        std::ostringstream out;
        out << "#ns-huehue-config 1\n";
        std::uint32_t values = 0;
        for (const auto& pair : ApplicationData::Current().LocalSettings().Values()) {
            const std::string key = winrt::to_string(pair.Key());
            const auto value = pair.Value();
            if (key.find('\t') != std::string::npos) continue;
            if (const auto text = value.try_as<winrt::Windows::Foundation::IReference<winrt::hstring>>())
                out << key << "\ts\t" << Escape(winrt::to_string(text.Value())) << '\n';
            else if (const auto flag = value.try_as<winrt::Windows::Foundation::IReference<bool>>())
                out << key << "\tb\t" << (flag.Value() ? 1 : 0) << '\n';
            else if (const auto number = value.try_as<winrt::Windows::Foundation::IReference<std::int32_t>>())
                out << key << "\ti\t" << number.Value() << '\n';
            else if (const auto wide = value.try_as<winrt::Windows::Foundation::IReference<std::int64_t>>())
                out << key << "\tl\t" << wide.Value() << '\n';
            else if (const auto real = value.try_as<winrt::Windows::Foundation::IReference<double>>())
                out << key << "\td\t" << real.Value() << '\n';
            else continue;
            ++values;
        }
        WriteText(root, ConfigValuesName, out.str());
        for (const auto name : ConfigFiles) {
            const auto path = LocalFolder() / fs::path{std::wstring{name}};
            if (const auto text = ReadWholeFile(path)) {
                WriteText(root, name, *text);
                ++result.copied;
            }
        }
        result.files = values;
        result.outcome = Outcome::Done;
        LOG_INFO(Frontend, "settings: copied {} values and {} files", values, result.copied);
    } catch (const winrt::hresult_error& e) {
        result.outcome = Outcome::Failed;
        result.error = winrt::to_string(e.message());
        LOG_ERROR(Frontend, "settings: copy failed: {}", result.error);
    } catch (const std::exception& e) {
        result.outcome = Outcome::Failed;
        result.error = e.what();
        LOG_ERROR(Frontend, "settings: copy failed: {}", result.error);
    }
    return result;
}

Result RestoreSettings() {
    Result result;
    if (!HasConfigFolder()) { result.outcome = Outcome::NoFolder; return result; }
    const auto picked = ConfigFolder();
    if (!picked) { result.outcome = Outcome::FolderUnavailable; return result; }
    try {
        const auto root = ChildFolder(*picked, ConfigFolderName, false);
        const auto item = root ? root->TryGetItemAsync(winrt::hstring{ConfigValuesName}).get() : nullptr;
        const auto file = item ? item.try_as<StorageFile>() : nullptr;
        if (!file) { result.outcome = Outcome::NothingToRestore; return result; }
        const auto values = ApplicationData::Current().LocalSettings().Values();
        std::istringstream in{ReadAll(file)};
        std::uint32_t restored = 0;
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            const auto first = line.find('\t');
            const auto second = first == std::string::npos ? first : line.find('\t', first + 1);
            if (second == std::string::npos) continue;
            const auto key = winrt::to_hstring(line.substr(0, first));
            const auto type = line.substr(first + 1, second - first - 1);
            const auto text = line.substr(second + 1);
            try {
                if (type == "s") values.Insert(key, winrt::box_value(winrt::to_hstring(Unescape(text))));
                else if (type == "b") values.Insert(key, winrt::box_value(text == "1"));
                else if (type == "i") values.Insert(key, winrt::box_value(static_cast<std::int32_t>(std::stol(text))));
                else if (type == "l") values.Insert(key, winrt::box_value(static_cast<std::int64_t>(std::stoll(text))));
                else if (type == "d") values.Insert(key, winrt::box_value(std::stod(text)));
                else continue;
                ++restored;
            } catch (...) {}
        }
        for (const auto name : ConfigFiles) {
            const auto copy = root->TryGetItemAsync(winrt::hstring{name}).get();
            const auto copy_file = copy ? copy.try_as<StorageFile>() : nullptr;
            if (!copy_file) continue;
            if (WriteFileAtomically(LocalFolder() / fs::path{std::wstring{name}}, ReadAll(copy_file))) ++result.copied;
        }
        result.files = restored;
        result.outcome = Outcome::Done;
        LOG_INFO(Frontend, "settings: restored {} values and {} files", restored, result.copied);
    } catch (const winrt::hresult_error& e) {
        result.outcome = Outcome::Failed;
        result.error = winrt::to_string(e.message());
        LOG_ERROR(Frontend, "settings: restore failed: {}", result.error);
    } catch (const std::exception& e) {
        result.outcome = Outcome::Failed;
        result.error = e.what();
        LOG_ERROR(Frontend, "settings: restore failed: {}", result.error);
    }
    return result;
}

} // namespace EdenXbox::SaveBackup
