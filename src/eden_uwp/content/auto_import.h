// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE: updates and DLC the player drops in the games folder are installed by themselves.
// After a library scan, each package of the internal games folder is looked at once (the result is
// kept by path, size and date, so later scans skip it): a game stays in the library, an update or a
// DLC is registered with the content store in place (the file is not copied) and leaves the list.
// It is the same import the Updates and DLC pages run, without the screens.

#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace EdenXbox::Content {

struct AutoImportResult {
    /// Packages that are updates or DLC: the library leaves them out of the list of games.
    std::vector<std::string> content_files;
    std::size_t added{};  ///< Updates and DLC registered by this pass.
    std::size_t failed{}; ///< Packages that could not be read or imported.
};

/// Looks at `paths` (absolute paths of the internal games folder). `cache` keeps what each file
/// was. Runs on a worker, never beside the metadata reader (both use the key manager).
AutoImportResult AutoImportPackages(const std::vector<std::string>& paths, const std::filesystem::path& cache,
                                    std::stop_token stop);

} // namespace EdenXbox::Content
