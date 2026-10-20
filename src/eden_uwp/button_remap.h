// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE: free button mapping (Configuracoes > Controles > Mapear botoes, a game's options and
// the in-game menu). For each Switch button, the Xbox control that presses it; several Switch
// buttons may share a control and a button may have none. The default is the console's own
// layout by label (Xbox A is Switch A); Modo Switch (face buttons by position) still applies
// first, as before. Pure: no WinRT, shared by the input thread, the library and the menu.

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace EdenXbox {

/// Switch buttons (the targets), in the order the pages list them.
enum class RemapButton : std::uint8_t {
    A, B, X, Y, L, R, ZL, ZR, LStick, RStick, Plus, Minus, Up, Down, Left, Right, Count
};
inline constexpr std::size_t RemapCount = static_cast<std::size_t>(RemapButton::Count);
/// Xbox controls (the sources), in the same order: A, B, X, Y, LB, RB, LT, RT, LS, RS, Menu,
/// View, d-pad up, down, left, right. RemapNone: the Switch button is never pressed.
inline constexpr std::uint8_t RemapNone = 0xFF;
using ButtonRemap = std::array<std::uint8_t, RemapCount>;

inline constexpr ButtonRemap IdentityRemap() {
    ButtonRemap remap{};
    for (std::size_t i = 0; i < RemapCount; ++i) remap[i] = static_cast<std::uint8_t>(i);
    return remap;
}
inline bool IsIdentity(const ButtonRemap& remap) { return remap == IdentityRemap(); }

inline std::wstring_view SwitchButtonName(std::size_t target) {
    static constexpr std::array<std::wstring_view, RemapCount> names{
        L"A", L"B", L"X", L"Y", L"L", L"R", L"ZL", L"ZR", L"L (analógico)", L"R (analógico)",
        L"+", L"−", L"↑", L"↓", L"←", L"→"};
    return target < RemapCount ? names[target] : L"?";
}
inline std::wstring_view XboxControlName(std::uint8_t source) {
    static constexpr std::array<std::wstring_view, RemapCount> names{
        L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT", L"RT", L"LS", L"RS", L"Menu", L"View",
        L"D-pad ↑", L"D-pad ↓", L"D-pad ←", L"D-pad →"};
    return source < RemapCount ? names[source] : L"—";
}
/// The next (or previous) control for a button: every control, then none.
inline std::uint8_t StepRemapSource(std::uint8_t source, bool back) {
    const std::size_t count = RemapCount + 1; // the controls, then RemapNone
    std::size_t slot = source < RemapCount ? source : RemapCount;
    slot = (slot + (back ? count - 1 : 1)) % count;
    return slot == RemapCount ? RemapNone : static_cast<std::uint8_t>(slot);
}

/// "0,1,2,..." (255 for none).
inline std::string SerializeRemap(const ButtonRemap& remap) {
    std::string text;
    for (std::size_t i = 0; i < RemapCount; ++i) {
        if (i) text += ',';
        text += std::to_string(remap[i]);
    }
    return text;
}
inline std::optional<ButtonRemap> ParseRemap(std::string_view text) {
    ButtonRemap remap{};
    std::size_t i = 0;
    while (!text.empty() && i < RemapCount) {
        const auto comma = text.find(',');
        const auto item = text.substr(0, comma);
        unsigned value = 0;
        const auto [end, error] = std::from_chars(item.data(), item.data() + item.size(), value);
        if (error != std::errc{} || (value >= RemapCount && value != RemapNone)) return std::nullopt;
        remap[i++] = static_cast<std::uint8_t>(value);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    if (i != RemapCount) return std::nullopt;
    return remap;
}

} // namespace EdenXbox
