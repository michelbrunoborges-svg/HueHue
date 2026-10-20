// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE: performance and image settings per game, picked in the game's options (Desempenho e
// graficos) and applied when the game boots (uwp_boot.cpp). The Xbox runs the console's own D3D12
// driver (a UWP app cannot load another), so these choose among what the emulator itself can
// trade:
//  - profile: CPU accuracy, shaders built on workers and how the image is enlarged;
//  - resolution: the console docked (the game's TV resolution, up to 1080p) or in handheld mode
//    (the game's portable resolution, usually 720p, lighter on the GPU);
//  - FPS limit and frame skip: frames the screen does not show are not composited or presented
//    (the game itself keeps its speed);
//  - shaders: how many threads build them;
//  - renderer: the native D3D12 one, or OpenGL through Mesa's d3d12 driver (gl_window.h), slower
//    but able to run games the D3D12 one cannot (the path NXbox / NX Zero takes).
// Kept by launch path in LocalState/ns_profiles.tsv.

#include <algorithm>
#include <iterator>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <span>
#include <string>

#include <winrt/Windows.Storage.h>

#include "eden_uwp/localization.h"
#include "eden_uwp/upscaling.h"

namespace EdenXbox {

enum class GameProfile : std::uint8_t { Default, Performance, Compatibility, Quality };
/// TV: docked (the game's TV resolution, up to 1080p). Portable: handheld (720p in most games).
/// Tv540 / Portable360: the same consoles at half the resolution through Eden's resolution scaling,
/// which only the OpenGL renderer has (on D3D12 they fall back to Portable). Eden scales in
/// quarters (1/4, 1/2, 3/4), so 540p and 360p are the steps under 720p; 480p is not one of them.
enum class GameResolution : std::uint8_t { Tv, Portable, Tv540, Portable360 };
inline constexpr std::size_t GameResolutionCount = 4;
enum class FpsLimit : std::uint8_t { Auto, Fps60, Fps30, Manual };
enum class ShaderBuild : std::uint8_t { Balanced, Fast };
enum class GameRenderer : std::uint8_t { D3D12, OpenGL };
/// How the OpenGL renderer hands shaders to Mesa: GLSL text (Eden's unchanged backend) or SPIR-V
/// (NX Zero's default: about 40% less memory per program).
enum class GlShaders : std::uint8_t { Glsl, Spirv };
/// Ampliacao da imagem for the game: Auto keeps the profile's (or the system's) choice.
enum class TuningUpscaling : std::uint8_t { Auto, None, Bilinear, FsrSharp, FsrBalanced, FsrSoft };
inline constexpr std::size_t TuningUpscalingCount = 6;
/// Economy trades some speed for headroom under the console's 5 GiB: caches give memory back
/// earlier, a smaller JIT code cache and no JIT warm-up at boot.
enum class MemoryMode : std::uint8_t { Normal, Economy };
/// Avancado (experimental): engine options that used to be reachable only through boot.cfg.
/// ASTC textures: decoded on the GPU (the default), on the CPU (no GPU decode work, more CPU
/// time) or kept as BC3 (less memory, a little less faithful).
enum class AstcMode : std::uint8_t { Gpu, Cpu, Bc3 };
/// Fastmem: guest memory through the host-mapped arena (faster CPU emulation when it fits; it
/// costs memory and can be slower when the game touches more than the hot window).
enum class FastmemMode : std::uint8_t { Off, Hybrid, Full };
/// How idle emulated cores wait: adaptive spin (default), block at once (less CPU and heat) or
/// always spin a little (lower latency, more CPU).
enum class IdleSpin : std::uint8_t { Adaptive, Off, Fixed };
/// The JIT code cache per core.
enum class JitCache : std::uint8_t { Normal, Small, Large };

struct GameTuning {
    /// Desempenho by default (the player asked, 19/10/2026): quality is the player's choice per game.
    GameProfile profile{GameProfile::Performance};
    GameResolution resolution{GameResolution::Tv};
    FpsLimit fps{FpsLimit::Auto};
    std::uint8_t fps_manual{45};  ///< 0 (no limit) to 60, any value (FpsLimit::Manual).
    std::uint8_t frame_skip{};    ///< 0 (automatic: none) to 3 frames skipped for each one shown.
    ShaderBuild shaders{ShaderBuild::Balanced};
    GameRenderer renderer{GameRenderer::D3D12};
    GlShaders gl_shaders{GlShaders::Glsl};
    TuningUpscaling upscaling{TuningUpscaling::Auto};
    MemoryMode memory{MemoryMode::Normal};
    // Avancado (experimental).
    bool record_thread{};          ///< D3D12 command lists recorded on a thread of their own.
    bool present_thread{true};     ///< Frames presented on a thread of their own.
    AstcMode astc{AstcMode::Gpu};
    bool astc_opaque_bc3{};        ///< Opaque ASTC as BC3 (faithful) instead of BC1 (half the memory).
    FastmemMode fastmem{FastmemMode::Off};
    bool pso_eviction{true};       ///< Idle pipelines give their PSOs back.
    IdleSpin idle_spin{IdleSpin::Adaptive};
    bool speed_limit{true};        ///< Off: the game runs as fast as the console can.
    JitCache jit_cache{JitCache::Normal};
    /// Shaders pendentes (issue #6 P1-A, experimental): skip the draws whose PSO is still being
    /// built instead of stalling (full-screen passes and compute still wait).
    bool pso_defer{};

