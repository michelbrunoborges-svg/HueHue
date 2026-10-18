// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mesa's WGL on the Xbox, as NXbox (NX Zero) drives it (src/eden_uwp/mesa_window.cpp there): the
// CoreWindow's ABI pointer is the device context, one OpenGL 4.6 core context presents to it, and
// later shared contexts (shader workers) stay surfaceless, since a second swap chain on the one
// CoreWindow hangs.

#include "eden_uwp/gl_window.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>

#include <windows.h>

#include <glad/glad.h>
#include <winrt/Windows.Foundation.h>

#include "common/logging.h"

using namespace winrt;
using namespace winrt::Windows::UI::Core;

namespace EdenXbox {

namespace {

HMODULE g_loader_library = nullptr;

void* ResolveGL(const char* name) {
    auto address = GetProcAddress(g_loader_library, name);
    if (!address) {
        const auto resolve = reinterpret_cast<PROC(WINAPI*)(LPCSTR)>(
            GetProcAddress(g_loader_library, "wglGetProcAddress"));
        if (resolve) address = resolve(name);
    }
    return reinterpret_cast<void*>(address);
}

struct PixelFormat {
    WORD size = sizeof(PixelFormat);
    WORD version = 1;
    DWORD flags = 0x00000004 | 0x00000020 | 0x00000001; // draw to window, OpenGL, double buffer
    BYTE type = 0;
    BYTE color_bits = 32;
    BYTE red_bits = 0, red_shift = 0, green_bits = 0, green_shift = 0;
    BYTE blue_bits = 0, blue_shift = 0, alpha_bits = 8, alpha_shift = 0;
    BYTE accum_bits = 0, accum_red_bits = 0, accum_green_bits = 0;
    BYTE accum_blue_bits = 0, accum_alpha_bits = 0;
    BYTE depth_bits = 24, stencil_bits = 8, auxiliary_buffers = 0;
    BYTE layer_type = 0, reserved = 0;
    DWORD layer_mask = 0, visible_mask = 0, damage_mask = 0;
};
static_assert(sizeof(PixelFormat) == 40);

// WGL_CONTEXT_MAJOR_VERSION 4, MINOR_VERSION 6, PROFILE_MASK core.
constexpr int CONTEXT_ATTRIBUTES[] = {0x2091, 4, 0x2092, 6, 0x9126, 1, 0};

} // namespace

struct MesaRuntime {
    HMODULE library = nullptr;
    using ResolveProc = PROC(WINAPI*)(LPCSTR);
    ResolveProc resolve = nullptr;
    HDC dc = nullptr;
    HANDLE context = nullptr;
    using MakeCurrentProc = BOOL(WINAPI*)(HDC, HANDLE);
    using DeleteContextProc = BOOL(WINAPI*)(HANDLE);
    using SwapProc = BOOL(WINAPI*)(HDC);
    using CreateAttribsProc = HANDLE(WINAPI*)(HDC, HANDLE, const int*);
    MakeCurrentProc make_current = nullptr;
    DeleteContextProc delete_context = nullptr;
    SwapProc swap = nullptr;
    CreateAttribsProc create_attribs = nullptr;

    template <typename T>
    T Function(const char* name) {
        PROC address = GetProcAddress(library, name);
        if (!address && resolve) address = resolve(name);
        if (!address || address == reinterpret_cast<PROC>(1) || address == reinterpret_cast<PROC>(2) ||
            address == reinterpret_cast<PROC>(3) || address == reinterpret_cast<PROC>(-1)) {
            throw std::runtime_error(std::string("Mesa: missing GL entry point ") + name);
        }
        return reinterpret_cast<T>(address);
    }

    ~MesaRuntime() {
        if (make_current) make_current(nullptr, nullptr);
        if (context && delete_context) delete_context(context);
        if (library) FreeLibrary(library);
    }

