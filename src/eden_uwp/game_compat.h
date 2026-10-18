// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE: which renderer a game starts on.
//  - A compatibility list: games known to need the OpenGL renderer (gl_window.h) start there the
//    first time, until the player picks otherwise in Desempenho e graficos.
//  - A game that closes on D3D12 (the GPU failed, or the app died with it in its first minute) is
//    moved to OpenGL for its next launch, and the library says so. A session marker in
//    LocalState/ns_session.txt tells a crash from a normal end: the boot writes it before the
//    game starts, refreshes it while the game runs and removes it when the game ends.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include <winrt/Windows.Storage.h>

#include "eden_uwp/game_profile.h"

namespace EdenXbox {

/// The renderer the compatibility list starts a title on, by its base program id.
inline std::optional<GameRenderer> KnownRenderer(std::uint64_t program_id) {
    switch (program_id & ~std::uint64_t{0xFFF}) {
    case 0x0100000000010000: // Runs on NX Zero's OpenGL path; closes on D3D12 (18/10/2026).
        return GameRenderer::OpenGL;
    default:
        return std::nullopt;
    }
}

/// Keeps the list's renderer for a game that has no settings of its own yet. True when it did.
inline bool ApplyKnownRenderer(const std::string& launch_path, std::uint64_t program_id) {
    const auto known = KnownRenderer(program_id);
    if (!known || HasSavedTuning(launch_path)) return false;
    GameTuning tuning;
    tuning.renderer = *known;
    return SaveGameTuning(launch_path, tuning);
}

namespace CompatDetail {
inline std::filesystem::path SessionFile() {
    try {
        return std::filesystem::path{
                   winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str()} /
               L"ns_session.txt";
    } catch (...) { return {}; }
}
} // namespace CompatDetail

/// A game that was running when the app ended without the boot noticing.
struct LostSession {
    std::string launch_path;
    GameRenderer renderer{};
    std::uint32_t seconds{}; ///< How long it had run, as last refreshed.
};

/// The boot, before the game starts and then every few seconds while it runs.
inline void WriteSession(const std::string& launch_path, GameRenderer renderer, std::uint32_t seconds) {
    const auto file = CompatDetail::SessionFile();
    if (file.empty()) return;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << launch_path << '\t' << static_cast<int>(renderer) << '\t' << seconds << '\n';
}

/// The boot, when the game ended in any way it saw.
inline void EndSession() {
    const auto file = CompatDetail::SessionFile();
    if (file.empty()) return;
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

/// At startup: the session the last run left behind (and forgets it).
inline std::optional<LostSession> TakeLostSession() {
    const auto file = CompatDetail::SessionFile();
    if (file.empty()) return std::nullopt;
    std::optional<LostSession> lost;
    {
        std::ifstream in(file, std::ios::binary);
        std::string line;
        if (in && std::getline(in, line)) {
            std::istringstream fields(line);
            LostSession session;
            std::string renderer, seconds;
            if (std::getline(fields, session.launch_path, '\t') && std::getline(fields, renderer, '\t') &&
                std::getline(fields, seconds)) {
                try {
                    session.renderer = std::stoi(renderer) == 1 ? GameRenderer::OpenGL : GameRenderer::D3D12;
                    session.seconds = static_cast<std::uint32_t>(std::stoul(seconds));
                    lost = session;
                } catch (...) {}
            }
        }
    }
    EndSession();
    return lost;
}

/// Moves a game that failed on D3D12 to OpenGL for its next launch. True when it did.
inline bool FallBackToOpenGl(const std::string& launch_path) {
    auto tuning = LoadGameTuning(launch_path);
    if (tuning.renderer == GameRenderer::OpenGL) return false;
    tuning.renderer = GameRenderer::OpenGL;
    return SaveGameTuning(launch_path, tuning);
}

/// A run shorter than this that ended with the app counts as the game failing, not the player
/// closing it.
inline constexpr std::uint32_t CRASH_WINDOW_SECONDS = 60;

} // namespace EdenXbox
