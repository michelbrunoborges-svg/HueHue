// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE themes (design/ns-huehue-switch-v1, approved by Michel on 19/10/2026): HueHue Grafite
// (the design pack v1, the default and what existing installs keep), Switch Escuro and Switch
// Claro. A theme is a palette (the Palette/Ns colors of ui_painter.h), assets (Assets/sw) and a
// composition (the Switch home is a row of game tiles over round shortcuts; the header and footer
// follow it). The choice is kept in LocalSettings under stable ids, apart from saves and from the
// emulation settings; an unknown id falls back to the graffiti theme.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "eden_uwp/localization.h"
#include "eden_uwp/ui_painter.h"

namespace EdenXbox {

/// In the order Temas shows them (the persisted value is the id string, ThemeKey).
enum class ThemeId : std::uint8_t { Graffiti, SwitchLight, SwitchDark };
inline constexpr std::size_t ThemeCount = 3;
/// The Switch home's ground: the theme's solid color or the Aurora panorama (assets/sw).
enum class WallpaperId : std::uint8_t { Solid, Aurora };

inline constexpr std::wstring_view ThemeKey(ThemeId id) {
    switch (id) {
    case ThemeId::SwitchDark: return L"switch-dark";
    case ThemeId::SwitchLight: return L"switch-light";
    case ThemeId::Graffiti: break;
    }
    return L"huehue-graffiti";
}
inline std::optional<ThemeId> ParseTheme(std::wstring_view key) {
    for (const auto id : {ThemeId::Graffiti, ThemeId::SwitchLight, ThemeId::SwitchDark})
        if (key == ThemeKey(id)) return id;
    return std::nullopt;
}
inline constexpr std::wstring_view WallpaperKey(WallpaperId id) {
    return id == WallpaperId::Aurora ? L"aurora" : L"solid";
}
inline std::optional<WallpaperId> ParseWallpaper(std::wstring_view key) {
    if (key == L"aurora") return WallpaperId::Aurora;
    if (key == L"solid") return WallpaperId::Solid;
    return std::nullopt;
}
inline I18n::Text ThemeName(ThemeId id) {
    switch (id) {
    case ThemeId::SwitchDark: return I18n::Text::NsThemeSwitchDark;
    case ThemeId::SwitchLight: return I18n::Text::NsThemeSwitchLight;
    case ThemeId::Graffiti: break;
    }
    return I18n::Text::NsThemeGraffiti;
}
inline constexpr bool IsSwitchTheme(ThemeId id) { return id != ThemeId::Graffiti; }

/// Puts the theme's colors in Palette and Ns (ui_painter.h). The library canvas calls it before a
/// frame and with Graffiti after it, so the other surfaces (the in-game menu, the game keyboard)
/// keep the pack's colors.
struct ThemeColors {
    unsigned backdrop, panel, row, row_active, accent, focus, accent_deep, magenta, on_accent, text,
        text_secondary, text_hint, warning, track, line, divider, secondary, menu_button, nav_selected,
        muted, error, toast;
};
/// A built-in theme's colors.
inline const ThemeColors& BaseThemeColors(ThemeId id) {
    static constexpr ThemeColors graffiti{0x071012, 0x101b1e, 0x162124, 0x1a321e, 0x8aff00, 0x8aff00, 0x173b12,
                                          0xff1493, 0x071012, 0xf4f7f5, 0xa6b5b7, 0xbccacb, 0xffcf32, 0x253033,
                                          0x354347, 0x304137, 0x172225, 0x081113, 0x163d17, 0x73858a, 0xff6b74,
                                          0x1d3822};
    // theme-tokens.json: background, foreground, surface, muted, border, focus, selection.
    static constexpr ThemeColors dark{0x2d2d2d, 0x383838, 0x383838, 0x17484d, 0x27d8e8, 0x27d8e8, 0x17484d,
                                      0x27d8e8, 0x0b2a2e, 0xfafafa, 0xc2c2c2, 0xc2c2c2, 0xffcf32, 0x4a4a4a,
                                      0x626262, 0x4a4a4a, 0x383838, 0x383838, 0x17484d, 0x9a9a9a, 0xff6b74,
                                      0x17484d};
    // Light: the focus stays cyan; accent text is #006773 so it reads on the light ground.
    static constexpr ThemeColors light{0xf2f2ef, 0xffffff, 0xffffff, 0xd9f7fa, 0x006773, 0x27d8e8, 0xd9f7fa,
                                       0x006773, 0xffffff, 0x303334, 0x606667, 0x606667, 0x8a5a00, 0xd8dcdc,
                                       0xc8cdce, 0xd5d9da, 0xffffff, 0xffffff, 0xd9f7fa, 0x7a8081, 0xc2323d,
                                       0xd9f7fa};
    return id == ThemeId::SwitchDark ? dark : id == ThemeId::SwitchLight ? light : graffiti;
}
inline void ApplyThemeColors(const ThemeColors& c) {
    Palette::Backdrop = c.backdrop;
    Palette::Panel = c.panel;
    Palette::Row = c.row;
    Palette::RowActive = c.row_active;
    Palette::Accent = c.accent;
    Palette::Focus = c.focus;
    Palette::AccentDeep = c.accent_deep;
    Palette::Magenta = c.magenta;
    Palette::OnAccent = c.on_accent;
    Palette::Text = c.text;
    Palette::TextSecondary = c.text_secondary;
    Palette::TextHint = c.text_hint;
    Palette::Warning = c.warning;
    Palette::Track = c.track;
    Ns::Line = c.line;
    Ns::Divider = c.divider;
    Ns::Secondary = c.secondary;
    Ns::MenuButton = c.menu_button;
    Ns::NavSelected = c.nav_selected;
    Ns::Muted = c.muted;
    Ns::Error = c.error;
    Ns::Toast = c.toast;
}
inline void ApplyThemePalette(ThemeId id) { ApplyThemeColors(BaseThemeColors(id)); }

} // namespace EdenXbox