    void Initialize(void* core_window) {
        _putenv_s("GALLIUM_DRIVER", "d3d12");
        // NXbox's Mesa patches journal every D3D12 call unless told not to (about 30% of the
        // render thread).
        for (const char* name : {"NXBOX_API_RING", "NXBOX_JOURNAL"}) {
            if (std::getenv(name) == nullptr) _putenv_s(name, "0");
        }
        // The DXIL validator Mesa signs its shaders with; the package already carries it for the
        // D3D12 renderer. Kept loaded for the process lifetime.
        if (!LoadPackagedLibrary(L"dxil.dll", 0)) {
            throw std::runtime_error("Mesa: cannot load dxil.dll (" + std::to_string(GetLastError()) + ")");
        }
        library = LoadPackagedLibrary(L"opengl32.dll", 0);
        if (!library) {
            throw std::runtime_error("Mesa: cannot load opengl32.dll (" + std::to_string(GetLastError()) + ")");
        }
        resolve = Function<ResolveProc>("wglGetProcAddress");
        const auto choose = Function<int(WINAPI*)(HDC, const PixelFormat*)>("wglChoosePixelFormat");
        const auto set = Function<BOOL(WINAPI*)(HDC, int, const PixelFormat*)>("wglSetPixelFormat");
        const auto create = Function<HANDLE(WINAPI*)(HDC)>("wglCreateContext");
        make_current = Function<MakeCurrentProc>("wglMakeCurrent");
        delete_context = Function<DeleteContextProc>("wglDeleteContext");
        swap = Function<SwapProc>("wglSwapBuffers");
        // Mesa's UWP GDI shim takes the CoreWindow ABI pointer as its device context.
        dc = reinterpret_cast<HDC>(core_window);
        const PixelFormat descriptor{};
        const int format = choose(dc, &descriptor);
        if (!format || !set(dc, format, &descriptor)) {
            throw std::runtime_error("Mesa: no pixel format for the window");
        }
        context = create(dc);
        if (!context || !make_current(dc, context)) {
            throw std::runtime_error("Mesa: cannot make the bootstrap context current");
        }
        create_attribs = Function<CreateAttribsProc>("wglCreateContextAttribsARB");
        HANDLE modern = create_attribs(dc, nullptr, CONTEXT_ATTRIBUTES);
        if (!modern) throw std::runtime_error("Mesa: OpenGL 4.6 core context refused");
        make_current(nullptr, nullptr);
        delete_context(context);
        context = modern;
        if (!make_current(dc, context)) {
            throw std::runtime_error("Mesa: cannot make the OpenGL 4.6 context current");
        }
        const auto get_string = Function<const unsigned char*(WINAPI*)(unsigned)>("glGetString");
        for (const auto& [name, id] : std::array<std::pair<const char*, unsigned>, 3>{
                 {{"vendor", 0x1F00}, {"renderer", 0x1F01}, {"version", 0x1F02}}}) {
            const auto value = get_string(id);
            LOG_INFO(Frontend, "OpenGL: {}={}", name, value ? reinterpret_cast<const char*>(value) : "null");
        }
    }
};

namespace {

/// The Xbox compositor reads the swap chain as premultiplied alpha: a frame left with alpha 0 shows
/// as black. Every presented pixel is made opaque; the GL state it touches is put back.
void ForceOpaque() {
    GLint draw = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    std::array<GLboolean, 4> mask{};
    glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
    std::array<GLfloat, 4> clear{};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear.data());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
}

class MesaContext final : public Core::Frontend::GraphicsContext {
public:
    MesaContext(std::shared_ptr<MesaRuntime> runtime_, HANDLE context_, bool surfaceless_,
                GlWindow* presenter_)
        : runtime(std::move(runtime_)), context(context_), surfaceless(surfaceless_),
          presenter(presenter_) {}
    ~MesaContext() override {
        runtime->delete_context(context);
    }

    void MakeCurrent() override {
        // Shader workers never draw to the window: binding them to its DC would make Mesa build a
        // second swap chain for the one CoreWindow, which hangs. Mesa's WGL takes a context
        // current without a drawable.
        if (!runtime->make_current(surfaceless ? nullptr : runtime->dc, context)) {
            throw std::runtime_error("Mesa: context activation failed");
        }
        // Eden draws with vertex buffers and no vertex array object, which only a compatibility
        // context allows; in this core context such draws fail and every frame stays black.
        if (vertex_array == 0) glGenVertexArrays(1, &vertex_array);
        glBindVertexArray(vertex_array);
    }
    void DoneCurrent() override {
        runtime->make_current(nullptr, nullptr);
    }
    void SwapBuffers() override {
        if (presenter) {
            // FPS limit and frame skip (Desempenho e graficos): a frame not shown is not
            // presented; the guest keeps its own speed.
            if (!presenter->ShowThisFrame()) return;
            presenter->CaptureFrame();
            presenter->DrawOverlay(false);
        }
        ForceOpaque();
        if (!runtime->swap(runtime->dc)) {
            throw std::runtime_error("Mesa: presentation failed");
        }
    }

private:
    std::shared_ptr<MesaRuntime> runtime;
    HANDLE context;
    bool surfaceless;
    GlWindow* presenter;
    GLuint vertex_array = 0;
};

} // namespace

