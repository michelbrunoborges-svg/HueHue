// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The diagnostic line the OpenGL (Mesa) renderer brought from NXbox writes (video_core, NXBOX_UWP):
// LocalState\eden_gl_diag.txt, apart from eden_uwp_diag.txt, which the boot owns. Past 8 MiB the
// file moves to its .old copy when the process first writes to it.

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <windows.h>

#include <winrt/Windows.Storage.h>

#include "common/log_rotation.h"

namespace EdenXbox {
inline void Diagnostic(const std::string& message) noexcept {
    try {
        OutputDebugStringA(("[eden-gl] " + message + "\n").c_str());
        static std::mutex mutex;
        const std::lock_guard lock(mutex);
        static const std::filesystem::path file = [] {
            const std::filesystem::path path =
                std::filesystem::path{winrt::Windows::Storage::ApplicationData::Current()
                                          .LocalFolder()
                                          .Path()
                                          .c_str()} /
                L"eden_gl_diag.txt";
            Common::RotateLogIfLarger(path, 8ULL << 20);
            return path;
        }();
        std::ofstream out(file, std::ios::app | std::ios::binary);
        out << message << '\n';
    } catch (...) {
    }
}
} // namespace EdenXbox
