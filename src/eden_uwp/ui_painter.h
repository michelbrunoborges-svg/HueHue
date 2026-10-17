// SPDX-FileCopyrightText: Copyright 2026 JulianDr14
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The frontend's drawing vocabulary on a Direct2D render target: rounded panels, text in the
// interface language, the vector icons, rows, page headers, switches and button glyphs. The
// library canvas draws on its swapchain with it, and the in-game menu on a CPU bitmap, so both
// look the same from one implementation.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "eden_uwp/ui_icon.h"

namespace EdenXbox {

/// The frontend's colors (0xRRGGBB): the NS HUEHUE design pack v1 (design/ns-huehue-v1, approved
/// by Michel on 17/10/2026). Dark teal-black ground, neon green focus, pink brand.
namespace Palette {
inline constexpr unsigned Backdrop = 0x071012;
inline constexpr unsigned Panel = 0x101b1e;
inline constexpr unsigned Row = 0x162124;
inline constexpr unsigned RowActive = 0x1a321e;
inline constexpr unsigned Accent = 0x8aff00;
inline constexpr unsigned AccentDeep = 0x173b12;
inline constexpr unsigned Magenta = 0xff1493;
inline constexpr unsigned OnAccent = 0x071012;
inline constexpr unsigned Text = 0xf4f7f5;
inline constexpr unsigned TextSecondary = 0xa6b5b7;
inline constexpr unsigned TextHint = 0xbccacb;
inline constexpr unsigned Warning = 0xffcf32;
inline constexpr unsigned Track = 0x253033;
} // namespace Palette

/// Tokens of the NS HUEHUE design pack that the palette above does not name (styles.css).
namespace Ns {
inline constexpr unsigned Line = 0x354347;      // borders
inline constexpr unsigned Divider = 0x304137;   // header and footer rules
inline constexpr unsigned Secondary = 0x172225; // secondary buttons and chips
inline constexpr unsigned MenuButton = 0x081113;
inline constexpr unsigned NavSelected = 0x163d17;
inline constexpr unsigned Muted = 0x73858a;
inline constexpr unsigned Error = 0xff6b74;
inline constexpr unsigned KeyX = 0x36c6ff;
inline constexpr unsigned KeyY = 0xffcf32;
inline constexpr unsigned Toast = 0x1d3822;
} // namespace Ns

/// As cores do logo HUEHUE (verde, azul e amarelo da bandeira).
namespace HueColor {
inline constexpr unsigned Green = 0x22c55e;
inline constexpr unsigned Blue = 0x3b82f6;
inline constexpr unsigned Yellow = 0xfacc15;
} // namespace HueColor

class UiPainter {
public:
    virtual ~UiPainter() = default;

    /// Draws on `target` from now on; texts are laid out by `write`.
    void Attach(ID2D1RenderTarget* target, IDWriteFactory* write);
    /// Text formats follow the interface language; call once per frame.
    void SyncLanguage();

    void Round(float x, float y, float w, float h, float radius, unsigned color);
    void Text(std::wstring_view text, float x, float y, float size, unsigned color, float width,
              float height = 40, bool bold = false, bool wrap = false, bool center = false,
              bool trailing = false);
    float MeasureText(std::wstring_view text, float size, bool bold);
    /// Native vector strokes: no texture loads, font glyphs or geometry allocation.
    void Icon(UiIcon icon, float x, float y, unsigned color);
    /// The ">" at the end of a row that opens a page.
    void Chevron(float cx, float cy, unsigned color, bool left = false);
    /// A selectable row: background, accent bar when active and the icon in its tile.
    /// Passive list position: track/thumb and available directions, hidden when everything fits.
    void ScrollIndicator(float x, float y, float height, std::size_t first,
                         std::size_t visible, std::size_t count, bool more_below = false);
    void RowFrame(float x, float y, float width, float height, bool active, UiIcon icon);
    void PageHeader(float x, float y, UiIcon icon, std::wstring_view title, std::wstring_view subtitle);
    /// An on/off switch, 52x28 at (x, y).
    void Switch(float x, float y, bool on, bool active);
    /// A controller button drawn natively: a ring with its letters (A, B, X, Y, LB, RB...).
    void ButtonGlyph(std::wstring_view letters, float x, float y, unsigned color);
    /// A button glyph and its label; returns the width it took.
    float Prompt(std::wstring_view letters, unsigned color, std::wstring_view label, float x, float y);
    /// The picture in `file`, cropped to fill the rectangle, with rounded corners. False when this
    /// surface draws no pictures or the file cannot be decoded: the caller draws a placeholder.
    virtual bool Picture(const std::filesystem::path& file, float x, float y, float w, float h) {
        (void)file, (void)x, (void)y, (void)w, (void)h;
        return false;
    }

protected:
    IDWriteTextFormat* Format(float size, bool bold, bool wrap, bool center, bool trailing);

    Microsoft::WRL::ComPtr<ID2D1RenderTarget> surface;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    Microsoft::WRL::ComPtr<IDWriteFactory> write;

private:
    std::wstring format_locale{L"en"};
    std::unordered_map<unsigned, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats;
};

/// Colors of the Xbox face buttons, for ButtonGlyph.
namespace ButtonColor {
inline constexpr unsigned A = 0x7ccd4f;
inline constexpr unsigned B = 0xf16b5f;
inline constexpr unsigned X = 0x68b7ff;
inline constexpr unsigned Y = 0xf5d76e;
inline constexpr unsigned Neutral = 0xf4f4f6;
} // namespace ButtonColor

} // namespace EdenXbox
