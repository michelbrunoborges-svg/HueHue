// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE online play: Eden's multiplayer rooms carry the Switch's local wireless play (LDN)
// over the internet, so two consoles running a game that has local wireless play meet in a room
// of an Eden room server (src/dedicated_room). The library keeps where to meet; the boot joins the
// room before the game starts (uwp_boot.cpp). Kept in LocalState/ns_online.cfg.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <winrt/Windows.Storage.h>

namespace EdenXbox {

struct OnlineSettings {
    bool enabled{};
    std::string server;          ///< Host name or IPv4 of the room server.
    std::uint16_t port{24872};   ///< Network::DefaultRoomPort.
    std::string password;        ///< The room's password; empty for an open room.
};

namespace OnlineDetail {
inline std::filesystem::path File() {
    try {
        return std::filesystem::path{
                   winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str()} /
               L"ns_online.cfg";
    } catch (...) { return {}; }
}
} // namespace OnlineDetail

inline OnlineSettings LoadOnlineSettings() {
    OnlineSettings settings;
    const auto file = OnlineDetail::File();
    if (file.empty()) return settings;
    std::ifstream in(file, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const auto key = line.substr(0, equal), value = line.substr(equal + 1);
        if (key == "enabled") settings.enabled = value == "1";
        else if (key == "server") settings.server = value;
        else if (key == "password") settings.password = value;
        else if (key == "port") {
            try {
                const auto port = std::stoul(value);
                if (port > 0 && port <= 65535) settings.port = static_cast<std::uint16_t>(port);
            } catch (...) {}
        }
    }
    return settings;
}

inline bool SaveOnlineSettings(const OnlineSettings& settings) {
    const auto file = OnlineDetail::File();
    if (file.empty()) return false;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "enabled=" << (settings.enabled ? 1 : 0) << '\n'
        << "server=" << settings.server << '\n'
        << "port=" << settings.port << '\n'
        << "password=" << settings.password << '\n';
    return static_cast<bool>(out);
}

} // namespace EdenXbox
