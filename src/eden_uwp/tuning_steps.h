// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE: value steps shared by Desempenho e graficos (game_profile.h) and the in-game menu
// (game_menu.h). Pure: no WinRT.

#include <array>
#include <cstddef>
#include <cstdint>

namespace EdenXbox {

/// Passagens que esperam: with Shaders pendentes on, draws of at most this many vertices (the
/// full-screen passes: fog, lighting, post effects) still wait for their pipeline; 0: none waits.
inline constexpr std::array<std::uint8_t, 6> CriticalVertexSteps{0, 3, 6, 12, 24, 48};

inline std::uint8_t StepCriticalVertices(std::uint8_t current, bool back) {
    std::size_t at = 2;
    for (std::size_t i = 0; i < CriticalVertexSteps.size(); ++i)
        if (CriticalVertexSteps[i] == current) at = i;
    at = (at + (back ? CriticalVertexSteps.size() - 1 : 1)) % CriticalVertexSteps.size();
    return CriticalVertexSteps[at];
}

} // namespace EdenXbox
