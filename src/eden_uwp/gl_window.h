// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NS HUEHUE: the OpenGL renderer for the games that the native D3D12 one does not run (chosen per
// game in Desempenho e graficos). Eden's OpenGL renderer draws through Mesa's d3d12 driver
// (opengl32.dll + libgallium_wgl.dll in the package), whose WGL takes the CoreWindow as its device
// context and owns the window's swap chain while the game runs. Brought from NXbox (NX Zero),
// where this path runs games the D3D12 one cannot.
//
// The frontend's in-game menu and game keyboard (D3D12::MenuImage) are drawn here over the frame,
// since the D3D12 renderer that normally shows them is not running.

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include <winrt/Windows.UI.Core.h>

#include "common/common_types.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "video_core/renderer_d3d12/d3d12_menu_image.h"

namespace EdenXbox {

struct MesaRuntime;

class GlWindow final : public Core::Frontend::EmuWindow {
public:
    /// core_window: the CoreWindow's IUnknown*. Throws when Mesa cannot be loaded or refuses an
    /// OpenGL 4.6 context.
    GlWindow(void* core_window, u32 width, u32 height);
    ~GlWindow() override;

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override;
    bool IsShown() const override {
        return true;
    }

    [[nodiscard]] std::pair<u32, u32> ScreenSize() const {
        return {width, height};
    }

    /// Shows (or with nullopt removes) the menu image over the game. Any thread.
    void SetOverlay(std::optional<D3D12::MenuImage> image);
    /// Draws the overlay on black (the guest is paused and presents nothing) and presents. GPU
    /// thread only, where the renderer's context is current.
    void PresentOverlayNow();

    /// The overlay, drawn over the frame about to be presented (on black first with on_black).
    /// GPU thread (SwapBuffers).
    void DrawOverlay(bool on_black);

private:
    winrt::Windows::UI::Core::CoreWindow window{nullptr};
    std::shared_ptr<MesaRuntime> runtime;
    u32 width{};
    u32 height{};
    mutable std::atomic<int> shared_contexts_created{0};

    std::mutex overlay_mutex;
    std::optional<D3D12::MenuImage> overlay;
    bool overlay_dirty{};
    // GL objects of the overlay, on the renderer's context.
    unsigned overlay_texture{};
    unsigned overlay_program{};
    unsigned overlay_vao{};
    u32 overlay_width{};
    u32 overlay_height{};
};

} // namespace EdenXbox
