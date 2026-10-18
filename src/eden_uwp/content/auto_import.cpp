// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden_uwp/content/auto_import.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "common/logging.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/registered_cache.h"
#include "eden_uwp/content/content_service.h"
#include "eden_uwp/uwp_rom_storage.h"

namespace EdenXbox::Content {
namespace {

/// What a package was when it was last looked at.
struct Seen {
    std::uint64_t size{};
    std::int64_t stamp{};
    std::string kind; ///< "base:<application>", "content" or "other".
};

std::map<std::string, Seen> LoadSeen(const std::filesystem::path& file) {
    std::map<std::string, Seen> seen;
    std::ifstream in(file, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        std::istringstream fields(line);
        std::string path, size, stamp, kind;
        if (!std::getline(fields, path, '\t') || !std::getline(fields, size, '\t') ||
            !std::getline(fields, stamp, '\t') || !std::getline(fields, kind))
            continue;
        try {
            seen[path] = {std::stoull(size), std::stoll(stamp), kind};
        } catch (...) {}
    }
    return seen;
}

void SaveSeen(const std::filesystem::path& file, const std::map<std::string, Seen>& seen) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    for (const auto& [path, item] : seen) out << path << '\t' << item.size << '\t' << item.stamp << '\t' << item.kind << '\n';
}

std::string Hex(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(value));
    return text;
}

/// The application a game package holds, or 0 for an update, a DLC or anything else.
std::uint64_t BaseApplication(FileSys::VfsFilesystem& vfs, const std::string& path) {
    using namespace FileSys;
    const auto file = vfs.OpenFile(path, OpenMode::Read);
    ManualContentProvider provider;
    if (!file || !provider.AddEntriesFromContainer(file)) return 0;
    const auto programs = provider.ListEntriesFilter(TitleType::Application, ContentRecordType::Program, {});
    return programs.empty() ? 0 : GetBaseTitleID(programs.front().title_id);
}

} // namespace

AutoImportResult AutoImportPackages(const std::vector<std::string>& paths, const std::filesystem::path& cache,
                                    std::stop_token stop) {
    AutoImportResult result;
    auto seen = LoadSeen(cache);
    const auto vfs = MakeUwpFilesystem();
    std::map<std::uint64_t, std::string> bases; // application -> its package
    std::vector<std::string> fresh;              // packages that are not games, not imported yet

    for (const auto& path : paths) {
        if (stop.stop_requested()) return result;
        std::error_code ec;
        const std::filesystem::path native{std::u8string{path.begin(), path.end()}};
        const auto size = std::filesystem::file_size(native, ec);
        if (ec) continue;
        const auto stamp = std::filesystem::last_write_time(native, ec).time_since_epoch().count();
        const auto found = seen.find(path);
        if (found != seen.end() && found->second.size == size && found->second.stamp == stamp) {
            const auto& kind = found->second.kind;
            if (kind == "content") result.content_files.push_back(path);
            else if (kind.starts_with("base:")) {
                try { bases[std::stoull(kind.substr(5), nullptr, 16)] = path; } catch (...) {}
            }
            continue;
        }
        if (const auto application = BaseApplication(*vfs, path)) {
            bases[application] = path;
            seen[path] = {size, stamp, "base:" + Hex(application)};
        } else {
            fresh.push_back(path);
        }
    }

    if (!fresh.empty()) {
        auto store = DefaultStore();
        Catalog catalog;
        if (store.Load(catalog) != Error::None) {
            result.failed = fresh.size();
            SaveSeen(cache, seen);
            return result;
        }
        std::map<std::uint64_t, std::uint32_t> base_versions;
        const auto base_version = [&](std::uint64_t application) -> std::uint32_t {
            const auto known = base_versions.find(application);
            if (known != base_versions.end()) return known->second;
            const auto package = bases.find(application);
            const std::uint32_t version = package == bases.end() ? 0 : PackageBaseVersion(*vfs, package->second, application);
            base_versions[application] = version;
            return version;
        };
        // An application the content store starts managing first keeps what its own package
        // carries (an XCI with an update inside), as the Updates and DLC pages do.
        const auto manage = [&](std::uint64_t application) {
            auto& managed = catalog.managed_applications;
            if (std::find(managed.begin(), managed.end(), application) != managed.end()) return;
            const auto package = bases.find(application);
            if (package != bases.end()) {
                auto embedded = InspectPackage(*vfs, package->second, false, stop);
                for (auto& item : embedded.contents) {
                    if (item.error != Error::None || PlanImport(catalog.revisions, item.revision) != ImportAction::Add)
                        continue;
                    ExecuteImport(store, catalog, *vfs, item, false, true, stop, {}, base_version(application));
                }
            }
            if (std::find(managed.begin(), managed.end(), application) == managed.end()) {
                managed.push_back(application);
                store.Save(catalog);
            }
        };
        for (const auto& path : fresh) {
            if (stop.stop_requested()) break;
            auto inspection = InspectPackage(*vfs, path, false, stop);
            bool content = false, broken = inspection.error != Error::None;
            for (auto& item : inspection.contents) {
                if (item.error != Error::None) { broken = true; continue; }
                const auto application = item.revision.application;
                const bool have_base = bases.contains(application);
                if (have_base) manage(application);
                const auto plan = PlanImport(catalog.revisions, item.revision);
                if (plan == ImportAction::Conflict) { broken = true; continue; }
                const auto error = plan == ImportAction::Duplicate ? Error::None
                    : ExecuteImport(store, catalog, *vfs, item, false, have_base, stop, {}, base_version(application));
                if (error == Error::None) {
                    content = true;
                    if (plan == ImportAction::Add) ++result.added;
                } else {
                    broken = true;
                }
            }
            std::error_code ec;
            const std::filesystem::path native{std::u8string{path.begin(), path.end()}};
            const auto size = std::filesystem::file_size(native, ec);
            const auto stamp = std::filesystem::last_write_time(native, ec).time_since_epoch().count();
            if (content) {
                result.content_files.push_back(path);
                seen[path] = {size, stamp, "content"};
            } else if (!broken && inspection.contents.empty()) {
                seen[path] = {size, stamp, "other"}; // Homebrew, or nothing the store takes.
            } else {
                ++result.failed; // Missing keys or a damaged file: looked at again next time.
            }
        }
        LOG_INFO(Frontend, "Content: auto-import added={} failed={}", result.added, result.failed);
    }
    SaveSeen(cache, seen);
    return result;
}

} // namespace EdenXbox::Content