GlWindow::GlWindow(void* core_window, u32 width_, u32 height_) : width(width_), height(height_) {
    winrt::copy_from_abi(window, core_window);
    window_info.type = Core::Frontend::WindowSystemType::CoreWindow;
    window_info.render_surface = core_window;
    window_info.render_surface_scale = 1.0f;
    UpdateCurrentFramebufferLayout(width, height);
    runtime = std::make_shared<MesaRuntime>();
    runtime->Initialize(core_window);
    g_loader_library = runtime->library;
    if (!gladLoadGLLoader(ResolveGL)) {
        throw std::runtime_error("Mesa: cannot load the OpenGL entry points");
    }
    runtime->make_current(nullptr, nullptr);
    // Shared worker contexts hang on Mesa d3d12, and its WGL cannot make a second context current
    // on the window: shaders are built and precompiled on the renderer's own context.
    strict_context_required = true;
    precompile_on_current_context = true;
    LOG_INFO(Frontend, "OpenGL: Mesa d3d12 ready");
}

GlWindow::~GlWindow() = default;

std::unique_ptr<Core::Frontend::GraphicsContext> GlWindow::CreateSharedContext() const {
    const auto create = [this] {
        const auto context = runtime->create_attribs(runtime->dc, runtime->context, CONTEXT_ATTRIBUTES);
        if (!context) throw std::runtime_error("Mesa: shared context refused");
        return context;
    };
    HANDLE context = nullptr;
    const auto dispatcher = window.Dispatcher();
    // WGL contexts on the CoreWindow are created on its UI thread (the app's run loop pumps the
    // dispatcher while a game runs).
    if (dispatcher.HasThreadAccess()) {
        context = create();
    } else {
        std::promise<HANDLE> promise;
        auto result = promise.get_future();
        dispatcher.RunAsync(CoreDispatcherPriority::Normal, [&] {
            try {
                promise.set_value(create());
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        });
        context = result.get();
    }
    // The first shared context is the renderer's and presents to the window; later ones are
    // shader workers and stay surfaceless.
    const bool first = shared_contexts_created++ == 0;
    return std::make_unique<MesaContext>(runtime, context, !first,
                                         first ? const_cast<GlWindow*>(this) : nullptr);
}

void GlWindow::SetOverlay(std::optional<D3D12::MenuImage> image) {
    std::scoped_lock lock{overlay_mutex};
    overlay = std::move(image);
    overlay_dirty = true;
}

void GlWindow::SetPacing(u32 max_fps, u32 skip) {
    pacing_max_fps = std::min<u32>(max_fps, 240);
    pacing_skip = std::min<u32>(skip, 3);
}

bool GlWindow::ShowThisFrame() {
    const u64 frame = paced_frames++;
    if (pacing_skip != 0 && frame % (pacing_skip + 1) != 0) return false;
    if (pacing_max_fps == 0) return true;
    const auto now = std::chrono::steady_clock::now();
    const auto period = std::chrono::nanoseconds{1'000'000'000LL / pacing_max_fps};
    // A little early is fine: the display's own refresh decides the exact moment.
    if (now + period / 8 < next_shown) return false;
    next_shown = std::max(next_shown + period, now);
    return true;
}

void GlWindow::CaptureFrame() {
    {
        std::scoped_lock lock{overlay_mutex};
        // Under the menu the game is paused: keep the frame from before it opened.
        if (overlay) return;
    }
    if (last_frame == 0) {
        glCreateTextures(GL_TEXTURE_2D, 1, &last_frame);
        glTextureStorage2D(last_frame, 1, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        glCreateFramebuffers(1, &last_frame_fbo);
        glNamedFramebufferTexture(last_frame_fbo, GL_COLOR_ATTACHMENT0, last_frame, 0);
        glNamedFramebufferReadBuffer(last_frame_fbo, GL_COLOR_ATTACHMENT0);
        glNamedFramebufferDrawBuffer(last_frame_fbo, GL_COLOR_ATTACHMENT0);
    }
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    std::array<GLboolean, 4> mask{};
    glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const auto w = static_cast<GLint>(width), h = static_cast<GLint>(height);
    glBlitNamedFramebuffer(0, last_frame_fbo, 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    has_last_frame = true;
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    if (scissor) glEnable(GL_SCISSOR_TEST);
}

void GlWindow::DrawOverlay(bool restore_frame) {
    std::scoped_lock lock{overlay_mutex};
    if (overlay_dirty) {
        overlay_dirty = false;
        if (overlay_texture != 0) {
            glDeleteTextures(1, &overlay_texture);
            overlay_texture = 0;
        }
        if (overlay && overlay->width > 0 && overlay->height > 0 &&
            overlay->pixels.size() >= static_cast<std::size_t>(overlay->width) * overlay->height * 4) {
            GLint unpack = 0, alignment = 0, row_length = 0;
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glCreateTextures(GL_TEXTURE_2D, 1, &overlay_texture);
            glTextureStorage2D(overlay_texture, 1, GL_RGBA8, static_cast<GLsizei>(overlay->width),
                               static_cast<GLsizei>(overlay->height));
            glTextureSubImage2D(overlay_texture, 0, 0, 0, static_cast<GLsizei>(overlay->width),
                                static_cast<GLsizei>(overlay->height), GL_RGBA, GL_UNSIGNED_BYTE,
                                overlay->pixels.data());
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack));
            glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
            overlay_width = overlay->width;
            overlay_height = overlay->height;
        }
    }
    if (!overlay && !restore_frame) return;
    // A blit touches no program, vertex or blend state the renderer's state tracker caches; only
    // the scissor, the color mask and sRGB writes apply to it.
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    std::array<GLboolean, 4> mask{};
    glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (restore_frame) {
        // The guest is paused and presents nothing: the menu goes over its last frame, as with
        // D3D12 (on black when no frame was shown yet).
        if (has_last_frame) {
            const auto w = static_cast<GLint>(width), h = static_cast<GLint>(height);
            glBlitNamedFramebuffer(last_frame_fbo, 0, 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        } else {
            constexpr std::array<GLfloat, 4> black{0.0f, 0.0f, 0.0f, 1.0f};
            glClearNamedFramebufferfv(0, GL_COLOR, 0, black.data());
        }
    }
    if (overlay && overlay->fill_screen) {
        const u32 rgb = overlay->fill;
        const std::array<GLfloat, 4> color{static_cast<GLfloat>((rgb >> 16) & 0xFF) / 255.0f,
                                           static_cast<GLfloat>((rgb >> 8) & 0xFF) / 255.0f,
                                           static_cast<GLfloat>(rgb & 0xFF) / 255.0f, 1.0f};
        glClearNamedFramebufferfv(0, GL_COLOR, 0, color.data());
    }
    if (overlay && overlay_texture != 0) {
        GLuint source = 0;
        glCreateFramebuffers(1, &source);
        glNamedFramebufferTexture(source, GL_COLOR_ATTACHMENT0, overlay_texture, 0);
        glNamedFramebufferReadBuffer(source, GL_COLOR_ATTACHMENT0);
        // Centered at its own size; image rows start at the top, the window's at the bottom.
        const auto w = static_cast<GLint>(overlay_width), h = static_cast<GLint>(overlay_height);
        const GLint x = (static_cast<GLint>(width) - w) / 2;
        const GLint y = (static_cast<GLint>(height) - h) / 2;
        glBlitNamedFramebuffer(source, 0, 0, 0, w, h, x, y + h, x + w, y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glDeleteFramebuffers(1, &source);
    }
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    if (scissor) glEnable(GL_SCISSOR_TEST);
}

void GlWindow::PresentOverlayNow() {
    // The guest is paused: the menu goes over the last frame the game showed.
    DrawOverlay(true);
    ForceOpaque();
    if (!runtime->swap(runtime->dc)) {
        LOG_ERROR(Frontend, "OpenGL: the menu could not be presented");
    }
}

} // namespace EdenXbox
