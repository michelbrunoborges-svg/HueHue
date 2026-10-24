// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE theme catalog: beyond the three built-in themes (theme.h),
// themes come as packs, one folder each with a versioned manifest and its art:
//
//   <folder>/theme.json   {"schemaVersion": 1, "id": "ns-huehue", "name": "NS HUEHUE", "version": "1.0",
//                          "base": "switch-dark" | "switch-light", "background": "background.png",
//                          "thumbnail": "thumbnail.png" (optional),
//                          "colors": {"background": "#2D2D2D", "foreground": "#FAFAFA", "surface": "#383838",
//                                     "muted": "#C2C2C2", "border": "#626262", "focus": "#27D8E8",
//                                     "selection": "#17484D", "accent": "#27D8E8"} (each optional)}
//   <folder>/background.png (16:9, 1920x1080 or more; .png or .jpg)
//
// A pack draws the approved Switch layout (its base) with its colors and its background on the
// home. Packs are read from the package (Assets/themes) and from LocalState/themes, where Temas >
// Importar temas copies them from a folder the player picks (a USB drive). A pack with a broken
// manifest or a missing background is left out; a saved pack that is gone falls back to the
// graffiti theme. Packs carry art only: no games, keys or firmware.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include <winrt/Windows.Storage.h>

#include "eden_uwp/theme.h"

namespace EdenXbox {

struct ThemePack {
    std::wstring id;   ///< Stable id (the manifest's; letters, digits and '-').
    std::wstring name; ///< As the manifest names it.
    std::wstring version;
    ThemeId base{ThemeId::SwitchDark};
    ThemeColors colors; ///< The base's, with the manifest's overrides.
    std::filesystem::path background;
    std::filesystem::path thumbnail; ///< The background when the pack has none.
    bool bundled{};                  ///< In the package (cannot be removed).
};

/// Every valid pack: the package's first, then LocalState/themes, by name. Ids are unique (a
/// LocalState pack with a bundled id is left out). Reads the disk: call it off the UI thread or
/// rarely.
std::vector<ThemePack> LoadThemePacks();

struct ThemeImportResult {
    unsigned imported{}, skipped{};
    std::string error; ///< Translated, empty when the folder could be read.
};
/// Copies the theme folders inside `source` (or `source` itself when it holds a theme.json)
/// into LocalState/themes, each validated first. MTA worker thread.
ThemeImportResult ImportThemePacks(const winrt::Windows::Storage::StorageFolder& source, std::stop_token stop);

} // namespace EdenXbox
