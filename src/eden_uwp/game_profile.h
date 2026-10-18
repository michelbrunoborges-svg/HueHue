// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE: a performance profile per game, picked in the game's options and applied when the
// game boots. The Xbox runs the console's own D3D12 driver (a UWP app cannot load another), so a
// profile chooses among what the emulator itself can trade: the CPU's accuracy, shaders built on
// workers and how the image is enlarged. Kept by launch path in LocalState/ns_profiles.tsv.

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>

#include <winrt/Windows.Storage.h>

#include "eden_uwp/localization.h"

namespace EdenXbox {

enum class GameProfile : std::uint8_t { Default, Performance, Compatibility, Quality };

inline std::span<const GameProfile> GameProfiles() {
    static constexpr std::array profiles{GameProfile::Default, GameProfile::Performance,
                                         GameProfile::Compatibility, GameProfile::Quality};
    return profiles;
}

inline I18n::Text GameProfileText(GameProfile profile) {
    switch (profile) {
    case GameProfile::Performance: return I18n::Text::NsProfilePerformance;
    case GameProfile::Compatibility: return I18n::Text::NsProfileCompatibility;
    case GameProfile::Quality: return I18n::Text::NsProfileQuality;
    case GameProfile::Default: break;
    }
    return I18n::Text::NsProfileDefault;
}

inline GameProfile NextGameProfile(GameProfile profile) {
    return static_cast<GameProfile>((static_cast<std::size_t>(profile) + 1) % GameProfiles().size());
}

namespace GameProfileDetail {
inline std::filesystem::path File() {
    try {
        return std::filesystem::path{
                   winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str()} /
               L"ns_profiles.tsv";
    } catch (...) { return {}; }
}
inline std::map<std::string, GameProfile> Load() {
    std::map<std::string, GameProfile> profiles;
    const auto file = File();
    if (file.empty()) return profiles;
    std::ifstream in(file, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        const auto tab = line.rfind('\t');
        if (tab == std::string::npos || tab + 1 >= line.size()) continue;
        const int value = line[tab + 1] - '0';
        if (value > 0 && value < static_cast<int>(GameProfiles().size()))
            profiles[line.substr(0, tab)] = static_cast<GameProfile>(value);
    }
    return profiles;
}
} // namespace GameProfileDetail

/// The profile kept for the game at `launch_path` (Default when none).
inline GameProfile LoadGameProfile(const std::string& launch_path) {
    const auto profiles = GameProfileDetail::Load();
    const auto found = profiles.find(launch_path);
    return found == profiles.end() ? GameProfile::Default : found->second;
}

/// Keeps `profile` for the game; false when it could not be written.
inline bool SaveGameProfile(const std::string& launch_path, GameProfile profile) {
    auto profiles = GameProfileDetail::Load();
    if (profile == GameProfile::Default) profiles.erase(launch_path);
    else profiles[launch_path] = profile;
    const auto file = GameProfileDetail::File();
    if (file.empty()) return false;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    for (const auto& [path, value] : profiles) out << path << '\t' << static_cast<int>(value) << '\n';
    return static_cast<bool>(out);
}

} // namespace EdenXbox