    bool operator==(const GameTuning&) const = default;

    /// The frame rate the screen shows at most; 0 leaves it to the game.
    [[nodiscard]] std::uint32_t MaxFps() const {
        switch (fps) {
        case FpsLimit::Fps60: return 60;
        case FpsLimit::Fps30: return 30;
        case FpsLimit::Manual: return fps_manual;
        case FpsLimit::Auto: break;
        }
        return 0;
    }
};

/// The upscaling a tuning asks for; nullopt keeps the profile's or the system's.
inline std::optional<Upscaling> ToUpscaling(TuningUpscaling value) {
    switch (value) {
    case TuningUpscaling::None: return Upscaling::None;
    case TuningUpscaling::Bilinear: return Upscaling::Bilinear;
    case TuningUpscaling::FsrSharp: return Upscaling::FsrSharp;
    case TuningUpscaling::FsrBalanced: return Upscaling::FsrBalanced;
    case TuningUpscaling::FsrSoft: return Upscaling::FsrSoft;
    case TuningUpscaling::Auto: break;
    }
    return std::nullopt;
}

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

/// The rows of the Desempenho e graficos page, in order.
enum class TuningRow : std::uint8_t {
    Profile, Renderer, GlShaderPath, Resolution, Upscaling, Fps, FpsManual, FrameSkip, Shaders, Memory,
    // Avancado (experimental)
    SpeedLimit, PsoDefer, RecordThread, PresentThread, Astc, AstcOpaque, Fastmem, PsoEviction, IdleSpin, JitCache
};
inline constexpr std::size_t TuningRowCount = 20;
/// Rows the page shows at once (it scrolls past them).
inline constexpr std::size_t TuningRowsVisible = 10;

/// Left/Right (back = Left) on a row steps its value; big (LB/RB) moves the manual FPS by 10.
inline void StepTuning(GameTuning& tuning, TuningRow row, bool back, bool big = false) {
    const auto cycle = [back](auto value, std::size_t count) {
        const auto index = static_cast<std::size_t>(value);
        return static_cast<decltype(value)>((index + (back ? count - 1 : 1)) % count);
    };
    switch (row) {
    case TuningRow::Profile: tuning.profile = cycle(tuning.profile, GameProfiles().size()); break;
    case TuningRow::Renderer: tuning.renderer = cycle(tuning.renderer, 2); break;
    case TuningRow::GlShaderPath:
        tuning.gl_shaders = cycle(tuning.gl_shaders, 2);
        tuning.renderer = GameRenderer::OpenGL; // only means something there
        break;
    case TuningRow::Resolution: tuning.resolution = cycle(tuning.resolution, GameResolutionCount); break;
    case TuningRow::Upscaling: tuning.upscaling = cycle(tuning.upscaling, TuningUpscalingCount); break;
    case TuningRow::Memory: tuning.memory = cycle(tuning.memory, 2); break;
    case TuningRow::Fps: tuning.fps = cycle(tuning.fps, 4); break;
    case TuningRow::FpsManual: {
        const int step = big ? 10 : 1;
        const int next = tuning.fps_manual + (back ? -step : step);
        tuning.fps_manual = static_cast<std::uint8_t>(next > 60 ? 0 : next < 0 ? 60 : next);
        tuning.fps = FpsLimit::Manual;
        break;
    }
    case TuningRow::FrameSkip: tuning.frame_skip = static_cast<std::uint8_t>((tuning.frame_skip + (back ? 3 : 1)) % 4); break;
    case TuningRow::Shaders: tuning.shaders = cycle(tuning.shaders, 2); break;
    case TuningRow::SpeedLimit: tuning.speed_limit = !tuning.speed_limit; break;
    case TuningRow::PsoDefer: tuning.pso_defer = !tuning.pso_defer; break;
    case TuningRow::RecordThread: tuning.record_thread = !tuning.record_thread; break;
    case TuningRow::PresentThread: tuning.present_thread = !tuning.present_thread; break;
    case TuningRow::Astc: tuning.astc = cycle(tuning.astc, 3); break;
    case TuningRow::AstcOpaque: tuning.astc_opaque_bc3 = !tuning.astc_opaque_bc3; break;
    case TuningRow::Fastmem: tuning.fastmem = cycle(tuning.fastmem, 3); break;
    case TuningRow::PsoEviction: tuning.pso_eviction = !tuning.pso_eviction; break;
    case TuningRow::IdleSpin: tuning.idle_spin = cycle(tuning.idle_spin, 3); break;
    case TuningRow::JitCache: tuning.jit_cache = cycle(tuning.jit_cache, 3); break;
    }
}

namespace GameProfileDetail {
inline std::filesystem::path File() {
    try {
        return std::filesystem::path{
                   winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str()} /
               L"ns_profiles.tsv";
    } catch (...) { return {}; }
}
/// "path \t profile [\t resolution \t fps \t manual \t skip \t shaders [\t renderer [\t gl
/// [\t upscaling \t memory [\t speed record present astc opaque fastmem pso spin jit [\t defer]]]]]]"
/// (older lines stop earlier: the rest keeps its defaults).
inline std::map<std::string, GameTuning> Load() {
    std::map<std::string, GameTuning> all;
    const auto file = File();
    if (file.empty()) return all;
    std::ifstream in(file, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::istringstream fields(line.substr(tab + 1));
        int profile = 0, resolution = 0, fps = 0, manual = 45, skip = 0, shaders = 0, renderer = 0, gl = 0,
            upscaling = 0, memory = 0;
        fields >> profile;
        if (!(fields >> resolution >> fps >> manual >> skip >> shaders)) {
            resolution = fps = skip = shaders = 0; // an older line: the profile only
            manual = 45;
        } else if (!(fields >> renderer)) {
            renderer = 0; // before the renderer choice
        } else if (!(fields >> gl)) {
            gl = 0;
        } else if (!(fields >> upscaling >> memory)) {
            upscaling = memory = 0;
        }
        int advanced[9] = {1, 0, 1, 0, 0, 0, 1, 0, 0}; // the defaults, for lines before Avancado
        {
            int read[9];
            bool complete = true;
            for (int& value : read) complete = complete && static_cast<bool>(fields >> value);
            if (complete) std::copy(std::begin(read), std::end(read), advanced);
        }
        int defer = 0; // 0.4.2: Shaders pendentes
        if (!(fields >> defer)) defer = 0;
        GameTuning tuning;
        tuning.profile = static_cast<GameProfile>(std::clamp(profile, 0, 3));
        tuning.resolution = static_cast<GameResolution>(std::clamp(resolution, 0, static_cast<int>(GameResolutionCount) - 1));
        tuning.fps = static_cast<FpsLimit>(std::clamp(fps, 0, 3));
        tuning.fps_manual = static_cast<std::uint8_t>(std::clamp(manual, 0, 60));
        tuning.frame_skip = static_cast<std::uint8_t>(std::clamp(skip, 0, 3));
        tuning.shaders = static_cast<ShaderBuild>(std::clamp(shaders, 0, 1));
        tuning.renderer = static_cast<GameRenderer>(std::clamp(renderer, 0, 1));
        tuning.gl_shaders = static_cast<GlShaders>(std::clamp(gl, 0, 1));
        tuning.upscaling = static_cast<TuningUpscaling>(std::clamp(upscaling, 0, static_cast<int>(TuningUpscalingCount) - 1));
        tuning.memory = static_cast<MemoryMode>(std::clamp(memory, 0, 1));
        tuning.speed_limit = advanced[0] != 0;
        tuning.record_thread = advanced[1] != 0;
        tuning.present_thread = advanced[2] != 0;
        tuning.astc = static_cast<AstcMode>(std::clamp(advanced[3], 0, 2));
        tuning.astc_opaque_bc3 = advanced[4] != 0;
        tuning.fastmem = static_cast<FastmemMode>(std::clamp(advanced[5], 0, 2));
        tuning.pso_eviction = advanced[6] != 0;
        tuning.idle_spin = static_cast<IdleSpin>(std::clamp(advanced[7], 0, 2));
        tuning.jit_cache = static_cast<JitCache>(std::clamp(advanced[8], 0, 2));
        tuning.pso_defer = defer != 0;
        all[line.substr(0, tab)] = tuning;
    }
    return all;
}
} // namespace GameProfileDetail

/// What is kept for the game at `launch_path` (the defaults when nothing is).
inline GameTuning LoadGameTuning(const std::string& launch_path) {
    const auto all = GameProfileDetail::Load();
    const auto found = all.find(launch_path);
    return found == all.end() ? GameTuning{} : found->second;
}

/// Whether anything is kept for the game at `launch_path`.
inline bool HasSavedTuning(const std::string& launch_path) {
    return GameProfileDetail::Load().contains(launch_path);
}

inline GameProfile LoadGameProfile(const std::string& launch_path) {
    return LoadGameTuning(launch_path).profile;
}

/// Keeps `tuning` for the game; false when it could not be written.
inline bool SaveGameTuning(const std::string& launch_path, const GameTuning& tuning) {
    auto all = GameProfileDetail::Load();
    // Kept even when it holds the defaults: a game the compatibility list starts on OpenGL
    // (game_compat.h) and the player set back to D3D12 stays on D3D12.
    all[launch_path] = tuning;
    const auto file = GameProfileDetail::File();
    if (file.empty()) return false;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    for (const auto& [path, value] : all) {
        out << path << '\t' << static_cast<int>(value.profile) << '\t' << static_cast<int>(value.resolution)
            << '\t' << static_cast<int>(value.fps) << '\t' << static_cast<int>(value.fps_manual) << '\t'
            << static_cast<int>(value.frame_skip) << '\t' << static_cast<int>(value.shaders) << '\t'
            << static_cast<int>(value.renderer) << '\t' << static_cast<int>(value.gl_shaders) << '\t'
            << static_cast<int>(value.upscaling) << '\t' << static_cast<int>(value.memory) << '\t'
            << int{value.speed_limit} << '\t' << int{value.record_thread} << '\t' << int{value.present_thread}
            << '\t' << static_cast<int>(value.astc) << '\t' << int{value.astc_opaque_bc3} << '\t'
            << static_cast<int>(value.fastmem) << '\t' << int{value.pso_eviction} << '\t'
            << static_cast<int>(value.idle_spin) << '\t' << static_cast<int>(value.jit_cache) << '\t'
            << int{value.pso_defer} << '\n';
    }
    return static_cast<bool>(out);
}

} // namespace EdenXbox
