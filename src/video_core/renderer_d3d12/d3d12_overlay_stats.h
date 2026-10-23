// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE 0.4.5: what the performance overlay shows that only the frontend knows: the app's
// memory and its limit, the JIT code, the emulated DRAM and the frames the game itself drew.
// No Mesa or D3D12 headers, so the UWP frontend includes it too.

#include "common/common_types.h"

namespace D3D12 {

struct OverlayStats {
    u64 app_used{}, app_limit{}, jit_bytes{}, emu_bytes{};
    u64 game_frames{};
    bool memory_valid{}, frames_valid{};
};
using OverlayStatsQuery = OverlayStats (*)();
/// Any thread; the overlay calls it at most once every half second while it is shown.
void SetOverlayStatsQuery(OverlayStatsQuery query);

} // namespace D3D12
