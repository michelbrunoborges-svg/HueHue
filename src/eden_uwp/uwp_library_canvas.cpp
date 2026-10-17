// SPDX-FileCopyrightText: Copyright 2026 JulianDr14
// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden_uwp/uwp_library_canvas.h"
#include "eden_uwp/pro_controller_geometry.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <d3d11.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include "eden_uwp/cheats/cheats_view.h"
#include "eden_uwp/game_library.h"
#include "eden_uwp/game_options_page.h"
#include "eden_uwp/content/content_view.h"
#include "eden_uwp/mods/mods_view.h"
#include "eden_uwp/localization.h"
#include "eden_uwp/ui_painter.h"
#include "eden_uwp/text_entry_panel.h"
#include "eden_uwp/ui_text.h"
#include "eden_uwp/uwp_input.h"

namespace EdenXbox {
using Microsoft::WRL::ComPtr;
using I18n::Tr;
using T = I18n::Text;
// The launcher owns a small D3D11/Direct2D surface only while selecting a game.
// Release every reference (including the swapchain) before D3D12 starts.
struct LibraryCanvas::Impl : UiPainter {
public:
    Impl(void* window, unsigned width, unsigned height) {
        ComPtr<ID3D11Device> device;
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &device, nullptr, &context));
        ComPtr<IDXGIDevice> dxgi;
        winrt::check_hresult(device.As(&dxgi));
        ComPtr<IDXGIAdapter> adapter;
        winrt::check_hresult(dxgi->GetAdapter(&adapter));
        ComPtr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(IID_PPV_ARGS(&factory)));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = std::max(1U, width);
        desc.Height = std::max(1U, height);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        winrt::check_hresult(factory->CreateSwapChainForCoreWindow(device.Get(),
            static_cast<IUnknown*>(window), &desc, nullptr, &swapchain));
        ComPtr<ID2D1Factory1> d2d;
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                              IID_PPV_ARGS(&d2d)));
        ComPtr<ID2D1Device> d2d_device;
        winrt::check_hresult(d2d->CreateDevice(dxgi.Get(), &d2d_device));
        winrt::check_hresult(d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                           &target));
        ComPtr<IDXGISurface> backbuffer;
        winrt::check_hresult(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)));
        const auto properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(desc.Format, D2D1_ALPHA_MODE_IGNORE));
        ComPtr<ID2D1Bitmap1> bitmap;
        winrt::check_hresult(target->CreateBitmapFromDxgiSurface(backbuffer.Get(),
                                                                &properties, &bitmap));
        target->SetTarget(bitmap.Get());
        ComPtr<IDWriteFactory> factory_write;
        winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory_write.GetAddressOf())));
        Attach(target.Get(), factory_write.Get());
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
        // Cache the installed version so the footer follows manifest updates
        // without a second version constant or per-frame WinRT calls.
        const auto version = winrt::Windows::ApplicationModel::Package::Current().Id().Version();
        const auto version_text = L"v" + std::to_wstring(version.Major) + L"." +
            std::to_wstring(version.Minor) + L"." + std::to_wstring(version.Build) +
            L"." + std::to_wstring(version.Revision);
        ComPtr<IDWriteTextFormat> version_format;
        winrt::check_hresult(write->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12, L"en", &version_format));
        winrt::check_hresult(write->CreateTextLayout(version_text.data(),
            static_cast<UINT32>(version_text.size()), version_format.Get(), 180, 18,
            &version_layout));
        winrt::check_hresult(version_layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING));
        version_string = version_text;
        try {
            asset_root = std::filesystem::path{
                winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str()} / L"Assets";
            // NS HUEHUE: the approved pack's art (design/ns-huehue-v1/assets), never regenerated.
            wall = LoadAssetBitmap(L"ns/pattern.png");
            ns_background = LoadAssetBitmap(L"ns/graffiti-background.jpg");
            ns_logo = LoadAssetBitmap(L"ns/logo.png");
            ns_mascot = LoadAssetBitmap(L"ns/mascot.png");
            ns_fallback = LoadAssetBitmap(L"ns/cover-fallback.png");
        } catch (const winrt::hresult_error&) { /* Wordmark remains available. */ }
        // "HUEHUE SEMPRE!": the pack's graffiti tag (Impact, falling back to Segoe UI Black).
        if (FAILED(write->CreateTextFormat(L"Impact", nullptr, DWRITE_FONT_WEIGHT_BLACK, DWRITE_FONT_STYLE_ITALIC,
                                           DWRITE_FONT_STRETCH_NORMAL, 54, L"pt-BR", &graffiti_format)))
            graffiti_format.Reset();
        const float scale = std::min(width / 1280.0f, height / 720.0f);
        target->SetTransform(D2D1::Matrix3x2F::Scale(std::max(scale, 0.01f), std::max(scale, 0.01f)) *
            D2D1::Matrix3x2F::Translation((width - 1280 * scale) / 2, (height - 720 * scale) / 2));
    }
    ~Impl() {
        target->SetTarget(nullptr);
        target.Reset();
        swapchain.Reset();
        context->ClearState();
        context->Flush();
    }
    void Draw(const LibraryView& view) {
        const auto& options = view.options;
        const auto& panel = view.panel;
        const auto& configuration = view.configuration;
        SyncLanguage();
        prompt_family = PromptFamily::Keyboard;
        if (panel.xbox) prompt_family = PromptFamily::Xbox;
        else if (const auto index = SelectController(panel.devices, options.controller_id))
            prompt_family = panel.devices[*index].pro ? PromptFamily::Nintendo : PromptFamily::Xbox;
        swap_prompt_faces = false;
        if (const auto active = SelectController(panel.devices, options.controller_id))
            swap_prompt_faces = SwapControllerFaceButtons(panel.devices[*active], options.swap_face_buttons);
        player_name = view.system.settings.user_name;
        target->BeginDraw();
        target->Clear(D2D1::ColorF(Palette::Backdrop));
        // The NS HUEHUE sections; the pages that keep Eden's own layout draw over the pack's ground.
        if (view.controller_panel) DrawNsControls(view);
        else if (configuration.open && view.system.open) DrawSystemPage(view.system);
        else if (configuration.open) DrawNsSettings(view);
        else if (view.home) DrawNsHome(view);
        else if (view.search) DrawNsSearch(view, *view.search);
        else DrawNsLibrary(view);
        if (panel.keyboard.open) DrawKeyboard(panel.keyboard);
        if (configuration.open && view.system.open && view.system.name_editor) DrawNameEditor(view.system);
        if (view.game_options.game || view.game_options.content) DrawGameOptions(view.game_options);
        if (view.missing_content) Content::DrawMissingContent(*this);
        winrt::check_hresult(target->EndDraw());
        winrt::check_hresult(swapchain->Present(1, 0));
    }
    void InvalidateCovers() { covers.clear(); pictures.clear(); }
private:
    // --- NS HUEHUE (design pack v1, design/ns-huehue-v1) ----------------------------------------
    // The approved screens are laid out on a 1920x1080 canvas; DesignSpace draws them there on top
    // of the library's own 1280x720 transform. Colors are the pack's tokens (ui_painter.h).
    static constexpr size_t HomeRows = 3;
    static constexpr size_t GridColumns = 5;      // "covers": repeat(5, 1fr)
    static constexpr size_t GridVisible = 10;     // two rows on screen
    static constexpr float CardWidth = 323.2f;    // (1728 - 4 * 28) / 5
    static constexpr float CardHeight = 291;     // 8 + cover 199 + name + line + 8
    static constexpr float GridTop = 376;
    static constexpr float CardGap = 28;
    struct DesignSpace {
        explicit DesignSpace(ID2D1DeviceContext* dc_) : dc(dc_) {
            dc->GetTransform(&saved);
            dc->SetTransform(D2D1::Matrix3x2F::Scale(2.0f / 3.0f, 2.0f / 3.0f) *
                             *D2D1::Matrix3x2F::ReinterpretBaseType(&saved));
        }
        ~DesignSpace() { dc->SetTransform(saved); }
        DesignSpace(const DesignSpace&) = delete;
        DesignSpace& operator=(const DesignSpace&) = delete;
        ID2D1DeviceContext* dc;
        D2D1_MATRIX_3X2_F saved{};
    };
    /// Letter-spaced capitals ("SEU JOGO. SUA RESENHA.").
    static std::wstring Spaced(std::wstring_view text) {
        std::wstring out;
        for (const wchar_t c : text) {
            if (!out.empty()) out += L' ';
            out += c;
        }
        return out;
    }
    ID2D1Bitmap1* NsIcon(std::wstring_view name, bool dark = false) {
        const std::wstring key = L"ns/icon-" + std::wstring{name} + (dark ? L"-dark.png" : L".png");
        auto found = ns_icons.find(key);
        if (found == ns_icons.end()) found = ns_icons.emplace(key, LoadAssetBitmap(key)).first;
        return found->second.Get();
    }
    /// Draws `bitmap` inside `box`: scaled to cover it (cropped) or to fit in it, aligned 0..1.
    void NsImage(ID2D1Bitmap1* bitmap, D2D1_RECT_F box, bool cover, float opacity = 1,
                 float align_x = 0.5f, float align_y = 0.5f) {
        if (!bitmap) return;
        const auto size = bitmap->GetSize();
        if (size.width <= 0 || size.height <= 0) return;
        const float bw = box.right - box.left, bh = box.bottom - box.top;
        const float scale = cover ? std::max(bw / size.width, bh / size.height)
                                  : std::min(bw / size.width, bh / size.height);
        const float w = size.width * scale, h = size.height * scale;
        const float x = box.left + (bw - w) * align_x, y = box.top + (bh - h) * align_y;
        if (cover) target->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
        target->DrawBitmap(bitmap, D2D1::RectF(x, y, x + w, y + h), opacity,
                           D2D1_INTERPOLATION_MODE_LINEAR);
        if (cover) target->PopAxisAlignedClip();
    }
    void NsFill(float x, float y, float w, float h, float radius, unsigned color, float alpha = 1) {
        brush->SetColor(D2D1::ColorF(color, alpha));
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius),
                                     brush.Get());
    }
    void NsStroke(float x, float y, float w, float h, float radius, float width, unsigned color,
                  float alpha = 1) {
        brush->SetColor(D2D1::ColorF(color, alpha));
        target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius),
                                     brush.Get(), width);
    }
    /// The pack's focus: a 3 px outline 5 px out, with a faint halo.
    void NsFocus(float x, float y, float w, float h, float radius) {
        NsStroke(x - 10, y - 10, w + 20, h + 20, radius + 10, 10, Palette::Accent, 0.12f);
        NsStroke(x - 6.5f, y - 6.5f, w + 13, h + 13, radius + 6.5f, 3, Palette::Accent);
    }
    /// A left-to-right fade of `color` from `from` to `to` alpha, between `start` and `end` (0..1).
    void NsFade(D2D1_RECT_F rect, unsigned color, float from, float to, float start, float end) {
        const D2D1_GRADIENT_STOP stops[] = {{start, D2D1::ColorF(color, from)},
                                            {end, D2D1::ColorF(color, to)}};
        auto* const render = static_cast<ID2D1RenderTarget*>(target.Get());
        ComPtr<ID2D1GradientStopCollection> collection;
        if (FAILED(render->CreateGradientStopCollection(stops, 2, &collection))) return;
        ComPtr<ID2D1LinearGradientBrush> fade;
        if (FAILED(render->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(rect.left, rect.top),
                                                    D2D1::Point2F(rect.right, rect.top)),
                collection.Get(), &fade)))
            return;
        target->FillRectangle(rect, fade.Get());
    }
    /// The ground of every screen: the graffiti art (home, splash) or the quiet pattern, and the
    /// 6 px green line along the top.
    void NsBackdrop(bool art) {
        const D2D1_RECT_F full = D2D1::RectF(0, 0, 1920, 1080);
        Round(0, 0, 1920, 1080, 0, Palette::Backdrop);
        if (art) {
            NsImage(ns_background.Get(), full, true);
            NsFade(full, 0x03090b, 0.85f, 0.15f, 0.10f, 0.80f);
        } else {
            NsImage(wall.Get(), full, true);
            NsFade(full, Palette::Backdrop, 0.15f, 0.5f, 0.0f, 1.0f);
        }
        Round(0, 0, 1920, 6, 0, Palette::Accent);
    }
    /// Logo, the five sections (`active` underlined), the player's name and the rule under them.
    /// The pack's .brand: the logo inside 250x90, left aligned.
    void NsBrand() {
        if (ns_logo) NsImage(ns_logo.Get(), D2D1::RectF(96, 31, 346, 121), false, 1, 0, 0.5f);
        else Text(L"NS HUEHUE", 96, 48, 40, Palette::Magenta, 250, 56, true);
    }
    void NsHeader(int active) {
        NsBrand();
        // Sections 1..3: Meus jogos, Configuracoes, Controles.
        static constexpr std::array<T, 3> nav{T::NsNavGames, T::NsNavSettings, T::NsNavControls};
        float x = 396;
        for (int i = 0; i < 3; ++i) {
            const auto label = Tr(nav[static_cast<size_t>(i)]);
            const float w = MeasureText(label, 27, false) + 44;
            const bool on = i + 1 == active;
            Text(label, x + 22, 52, 27, on ? Palette::Accent : Palette::TextSecondary, w, 44);
            if (on) Round(x, 108, w, 4, 0, Palette::Accent);
            x += w + 16;
        }
        const std::wstring name = player_name.empty() ? std::wstring{Tr(T::NsPlayer)} : player_name;
        const float name_width = MeasureText(name, 24, false);
        Text(name, 1824 - name_width - 2, 56, 24, Palette::Text, name_width + 4, 40);
        NsImage(NsIcon(L"user"), D2D1::RectF(1824 - name_width - 57, 56, 1824 - name_width - 17, 96), false);
        Round(96, 152, 1728, 1, 0, Ns::Divider);
    }
    void NsTitle(std::wstring_view title) { Text(title, 96, 182, 60, Palette::Text, 1500, 80, true); }
    struct NsKey {
        wchar_t letter;
        unsigned color;
        T label;
    };
    /// The rule and the button glyphs at the bottom: only the shortcuts this screen has.
    void NsFooter(std::initializer_list<NsKey> keys) {
        Round(96, 996, 1728, 1, 0, Ns::Divider);
        float x = 96;
        for (const NsKey& key : keys) {
            wchar_t letter = key.letter;
            if (swap_prompt_faces && (letter == L'A' || letter == L'B')) letter = letter == L'A' ? L'B' : L'A';
            brush->SetColor(D2D1::ColorF(key.color));
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 16, 1038), 16, 16), brush.Get());
            Text(std::wstring(1, letter), x, 1022, 21, Palette::OnAccent, 32, 32, true, false, true);
            const auto label = Tr(key.label);
            const float width = MeasureText(label, 23, false);
            Text(label, x + 44, 1021, 23, Palette::Text, width + 6, 36);
            x += 44 + width + 40;
        }
    }
    /// A short message in the footer's right half (checking a game, a saved favorite...).
    void NsToast(std::wstring_view text) {
        if (text.empty()) return;
        NsFill(1060, 1006, 764, 64, 6, Ns::Toast, 0.96f);
        Text(text, 1084, 1019, 22, Palette::Accent, 720, 38);
    }
    /// A pack button: primary (green) or secondary, focused or not.
    void NsButton(float x, float y, float w, std::wstring_view icon, std::wstring_view label,
                  bool primary, bool focus) {
        NsFill(x, y, w, 70, 7, primary ? Palette::Accent : Ns::Secondary);
        if (!primary) NsStroke(x + 1, y + 1, w - 2, 68, 6, 2, Ns::Line);
        if (focus) NsFocus(x, y, w, 70, 7);
        NsImage(NsIcon(icon, primary), D2D1::RectF(x + 26, y + 17, x + 62, y + 53), false);
        Text(label, x + 82, y + 14, 28, primary ? Palette::OnAccent : Palette::Text, w - 100, 42, true);
    }
    /// A pack row: label on the left, value on the right; focus fills it green.
    void NsRow(float x, float y, float w, std::wstring_view label, std::wstring_view value,
               bool focus, float size = 27, float height = 78) {
        Round(x, y, w, height, 0, focus ? Palette::RowActive : Palette::Row);
        Round(x, y + height - 1, w, 1, 0, 0x2a393d);
        if (focus) NsFocus(x, y, w, height, 2);
        Text(label, x + 22, y + (height - size * 1.45f) / 2, size, Palette::Text, w * 0.55f, size * 1.6f);
        if (!value.empty())
            Text(value, x + w * 0.45f, y + (height - size * 1.45f) / 2, size,
                 focus ? Palette::Accent : Palette::TextSecondary, w * 0.55f - 22, size * 1.6f, true, false,
                 false, true);
    }
    void DrawGraffiti(std::wstring_view text, float x, float y) {
        if (!graffiti_format) return;
        D2D1_MATRIX_3X2_F base{};
        target->GetTransform(&base);
        target->SetTransform(D2D1::Matrix3x2F::Rotation(9, D2D1::Point2F(x + 150, y + 60)) *
                             *D2D1::Matrix3x2F::ReinterpretBaseType(&base));
        brush->SetColor(D2D1::ColorF(Palette::Accent));
        target->DrawText(text.data(), static_cast<UINT32>(text.size()), graffiti_format.Get(),
                         D2D1::RectF(x, y, x + 360, y + 140), brush.Get());
        target->SetTransform(base);
    }

    // 01 · Início
    void DrawNsHome(const LibraryView& view) {
        DesignSpace space(target.Get());
        NsBackdrop(true);
        NsImage(ns_mascot.Get(), D2D1::RectF(960, 120, 1900, 1110), false, 1, 0.5f, 1.0f);
        DrawGraffiti(L"HUEHUE\nSEMPRE!", 1640, 122);
        if (ns_logo) NsImage(ns_logo.Get(), D2D1::RectF(96, 125, 816, 325), false, 1, 0, 0.5f);
        else Text(L"NS HUEHUE", 96, 150, 96, Palette::Magenta, 800, 130, true);
        Text(Spaced(Tr(T::NsTagline)), 97, 350, 24, Palette::Text, 900, 36);
        static constexpr std::array<std::pair<T, const wchar_t*>, HomeRows> items{{
            {T::NsNavGames, L"games"}, {T::NsNavSettings, L"settings"}, {T::NsNavControls, L"pad"}}};
        // .home-menu .button: 500 wide, 73 tall with its border, 18 apart.
        for (size_t i = 0; i < HomeRows; ++i) {
            const float y = 431 + static_cast<float>(i) * 91;
            const bool focus = i == view.home_row;
            NsFill(96, y, 500, 73, 7, focus ? Palette::AccentDeep : Ns::MenuButton, focus ? 1.0f : 0.85f);
            NsStroke(97, y + 1, 498, 71, 6, 2, Ns::Divider);
            if (focus) NsFocus(96, y, 500, 73, 7);
            NsImage(NsIcon(items[i].second), D2D1::RectF(124, y + 18, 160, y + 54), false);
            Text(Tr(items[i].first), 180, y + 15, 28, focus ? Palette::Accent : Palette::Text, 400, 42, true);
        }
        NsFooter({{L'A', Palette::Accent, T::NsConfirm}, {L'B', Ns::Error, T::Exit}});
        NsToast(view.notice);
    }

    bool IsFavorite(const LibraryView& view, const LibraryEntry& entry) const {
        return view.favorites &&
               std::find(view.favorites->begin(), view.favorites->end(), entry.launch_path) != view.favorites->end();
    }

    /// One cover card of the grid (.game): cover, favorite star, name and a line under it.
    void DrawNsCard(const LibraryView& view, const LibraryEntry& entry, float cx, float cy) {
        NsFill(cx, cy, CardWidth, CardHeight, 8, Palette::Panel);
        NsStroke(cx + 0.5f, cy + 0.5f, CardWidth - 1, CardHeight - 1, 8, 1, Ns::Line);
        const D2D1_RECT_F cover = D2D1::RectF(cx + 8, cy + 8, cx + CardWidth - 8, cy + 207);
        Round(cover.left, cover.top, cover.right - cover.left, cover.bottom - cover.top, 4, 0x0d1a1d);
        ID2D1Bitmap1* const art = CoverBitmap(entry);
        NsImage(art ? art : ns_fallback.Get(), cover, true);
        if (IsFavorite(view, entry)) {
            NsFill(cover.right - 50, cover.top + 10, 40, 40, 20, Palette::Backdrop, 0.7f);
            NsImage(NsIcon(L"star"), D2D1::RectF(cover.right - 44, cover.top + 16, cover.right - 16, cover.top + 44), false);
        }
        Text(entry.name, cx + 15, cy + 218, 23, Palette::Text, CardWidth - 30, 32, true);
        std::wstring under = entry.display_version.empty() ? std::wstring{} : L"v" + entry.display_version;
        if (!entry.source_name.empty()) under += (under.empty() ? L"" : L" · ") + entry.source_name;
        if (!entry.pulse_saved.empty()) under += under.empty() ? L"Pulse" : L" · Pulse";
        if (under.empty()) under = entry.relative_path.extension().wstring();
        Text(under, cx + 15, cy + 252, 17, Palette::TextSecondary, CardWidth - 30, 26);
    }

    // 02-05, 09, 47, 51 · Meus jogos
    // 09 · Biblioteca vazia: A opens Arquivos e USB.
    void DrawNsEmpty() {
        NsTitle(Tr(T::NsEmptyTitle));
        NsImage(ns_mascot.Get(), D2D1::RectF(1190, 150, 1890, 1050), false, 0.6f, 0.5f, 1.0f);
        NsImage(NsIcon(L"games"), D2D1::RectF(186, 268, 240, 322), false);
        Text(Tr(T::NsEmptyHeadline), 186, 330, 76, Palette::Text, 950, 100, true);
        Text(Tr(T::NsEmptyBody), 186, 440, 28, Palette::Text, 950, 50);
        NsButton(186, 512, 420, L"folder", Tr(T::NsSettingsFiles), true, true);
        Text(Tr(T::EmptyBody), 186, 622, 23, Palette::TextSecondary, 900, 80, false, true);
        NsFooter({{L'A', Palette::Accent, T::NsConfirm}, {L'B', Ns::Error, T::Back}});
    }

    void DrawNsLibrary(const LibraryView& view) {
        DesignSpace space(target.Get());
        NsBackdrop(false);
        NsHeader(1);
        if (!view.loading && view.scan.entries.empty()) {
            DrawNsEmpty();
            NsToast(view.notice);
            return;
        }
        NsTitle(Tr(T::NsNavGames));
        static constexpr std::array<T, 3> tabs{T::NsTabAll, T::NsTabFavorites, T::NsTabRecent};
        float x = 96;
        for (unsigned i = 0; i < 3; ++i) {
            const bool selected = i == view.library_tab;
            const auto label = Tr(tabs[i]);
            const float w = MeasureText(label, 25, selected) + 50;
            NsFill(x, 285, w, 59, 6, selected ? Palette::Accent : Ns::Secondary);
            if (!selected) NsStroke(x + 0.5f, 285.5f, w - 1, 58, 6, 1, Ns::Line);
            Text(label, x, 285, 25, selected ? Palette::OnAccent : Palette::Text, w, 59, selected, false, true);
            if (view.library_focus == static_cast<int>(i)) NsFocus(x, 285, w, 59, 6);
            x += w + 16;
        }
        NsFill(1414, 268, 410, 61, 8, Ns::MenuButton);
        NsStroke(1414.5f, 268.5f, 409, 60, 8, 1, view.query.empty() ? Ns::Line : Palette::Accent);
        NsImage(NsIcon(L"search"), D2D1::RectF(1435, 286, 1461, 312), false);
        Text(view.query.empty() ? std::wstring{Tr(T::NsSearchGames)} : std::wstring{view.query}, 1477, 280, 23,
             view.query.empty() ? Palette::TextSecondary : Palette::Text, 280, 38);
        Text(L"Y", 1772, 280, 23, Palette::TextSecondary, 32, 38, true);
        if (view.library_focus == 3) NsFocus(1414, 268, 410, 61, 8);

        const auto card_x = [](size_t slot) { return 96 + static_cast<float>(slot % GridColumns) * (CardWidth + CardGap); };
        const auto card_y = [](size_t slot) { return GridTop + static_cast<float>(slot / GridColumns) * (CardHeight + CardGap); };
        if (view.loading) {
            for (size_t slot = 0; slot < GridColumns; ++slot) {
                NsFill(card_x(slot), GridTop, CardWidth, 390, 8, 0x1b282b);
                NsStroke(card_x(slot) + 0.5f, GridTop + 0.5f, CardWidth - 1, 389, 8, 1, Ns::Divider);
            }
            Text(Tr(T::LoadingTitle), 96, 820, 34, Palette::Text, 1500, 50, true);
            Text(Tr(T::LoadingBody), 96, 876, 23, Palette::TextSecondary, 1500, 40);
        } else if (!view.shown || view.shown->empty()) {
            const T message = !view.query.empty() ? T::NsNoResults
                            : view.library_tab == 1 ? T::NsNoFavorites : T::NsNoRecent;
            NsImage(NsIcon(view.library_tab == 1 && view.query.empty() ? L"star" : L"search"),
                    D2D1::RectF(186, 420, 276, 510), false);
            Text(Tr(message), 186, 535, 34, Palette::Text, 1200, 120, true, true);
        } else {
            const auto& list = *view.shown;
            const auto at = std::find(list.begin(), list.end(), view.selected);
            const size_t position = at == list.end() ? 0 : static_cast<size_t>(at - list.begin());
            const size_t first = position / GridVisible * GridVisible;
            for (size_t k = first; k < std::min(first + GridVisible, list.size()); ++k) {
                const size_t slot = k - first;
                const float cx = card_x(slot), cy = card_y(slot);
                DrawNsCard(view, view.scan.entries[list[k]], cx, cy);
                if (list[k] == view.selected && view.library_focus < 0) NsFocus(cx, cy, CardWidth, CardHeight, 8);
            }
            Text(std::to_wstring(position + 1) + L" / " + std::to_wstring(list.size()), 1524, 200, 23,
                 Palette::TextSecondary, 300, 36, false, false, false, true);
        }
        NsFooter({{L'A', Palette::Accent, T::NsConfirm}, {L'B', Ns::Error, T::Back},
                  {L'X', Ns::KeyX, T::NsOptions}, {L'Y', Ns::KeyY, T::NsSearch}});
        NsToast(view.notice);
    }

    // 11-16 · Configurações (the settings home and the file manager)
    void DrawNsSettings(const LibraryView& view) {
        const auto& panel = view.configuration;
        DesignSpace space(target.Get());
        NsBackdrop(false);
        NsHeader(2);
        static constexpr std::array<T, HomeRowCount> sections{T::NsSettingsSystem, T::NsSettingsControllers,
                                                              T::NsSettingsFiles, T::NsCredits};
        static constexpr std::array<const wchar_t*, HomeRowCount> icons{L"settings", L"pad", L"folder", L"info"};
        const size_t section = panel.files ? static_cast<size_t>(HomeFiles) : std::min<size_t>(panel.selected, HomeRowCount - 1);
        NsTitle(std::wstring{Tr(T::NsNavSettings)} + L" · " + std::wstring{Tr(sections[section])});
        for (size_t i = 0; i < HomeRowCount; ++i) {
            const float y = 294 + static_cast<float>(i) * 89;
            const bool selected = i == section;
            if (selected) {
                Round(96, y, 350, 80, 6, Ns::NavSelected);
                Round(96, y, 4, 80, 0, Palette::Accent);
            }
            NsImage(NsIcon(icons[i]), D2D1::RectF(123, y + 24, 155, y + 56), false);
            Text(Tr(sections[i]), 169, y + 18, 29, selected ? Palette::Accent : Palette::TextSecondary, 270, 44);
            if (selected && !panel.files) NsFocus(96, y, 350, 80, 6);
        }
        const float left = 521, width = 1280;
        Text(Tr(sections[section]), left, 292, 34, Palette::Text, width, 50, true);
        if (!panel.files && section == HomeCredits) {
            // Créditos e licenças (they used to be the Sobre page): who the work comes from, the
            // version and the license note. Nothing here opens.
            static constexpr std::array<std::pair<const wchar_t*, T>, 5> credits{{
                {L"eden-xbox · JulianDr14", T::NsRolePort}, {L"Eden · yuzu · Sudachi", T::NsRoleBase},
                {L"NXbox · NspxMiguel", T::NsRoleUi}, {L"Mesa · dynarmic · DXC", T::NsRoleGraphics},
                {L"NS HUEHUE · BR HueHue Edition", T::NsRoleEdition}}};
            for (size_t i = 0; i < credits.size(); ++i)
                NsRow(left, 356 + static_cast<float>(i) * 78, width, credits[i].first, Tr(credits[i].second), false);
            Text(I18n::Format(T::NsExperimental, {version_string}), left, 762, 23, Palette::TextSecondary, width, 36);
            Text(std::wstring{Tr(T::NsCreditsBody)} + L" " + std::wstring{Tr(T::NsNotAffiliated)}, left, 804, 21,
                 Palette::TextSecondary, width, 120, false, true);
        } else if (!panel.files) {
            // A preview of the section; A opens it.
            std::array<std::pair<std::wstring, std::wstring>, 3> rows;
            if (section == HomeSystem) {
                rows = {{{std::wstring{Tr(T::UserName)}, view.system.settings.user_name.empty() ? std::wstring{Tr(T::NsPlayer)} : view.system.settings.user_name},
                         {std::wstring{Tr(T::SystemTitle)}, std::wstring{I18n::CurrentUiLanguage().native_name}},
                         {std::wstring{Tr(T::NsOpen)}, L"›"}}};
            } else if (section == HomeControllers) {
                rows = {{{std::wstring{Tr(T::ControllerDevice)}, ControllerLabel(view.panel.devices, view.options.controller_id)},
                         {std::wstring{Tr(T::ControllerSeenAs)}, std::wstring{Tr(ControllerStyleText(view.options.style))}},
                         {std::wstring{Tr(T::NsOpen)}, L"›"}}};
            } else {
                rows = {{{std::wstring{Tr(T::EmulatorData)}, std::wstring{Tr(panel.status.keys_ready ? T::KeysReady : T::KeysMissing)}},
                         {std::wstring{Tr(T::ImportFirmware)}, I18n::Format(T::FirmwareFiles, {std::to_wstring(panel.status.firmware_files)})},
                         {std::wstring{Tr(T::NsOpen)}, L"›"}}};
            }
            for (size_t i = 0; i < rows.size(); ++i)
                NsRow(left, 356 + static_cast<float>(i) * 78, width, rows[i].first, rows[i].second, false);
            Text(Tr(section == HomeSystem ? T::SystemHint : section == HomeControllers ? T::ControllersHint : T::FileManagerHint),
                 left, 612, 21, Palette::TextSecondary, width, 70, false, true);
        } else {
            const auto count = ConfigurationRowCount(panel);
            const auto first = ConfigurationFirstRow(panel);
            for (size_t i = first; i < std::min(first + 5, count); ++i) {
                std::wstring title;
                if (i == 0) title = Tr(T::AddGamesFolder);
                else if (i == 1) title = Tr(T::ImportKeys);
                else if (i == 2) title = Tr(T::ImportFirmware);
                else if (i == 3) title = Tr(T::ContentUpdates);
                else if (i == 4) title = Tr(T::ContentDlc);
                else title = I18n::Format(T::RemoveFolder, {panel.status.sources[i - 5].name});
                const std::wstring value = i == 1 ? std::wstring{Tr(panel.status.keys_ready ? T::KeysReady : T::KeysMissing)}
                                         : i == 2 ? I18n::Format(T::FirmwareFiles, {std::to_wstring(panel.status.firmware_files)})
                                                  : std::wstring{L"›"};
                NsRow(left, 356 + static_cast<float>(i - first) * 78, width, title, value, i == panel.selected);
            }
            Text(Tr(T::FilesSubtitle), left, 770, 21, Palette::TextSecondary, width, 60, false, true);
        }
        if (panel.busy)
            Text(I18n::Format(T::Importing, {std::to_wstring(panel.completed), std::to_wstring(panel.total)}),
                 left, 920, 24, Palette::Accent, width, 40);
        else if (!panel.notice.empty())
            Text(panel.notice, left, 920, 23, Palette::Warning, width, 70, false, true);
        if (!panel.files && section == HomeCredits) NsFooter({{L'B', Ns::Error, T::Back}});
        else NsFooter({{L'A', Palette::Accent, T::NsConfirm}, {L'B', Ns::Error, panel.busy ? T::Cancel : T::Back}});
    }

    // 18 · Controles (the controller page of the library)
    void DrawNsControls(const LibraryView& view) {
        const auto& options = view.options;
        const auto& panel = view.panel;
        DesignSpace space(target.Get());
        NsBackdrop(false);
        NsHeader(3);
        NsTitle(Tr(T::NsNavControls));
        NsImage(NsIcon(L"pad"), D2D1::RectF(300, 402, 800, 802), false);
        const auto callout = [&](T text, float x, float y, bool right) {
            const auto label = Tr(text);
            const float w = MeasureText(label, 26, false) + 4;
            const float lx = right ? x - w : x;
            Text(label, lx, y, 26, Palette::Accent, w, 40);
            Round(lx, y + 46, w, 1, 0, 0x496065);
        };
        callout(T::NsNavHint, 135, 345, false);
        callout(T::NsDpadHint, 120, 705, false);
        callout(T::NsConfirmHint, 970, 415, true);
        callout(T::NsBackHint, 970, 635, true);
        const float left = 1114, width = 710;
        Text(Tr(T::NsControlsHeading), left, 326, 34, Palette::Text, width, 50, true);
        const unsigned offset = panel.xbox ? 0 : 1;
        const auto active = SelectController(panel.devices, options.controller_id);
        const bool nintendo = active && panel.devices[*active].nintendo;
        float y = 396;
        for (unsigned row = 0; row < 3 + offset; ++row) {
            const bool device = offset && row == 0;
            const bool type = row == offset;
            const bool face = row == offset + 1;
            std::wstring value;
            if (device) {
                const auto label = ControllerLabel(panel.devices, options.controller_id);
                value = options.controller_id.empty() ? I18n::Format(T::AutomaticValue, {label}) : label;
            } else if (type) {
                value = Tr(ControllerStyleText(options.style));
            } else if (face) {
                value = Tr(nintendo ? T::NintendoFaceButtons : options.swap_face_buttons ? T::ByPosition : T::ByLetter);
            } else {
                value = std::to_wstring(static_cast<int>(options.deadzone * 100)) + L"%";
            }
            NsRow(left, y, width, Tr(device ? T::ControllerDevice : type ? T::ControllerSeenAs : face ? T::FaceButtons : T::DeadZone),
                  value, row == view.controller_row, 23, 76);
            y += 76;
        }
        if (!panel.xbox) {
            NsRow(left, y, width, Tr(T::SetUpKeyboard), L"›", view.controller_row == 4, 23, 76);
            y += 76;
        }
        Text(panel.notice.empty() ? std::wstring{Tr(T::ControllerSubtitle)} : panel.notice, left, y + 24, 23,
             panel.notice.empty() ? Palette::TextSecondary : Palette::Accent, width, 110, false, true);
        if (panel.expanded) {
            const size_t first = ControllerChoiceFirst(panel);
            const size_t count = panel.devices.size() + 2;
            const size_t end = std::min(count, first + ControllerVisibleChoices);
            NsFill(left - 12, 384, width + 24, static_cast<float>(end - first) * 70 + 24, 12, Palette::Panel);
            NsStroke(left - 11.5f, 384.5f, width + 23, static_cast<float>(end - first) * 70 + 23, 12, 1, Ns::Line);
            for (size_t i = first; i < end; ++i) {
                const bool session = i >= 2 && panel.devices[i - 2].id.starts_with(L"session:");
                const bool supported = i < 2 || (bool(panel.devices[i - 2]) && !session);
                std::wstring label;
                if (i == 0) label = Tr(T::AutoFirstController);
                else if (i == 1) label = Tr(T::Keyboard);
                else label = panel.devices[i - 2].name + L" · " +
                    std::wstring{Tr(supported ? (panel.devices[i - 2].wireless ? T::Wireless : T::Usb) :
                                    session ? T::AutomaticOnly : T::NoCompatibleMapping)};
                NsRow(left, 396 + static_cast<float>(i - first) * 70, width, label, L"", i == panel.choice, 23, 70);
            }
        }
        NsFooter({{L'A', Palette::Accent, T::NsConfirm}, {L'B', Ns::Error, T::Back}});
    }

    // 07 · Opções do jogo (the list; its pages draw themselves)
    void DrawNsGameOptions(const GameOptionsView& view, const LibraryEntry& game) {
        DesignSpace space(target.Get());
        NsBackdrop(false);
        NsHeader(1);
        NsTitle(Tr(T::NsGameOptions));
        NsFill(400, 286, 1120, 696, 12, Palette::Panel, 0.95f);
        NsStroke(400.5f, 286.5f, 1119, 695, 12, 1, Ns::Line);
        Text(game.name, 441, 328, 34, Palette::Text, 1040, 50, true);
        Round(441, 392, 1038, 1, 0, Ns::Line);
        const auto options = GameOptions();
        // The library's rows around Eden's: Iniciar jogo first, the favorite last.
        for (size_t i = 0; i < options.size() + 2; ++i) {
            const float y = 419 + static_cast<float>(i) * 77;
            std::wstring title, value = L"›";
            if (i == 0) {
                title = Tr(T::NsStartGame);
                value = swap_prompt_faces ? L"B" : L"A";
            } else if (i <= options.size()) {
                title = Tr(GameOptionTitle(options[i - 1]));
                if (options[i - 1] == GameOption::Pulse)
                    value = game.pulse_saved.empty() ? std::wstring{Tr(T::PulseNone)}
                          : I18n::Format(game.pulse_current ? T::PulseSavedOn : T::PulseSavedOther, {game.pulse_saved});
            } else {
                title = Tr(view.favorite ? T::NsFavoriteRemove : T::NsFavoriteAdd);
                value = L"☆";
            }
            NsRow(441, y, 1038, title, value, i == view.page.selected, 27, 77);
        }
        if (!view.notice.empty()) NsToast(view.notice);
        NsFooter({{L'A', Palette::Accent, T::NsConfirm}, {L'B', Ns::Error, T::Back}});
    }

    // 05, 51 · Buscar jogos: the box, the first result and the shared keyboard's keys.
    void DrawNsSearch(const LibraryView& view, const TextEntry& editor) {
        using K = TextEntryPanelLayout;
        using S = NsSearchLayout;
        DesignSpace space(target.Get());
        NsBackdrop(false);
        NsHeader(1);
        NsTitle(Tr(T::NsSearchGames));
        NsFill(96, 266, 1728, 72, 8, Palette::Backdrop);
        NsStroke(97.5f, 267.5f, 1725, 69, 8, 3, Palette::Accent);
        const std::wstring& value = editor.Value();
        const float typed = std::min(MeasureText(value, 28, false), 1600.0f);
        Text(value, 124, 282, 28, Palette::Text, 1620, 42);
        Round(126 + typed, 286, 2, 34, 1, Palette::Accent);
        const size_t found = view.shown ? view.shown->size() : 0;
        Text(found ? I18n::Plural(T::NsFoundOne, T::NsFoundOther, found) : std::wstring{Tr(T::NsNoResults)},
             96, 352, 23, Palette::TextSecondary, 1200, 36);
        if (found) {
            DrawNsCard(view, view.scan.entries[view.shown->front()], 96, S::KeysY);
            NsFocus(96, S::KeysY, CardWidth, CardHeight, 8);
        }
        // The panel draws its own title and box too: only its keys show here.
        target->PushAxisAlignedClip(D2D1::RectF(S::KeysX - 10, S::KeysY - 10, S::KeysX + K::BoxWidth + 10,
                                                S::KeysY + TextEntry::Rows * (K::KeyHeight + K::Gap) + 4),
                                    D2D1_ANTIALIAS_MODE_ALIASED);
        DrawTextEntryPanel(*this, {editor, std::wstring_view{}, std::wstring_view{}, std::wstring_view{}},
                           S::PanelX(), S::PanelY());
        target->PopAxisAlignedClip();
        NsFooter({{L'A', Palette::Accent, T::TypeKey}, {L'X', Ns::KeyX, T::DeleteKey}, {L'Y', Ns::KeyY, T::SpaceKey},
                  {L'\u2261', Palette::Text, T::DoneKey}, {L'B', Ns::Error, T::Cancel}});
    }

    void DrawControllerPanel(const LibraryView& view) {
        const auto& options = view.options;
        const auto& panel = view.panel;
        brush->SetColor(D2D1::ColorF(0, 0, 0, 0.72f));
        target->FillRectangle(D2D1::RectF(0, 0, 1280, 720), brush.Get());
        Round(620, 133, 586, 487, 24, 0x101b1e);
        Text(Tr(T::ControllerTitle), 658, 168, 34, 0xf4f7f5, 490, 55, true);
        Text(Tr(T::ControllerSubtitle), 658, 227, 21, 0xa6b5b7, 490);
        const unsigned offset = panel.xbox ? 0 : 1;
        const auto active = SelectController(panel.devices, options.controller_id);
        const bool nintendo = active && panel.devices[*active].nintendo;
        for (unsigned row = 0; row < 3 + offset; ++row) {
            const float y = ControllerRowTop + row * ControllerRowHeight;
            Round(648, y, 530, 56, 14, row == view.controller_row ? 0x1a321e : 0x162124);
            const bool device = offset && row == 0;
            const bool type = row == offset;
            const bool face = row == offset + 1;
            Text(Tr(device ? T::ControllerDevice : type ? T::ControllerSeenAs :
                    face ? T::FaceButtons : T::DeadZone),
                 669, y + 2, 19, 0xf4f7f5, 400, 28, true);
            if (device || type)
                Text(device ? L"v" : L"<  >", 1080, y + 2, 19, 0xf4f7f5, 80, 28, true, false, false, true);
            std::wstring value;
            if (device) {
                const auto label = ControllerLabel(panel.devices, options.controller_id);
                value = options.controller_id.empty() ? I18n::Format(T::AutomaticValue, {label}) : label;
            } else if (type) {
                value = Tr(ControllerStyleText(options.style));
            } else if (face) {
                value = Tr(nintendo ? T::NintendoFaceButtons :
                           options.swap_face_buttons ? T::ByPosition : T::ByLetter);
            } else {
                value = std::to_wstring(static_cast<int>(options.deadzone * 100)) + L"%";
            }
            Text(value, 669, y + 28, 18, 0x8aff00, 460, 26);
        }
        if (!panel.xbox) {
            Round(648, 510, 530, 34, 10, view.controller_row == 4 ? 0x1a321e : 0x162124);
            Text(std::wstring{Tr(T::SetUpKeyboard)} + L"   >", 669, 512, 19, 0x8aff00, 460, 30, true);
        }
        ControlPrompt(Navigation::Explore, Tr(T::Select), 658, 551, 90);
        ControlPrompt(Navigation::Play, Tr(T::Change), 838, 551, 100);
        ControlPrompt(Navigation::Back, Tr(T::Back), 1018, 551, 100);
        if (!panel.notice.empty()) Text(panel.notice, 658, 594, 16, 0x8aff00, 515, 25);
        if (panel.expanded) {
            const size_t first = ControllerChoiceFirst(panel);
            const size_t count = panel.devices.size() + 2;
            const size_t end = std::min(count, first + ControllerVisibleChoices);
            Round(648, ControllerChoiceTop - 4, 530,
                  (end - first) * ControllerChoiceHeight + 8, 12, 0x34353c);
            for (size_t i = first; i < end; ++i) {
                const float y = ControllerChoiceTop + (i - first) * ControllerChoiceHeight;
                Round(652, y, 522, 40, 8, i == panel.choice ? 0x1a321e : 0x162124);
                const bool session = i >= 2 && panel.devices[i - 2].id.starts_with(L"session:");
                const bool supported = i < 2 || (bool(panel.devices[i - 2]) && !session);
                std::wstring label;
                if (i == 0) label = Tr(T::AutoFirstController);
                else if (i == 1) label = Tr(T::Keyboard);
                else label = panel.devices[i - 2].name + L" · " +
                    std::wstring{Tr(supported ? (panel.devices[i - 2].wireless ? T::Wireless : T::Usb) :
                                    session ? T::AutomaticOnly : T::NoCompatibleMapping)};
                Text(label, 665, y + 6, 18, supported ? 0xf4f7f5 : 0xa6b5b7, 480, 30);
            }
            ScrollIndicator(1187, ControllerChoiceTop, ControllerVisibleChoices * ControllerChoiceHeight,
                            first, ControllerVisibleChoices, count);
        }
    }
    void DrawKeyboard(const KeyboardEditor& editor) {
        brush->SetColor(D2D1::ColorF(0, 0, 0, 0.86f));
        target->FillRectangle(D2D1::RectF(0, 0, 1280, 720), brush.Get());
        Round(80, 88, 1120, 584, 22, 0x121216);
        Text(Tr(T::KeyboardTitle), 120, 118, 28, 0xf4f7f5, 900, 44, true);
        Text(Tr(T::KeyboardSubtitle), 120, 166, 17, 0x9a9aa4, 650, 30);
        Round(1120, 112, 48, 40, 10, 0x1d1e23);
        brush->SetColor(D2D1::ColorF(0xc9c9d1));
        target->DrawLine(D2D1::Point2F(1137, 125), D2D1::Point2F(1151, 139), brush.Get(), 2);
        target->DrawLine(D2D1::Point2F(1151, 125), D2D1::Point2F(1137, 139), brush.Get(), 2);
        Round(120, 210, 480, 342, 16, 0x18191d);
        Text(Tr(T::ControllerView), 143, 225, 13, 0x9a9aa4, 220, 24, true);
        // Reuse Eden Qt's original Pro Controller contours and button coordinates.
        // Create paths once; fills/strokes/highlights share cached GPU resources.
        if (!controller_paths[0]) {
            using namespace ProControllerGeometry;
            ComPtr<ID2D1Factory> factory; target->GetFactory(&factory);
            auto polygon = [&](size_t index, const auto& points, bool mirror, bool symmetric) {
                winrt::check_hresult(factory->CreatePathGeometry(&controller_paths[index]));
                ComPtr<ID2D1GeometrySink> sink;
                winrt::check_hresult(controller_paths[index]->Open(&sink));
                auto point = [&](size_t i, bool reflected) {
                    const float x = points[i * 2] * (reflected ? -1.0f : 1.0f);
                    return D2D1::Point2F(350 + x, 390 + points[i * 2 + 1]);
                };
                sink->BeginFigure(point(0, mirror), D2D1_FIGURE_BEGIN_FILLED);
                for (size_t i = 1; i < points.size() / 2; ++i) sink->AddLine(point(i, mirror));
                if (symmetric)
                    for (size_t i = points.size() / 2; i-- > 0;) sink->AddLine(point(i, !mirror));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                winrt::check_hresult(sink->Close());
            };
            polygon(0, pro_body, false, true);
            polygon(1, pro_left_handle, false, false); polygon(2, pro_left_handle, true, false);
            polygon(3, pro_left_trigger, false, false); polygon(4, pro_left_trigger, true, false);
        }
        auto path = [&](size_t i, unsigned color) {
            brush->SetColor(D2D1::ColorF(color)); target->FillGeometry(controller_paths[i].Get(), brush.Get());
            brush->SetColor(D2D1::ColorF(0x55565f)); target->DrawGeometry(controller_paths[i].Get(), brush.Get(), 1.2f);
        };
        const size_t action = editor.selected;
        path(1, 0x26272d); path(2, 0x26272d); path(0, 0x1d1e23);
        path(3, action == 6 ? 0x8aff00 : 0x35363d); path(4, action == 7 ? 0x8aff00 : 0x35363d);
        auto circle = [&](float x, float y, float radius, const wchar_t* label, bool active, float font = 16) {
            const auto shape = D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius);
            brush->SetColor(D2D1::ColorF(active ? 0x8aff00U : 0x162124U)); target->FillEllipse(shape, brush.Get());
            brush->SetColor(D2D1::ColorF(active ? 0xe0fa85U : 0x62636dU)); target->DrawEllipse(shape, brush.Get(), 1.4f);
            Text(label, x - radius, y - radius, font, active ? 0x071012 : 0xe6e6eb,
                 radius * 2, radius * 2, true, false, true);
        };
        auto stick = [&](float x, float y, const wchar_t* label, bool active) {
            circle(x, y, 32, L"", active);
            circle(x, y, 22, label, false, 12);
        };
        stick(239, 335, L"L3", action == 4 || (action >= 20 && action < 24));
        stick(401, 390, L"R3", action == 5 || action >= 24);
        circle(486, 334, 15, L"A", action == 0); circle(455, 365, 15, L"B", action == 1);
        circle(455, 303, 15, L"X", action == 2); circle(424, 334, 15, L"Y", action == 3);
        Round(278, 359, 22, 62, 4, 0x131318); Round(258, 379, 62, 22, 4, 0x131318);
        for (size_t i = 12; i <= 15; ++i) {
            const auto tile = KeyboardDiagramTile(i);
            Round(tile.x, tile.y, tile.w, tile.h, 3, action == i ? 0x8aff00 : 0x33343b);
        }
        circle(300, 304, 10, L"-", action == 11, 14); circle(400, 304, 10, L"+", action == 10, 14);
        circle(379, 334, 10, L"", action == 18);
        brush->SetColor(D2D1::ColorF(action == 18 ? 0x071012U : 0xc9c9d1U));
        target->DrawLine(D2D1::Point2F(374, 334), D2D1::Point2F(379, 330), brush.Get(), 1.4f);
        target->DrawLine(D2D1::Point2F(379, 330), D2D1::Point2F(384, 334), brush.Get(), 1.4f);
        target->DrawRectangle(D2D1::RectF(376, 334, 382, 339), brush.Get(), 1.2f);
        Round(314, 327, 14, 14, 3, action == 19 ? 0x8aff00 : 0x131318);
        brush->SetColor(D2D1::ColorF(0x9a9aa4));
        target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(321, 334), 4, 4), brush.Get(), 1.2f);
        for (const auto i : {size_t{8}, size_t{9}}) {
            const auto tile = KeyboardDiagramTile(i);
            Round(tile.x, tile.y, tile.w, tile.h, 8, action == i ? 0x8aff00 : 0x26272d);
            Text(i == 8 ? L"ZL" : L"ZR", tile.x, tile.y, 14, action == i ? 0x071012 : 0xc9c9d1,
                 tile.w, tile.h, true, false, true);
        }
        Text(L"L", 220, 252, 14, action == 6 ? 0x071012 : 0xc9c9d1, 60, 22, true, false, true);
        Text(L"R", 450, 252, 14, action == 7 ? 0x071012 : 0xc9c9d1, 40, 22, true, false, true);
        Round(120, 564, 480, 67, 12, 0x1f280c);
        Text(Tr(T::SelectedBinding), 140, 574, 11, 0xa8b86a, 300, 19, true);
        Text(KeyboardActionName(action), 140, 594, 20, 0xf4f6ee, 285, 30, true);
        Round(436, 579, 142, 38, 8, 0x12160a);
        Text(editor.capturing ? std::wstring{Tr(T::PressAKey)} : KeyboardKeyName(editor.bindings[action]),
             436, 579, 16, 0x8aff00, 142, 38, true, false, true);
        const std::array<std::wstring_view, 3> pages{Tr(T::KeyPageButtons), Tr(T::KeyPageSystem), Tr(T::KeyPageSticks)};
        for (unsigned page = 0; page < pages.size(); ++page) {
            const float x = 635 + page * 174.0f;
            Round(x, 203, 166, 36, 9, page == editor.page ? 0x1a321e : 0x1b1c21);
            Text(pages[page], x, 203, 15, page == editor.page ? 0xd6f75a : 0xb0b0b9,
                 166, 36, true, false, true);
        }
        for (const size_t i : KeyboardPageActions(editor.page)) {
            const auto tile = KeyboardTileFor(i);
            Round(tile.x, tile.y, tile.w, tile.h, 9, i == action ? 0x222c0e : 0x1a1b20);
            Text(KeyboardActionName(i), tile.x + 14, tile.y, 15, 0xc9c9d1, 136, tile.h, false, false, true);
            Round(tile.x + 153, tile.y + 9, 86, 27, 6, 0x131318);
            Text(i == action && editor.capturing ? std::wstring{L"..."} : KeyboardKeyName(editor.bindings[i]),
                 tile.x + 153, tile.y + 9, 14, i == action ? 0x8aff00 : 0xb0b0b9, 86, 27, true, false, true);
        }
        Round(635, 603, 155, 36, 8, 0x1b1c21); Text(Tr(T::ClearBinding), 635, 603, 15, 0xc9c9d1, 155, 36, false, false, true);
        Round(806, 603, 170, 36, 8, 0x1b1c21); Text(Tr(T::RestoreDefaults), 806, 603, 15, 0xc9c9d1, 170, 36, false, false, true);
        if (!editor.notice.empty()) Text(editor.notice, 990, 609, 13, 0xa8b86a, 172, 30);
        Text(Tr(editor.capturing ? T::WaitingForKey : T::KeyboardHelp),
             120, 644, 13, 0x80808a, prompt_family == PromptFamily::Keyboard ? 1030.0f : 860.0f, 22);
        if (prompt_family != PromptFamily::Keyboard)
            ControlPrompt(Navigation::Back, Tr(T::Back), 1010, 637, 100);
    }
    ComPtr<ID2D1Bitmap1> LoadAssetBitmap(const std::filesystem::path& relative) {
        ComPtr<ID2D1Bitmap1> bitmap;
        if (asset_root.empty()) return bitmap;
        try {
            ComPtr<IWICBitmapDecoder> decoder;
            const auto path = asset_root / relative;
            winrt::check_hresult(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnDemand, &decoder));
            ComPtr<IWICBitmapFrameDecode> frame;
            winrt::check_hresult(decoder->GetFrame(0, &frame));
            ComPtr<IWICFormatConverter> converter;
            winrt::check_hresult(wic->CreateFormatConverter(&converter));
            winrt::check_hresult(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
            winrt::check_hresult(target->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap));
        } catch (const winrt::hresult_error&) {}
        return bitmap;
    }
    enum class PromptFamily : unsigned { Keyboard, Xbox, Nintendo };
    /// Space is Y on a pad; it has no packaged glyph and is drawn natively like AddFolder (X).
    /// The first five have packaged glyphs; the others are drawn natively (UiPainter::ButtonGlyph).
    enum class Navigation : unsigned { Play, Explore, Settings, Options, Back, AddFolder, Space, Refresh, Tabs };
    using ConfigurationIcon = UiIcon;
    void DrawConfiguration(const ConfigurationPanel& panel) {
        {
            DesignSpace space(target.Get());
            NsBackdrop(false);
            NsBrand();
        }
        const auto layout = GetConfigurationLayout(panel);
        if (panel.files) {
            Round(90, 80, 1100, 590, 24, 0x101b1e);
            Round(130, 108, 48, 48, 12, 0x1a321e);
            Icon(ConfigurationIcon::Folder, 138, 116, 0x8aff00);
            Text(Tr(T::FileManager), 194, 108, 32, 0xf4f7f5, 850, 50, true);
            Text(std::wstring{Tr(panel.status.keys_ready ? T::KeysReady : T::KeysMissing)} + L"  ·  " +
                 I18n::Format(T::FirmwareFiles, {std::to_wstring(panel.status.firmware_files)}),
                 130, 168, 18, 0x8aff00, 950);
            Text(Tr(T::FilesSubtitle), 130, 201, 16, 0xa6b5b7, 950);
        } else {
            Round(140, 100, 1000, 540, 24, 0x101b1e);
            PageHeader(180, 131, ConfigurationIcon::Settings, Tr(T::SettingsTitle), Tr(T::SettingsSubtitle));
        }
        const auto count = ConfigurationRowCount(panel);
        const auto first = ConfigurationFirstRow(panel);
        for (size_t i = first; i < std::min(first + 5, count); ++i) {
            const float y = layout.top + static_cast<float>(i - first) * layout.stride;
            const bool active = i == panel.selected;
            ConfigurationIcon icon;
            std::wstring title, hint;
            if (!panel.files) {
                icon = i == HomeFiles ? ConfigurationIcon::Folder :
                       i == HomeControllers ? ConfigurationIcon::Controller : ConfigurationIcon::Language;
                title = Tr(i == HomeFiles ? T::FileManager : i == HomeControllers ? T::ControllersAndKeyboard : T::System);
                hint = Tr(i == HomeFiles ? T::FileManagerHint : i == HomeControllers ? T::ControllersHint : T::SystemHint);
            } else {
                icon = i == 0 ? ConfigurationIcon::Folder : i == 1 ? ConfigurationIcon::Key :
                       i == 2 ? ConfigurationIcon::Firmware : i < 5 ? ConfigurationIcon::Folder : ConfigurationIcon::Remove;
                if (i == 0) title = Tr(T::AddGamesFolder);
                else if (i == 1) title = Tr(T::ImportKeys);
                else if (i == 2) title = Tr(T::ImportFirmware);
                else if (i == 3) title = Tr(T::ContentUpdates);
                else if (i == 4) title = Tr(T::ContentDlc);
                else title = I18n::Format(T::RemoveFolder, {panel.status.sources[i - 5].name});
            }
            RowFrame(layout.x, y, layout.width, layout.row_height, active, icon);
            Text(title, layout.x + 72, y + (panel.files ? 10.0f : 12.0f), panel.files ? 20.0f : 22.0f,
                 0xf4f7f5, layout.width - 135, 32, !panel.files);
            if (!hint.empty()) Text(hint, layout.x + 72, y + 44, 16, 0xbcbcc5, layout.width - 135, 26);
            Chevron(layout.x + layout.width - 28, y + layout.row_height / 2, active ? 0x8aff00 : 0xa6b5b7);
        }
        ScrollIndicator(layout.x + layout.width + 14, layout.top,
                        5 * layout.stride - (layout.stride - layout.row_height), first,
                        5, count);
        if (!panel.files) {
            Text(Tr(T::EmulatorData), 180, 511, 12, 0xa6b5b7, 190, 26, true);
            Text(Tr(panel.status.keys_ready ? T::KeysReady : T::KeysMissing), 380, 506, 16,
                 panel.status.keys_ready ? 0x8aff00 : 0xf3ba6a, 260);
            Text(I18n::Format(T::FirmwareFiles, {std::to_wstring(panel.status.firmware_files)}),
                 680, 506, 16, 0xa6b5b7, 350);
        }
        if (panel.busy) Text(I18n::Format(T::Importing, {std::to_wstring(panel.completed), std::to_wstring(panel.total)}),
                             130, 565, 18, 0x8aff00, 990);
        else Text(panel.notice, layout.x + 4, panel.files ? 561.0f : 538.0f, 16, 0xf3ba6a,
                  layout.width - 20, panel.files ? 40.0f : 28.0f, false, true);
        ControlPrompt(Navigation::Explore, Tr(T::Select), layout.x + 4, layout.footer, 110);
        ControlPrompt(Navigation::Play, Tr(T::Open), layout.x + 380, layout.footer, 110);
        ControlPrompt(Navigation::Back, Tr(panel.busy ? T::Cancel : T::Back),
                      layout.x + layout.width - 230, layout.footer, 150);
    }
    void DrawSystemPage(const SystemPage& page) {
        using L = SystemPageLayout;
        {
            DesignSpace space(target.Get());
            NsBackdrop(false);
            NsBrand();
        }
        Round(L::PanelX, L::PanelY, L::PanelWidth, L::PanelHeight, 24, 0x101b1e);
        PageHeader(L::RowX, 99, ConfigurationIcon::Language, Tr(T::SystemTitle), Tr(T::SystemSubtitle));
        static constexpr std::array<ConfigurationIcon, SystemPage::RowCount> icons{
            ConfigurationIcon::User, ConfigurationIcon::Language, ConfigurationIcon::Controller,
            ConfigurationIcon::Region, ConfigurationIcon::Mods, ConfigurationIcon::Settings, ConfigurationIcon::Archive,
            ConfigurationIcon::Cache};
        for (size_t i = 0; i < SystemPage::RowCount; ++i) {
            const auto row = static_cast<SystemPage::Row>(i);
            const float y = L::RowTop + static_cast<float>(i) * L::RowStride;
            const bool active = i == page.selected;
            RowFrame(L::RowX, y, L::RowWidth, L::RowHeight, active, icons[i]);
            Text(Tr(SystemRowTitle(row)), L::RowX + 72, y + 4, 20, 0xf4f7f5, 420, 30, true);
            Text(Tr(SystemRowHint(row)), L::RowX + 72, y + 28, 15, 0xbcbcc5, 440, 22);
            const unsigned accent = active ? 0x8aff00 : 0xa6b5b7;
            const float right = L::RowX + L::RowWidth - 28;
            const float middle = y + L::RowHeight / 2;
            const bool choice = row == SystemPage::InterfaceLanguage || row == SystemPage::GameLanguage ||
                                row == SystemPage::Region || row == SystemPage::AdultMods || row == SystemPage::PerformanceStats ||
                                row == SystemPage::PulseSessions;
            if (choice) {
                // "< value >": arrows say left/right changes it, as in the controller panel.
                Chevron(L::RowX + 486, middle, accent, true);
                Chevron(right, middle, accent);
            } else {
                Chevron(right, middle, accent);
            }
            const auto value = SystemRowValue(page, row);
            if (!value.empty())
                Text(value, L::RowX + 500, y + (L::RowHeight - 30) / 2, 19, active ? 0xf4f7f5 : 0xdadae0,
                     right - 22 - (L::RowX + 500), 30, active, false, false, true);
        }
        if (!page.notice.empty()) Text(page.notice, L::RowX + 4, L::Notice, 16, 0x8aff00, L::RowWidth - 20, 28);
        ControlPrompt(Navigation::Explore, Tr(T::Select), L::RowX + 4, L::Footer, 110);
        ControlPrompt(Navigation::Play, Tr(T::Change), L::RowX + 380, L::Footer, 110);
        ControlPrompt(Navigation::Back, Tr(T::Back), L::RowX + L::RowWidth - 230, L::Footer, 150);
    }
    void DrawNameEditor(const SystemPage& page) {
        using L = SystemPageLayout;
        using K = TextEntryPanelLayout;
        brush->SetColor(D2D1::ColorF(0, 0, 0, 0.86f));
        target->FillRectangle(D2D1::RectF(0, 0, 1280, 720), brush.Get());
        Round(L::EditorX, L::EditorY, K::Width, K::Height, 22, 0x121216);
        DrawTextEntryPanel(*this, {*page.name_editor, Tr(T::UserName), Tr(T::NameEditorHint), page.editor_notice},
                           L::EditorX, L::EditorY);
        const float left = L::EditorX + K::Left;
        const float footer = L::EditorY + K::Footer;
        if (prompt_family == PromptFamily::Keyboard) {
            Text(Tr(T::NameEditorKeyboardHelp), left, footer + 6, 15, 0x80808a, K::BoxWidth, 24);
        } else {
            ControlPrompt(Navigation::Play, Tr(T::TypeKey), left, footer, 140);
            ControlPrompt(Navigation::AddFolder, Tr(T::DeleteKey), left + 190, footer, 140);
            ControlPrompt(Navigation::Space, Tr(T::SpaceKey), left + 380, footer, 140);
            ControlPrompt(Navigation::Options, Tr(T::DoneKey), left + 570, footer, 140);
            ControlPrompt(Navigation::Back, Tr(T::Cancel), left + 760, footer, 140);
        }
    }
    void ControlPrompt(Navigation action, std::wstring_view label, float x, float y,
                       float width, unsigned color = 0xa6b5b7) {
        // An empty fallback means "use the translated name".
        struct Glyph { const wchar_t* asset; std::wstring_view fallback; T name; };
        static constexpr std::array<Glyph, 5> keyboard{{
            {L"keyboard_enter", L"Enter", {}}, {L"keyboard_arrows", {}, T::Arrows},
            {L"keyboard_f1", L"F1", {}}, {L"keyboard_f2", L"F2", {}}, {L"keyboard_escape", L"Esc", {}}}};
        static constexpr std::array<Glyph, 5> xbox{{
            {L"xbox_a", L"A", {}}, {L"xbox_dpad", {}, T::DPad}, {L"xbox_view", L"View", {}},
            {L"xbox_menu", L"Menu", {}}, {L"xbox_b", L"B", {}}}};
        const auto index = static_cast<unsigned>(action);
        if (prompt_family == PromptFamily::Keyboard &&
            (action == Navigation::AddFolder || action == Navigation::Refresh)) {
            const bool add = action == Navigation::AddFolder;
            AssetPrompt(add ? L"keyboard_o" : L"keyboard_r", label, add ? L"O" : L"R", x, y, width, color);
            return;
        }
        if (action >= Navigation::AddFolder) {
            // Remaining buttons use native glyphs when no packaged image is available.
            const bool pad = prompt_family != PromptFamily::Keyboard;
            const bool colored = prompt_family == PromptFamily::Xbox;
            const auto key = [&](std::wstring_view letter, float left) {
                Round(left, y, 32, 32, 6, 0x162124);
                Text(letter, left, y, 18, 0xf4f7f5, 32, 32, true, false, true);
            };
            float glyphs = 40;
            switch (action) {
            case Navigation::AddFolder:
                if (pad) ButtonGlyph(swap_prompt_faces ? L"Y" : L"X", x, y, colored ? (swap_prompt_faces ? ButtonColor::Y : ButtonColor::X) : ButtonColor::Neutral);
                else key(L"O", x);
                break;
            case Navigation::Space:
            case Navigation::Refresh:
                if (pad) ButtonGlyph(swap_prompt_faces ? L"X" : L"Y", x, y, colored ? (swap_prompt_faces ? ButtonColor::X : ButtonColor::Y) : ButtonColor::Neutral);
                else key(action == Navigation::Space ? L"Y" : L"R", x);
                break;
            default: // Tabs
                if (pad) {
                    const bool nintendo = prompt_family == PromptFamily::Nintendo;
                    ButtonGlyph(nintendo ? L"L" : L"LB", x, y, ButtonColor::Neutral);
                    ButtonGlyph(nintendo ? L"R" : L"RB", x + 44, y, ButtonColor::Neutral);
                } else {
                    key(L"Q", x);
                    key(L"E", x + 38);
                }
                glyphs = pad ? 90.0f : 78.0f;
                break;
            }
            Text(label, x + glyphs, y + 3, 19, color, width);
            return;
        }
        if (prompt_family != PromptFamily::Keyboard && action == Navigation::Explore) {
            // Neutral monochrome d-pad for both controller families. The old
            // bitmap was Kenney's red color variant, not an application state.
            Round(x + 11, y + 3, 10, 26, 2, 0xf4f7f5);
            Round(x + 3, y + 11, 26, 10, 2, 0xf4f7f5);
            brush->SetColor(D2D1::ColorF(0xf4f7f5));
            target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 56, y + 16), 13, 13), brush.Get(), 2);
            Text(L"L", x + 40, y, 16, 0xf4f7f5, 32, 32, true, false, true);
            Text(label, x + 80, y + 3, 19, color, width);
            return;
        }
        if (prompt_family == PromptFamily::Nintendo) {
            // Native D2D glyphs match Nintendo's monochrome A/B and minus/plus.
            // They reuse cached text formats and allocate no bitmap or path.
            brush->SetColor(D2D1::ColorF(0xf4f7f5));
            const auto center = D2D1::Point2F(x + 16, y + 16);
            if (action == Navigation::Play || action == Navigation::Back) {
                target->DrawEllipse(D2D1::Ellipse(center, 13, 13), brush.Get(), 2);
                Text(action == Navigation::Play ? L"A" : L"B", x, y, 18, 0xf4f7f5, 32, 32, true, false, true);
            } else {
                target->DrawLine(D2D1::Point2F(x + 7, y + 16), D2D1::Point2F(x + 25, y + 16), brush.Get(), 3);
                if (action == Navigation::Options)
                    target->DrawLine(D2D1::Point2F(x + 16, y + 7), D2D1::Point2F(x + 16, y + 25), brush.Get(), 3);
            }
            Text(label, x + 40, y + 3, 19, color, width);
            return;
        }
        const auto physical_index = swap_prompt_faces && action == Navigation::Play
            ? static_cast<unsigned>(Navigation::Back)
            : swap_prompt_faces && action == Navigation::Back
                ? static_cast<unsigned>(Navigation::Play) : index;
        const auto& glyph = prompt_family == PromptFamily::Keyboard ? keyboard[index] : xbox[physical_index];
        AssetPrompt(glyph.asset, label, glyph.fallback.empty() ? Tr(glyph.name) : glyph.fallback, x, y, width, color);
    }
    /// The game's options, and the option page open over them.
    void DrawGameOptions(const GameOptionsView& view) {
        if (view.content) { Content::DrawContentPanel(*this, *view.content, view.game ? view.game->name : Tr(T::ContentOverview)); return; }
        const LibraryEntry& game = *view.game;
        Round(0, 0, 1280, 720, 0, Palette::Backdrop);
        if (view.page.page == GameOption::Cheats && view.cheats) {
            using L = Cheats::CheatsLayout;
            constexpr float x = (1280 - L::Width) / 2, y = 30;
            Cheats::DrawCheatsPanel(*this, {*view.cheats, view.cheats_page, game.name, view.notice, false}, x, y);
            const float footer = y + L::Footer;
            ControlPrompt(Navigation::Play, Tr(T::ToggleCheat), x + L::Margin, footer, 120);
            ControlPrompt(Navigation::AddFolder, Tr(T::AllCheats), x + L::Margin + 170, footer, 110);
            float next = x + L::Margin + 330;
            if (Cheats::CheatTabCount(view.cheats->Catalog()) > 1) {
                ControlPrompt(Navigation::Tabs, Tr(T::Category), next, footer, 140);
                next += 250;
            }
            ControlPrompt(Navigation::Refresh, Tr(T::Refresh), next, footer, 130);
            ControlPrompt(Navigation::Back, Tr(T::Back), x + L::Width - L::Margin - 150, footer, 120);
            return;
        }
        if (view.page.page == GameOption::Mods && view.mods && view.mods_page) {
            using L = Mods::ModsLayout;
            using Button = Mods::ModsPrompt::Button;
            constexpr float x = (1280 - L::Width) / 2, y = 30;
            Mods::DrawModsPanel(*this, {*view.mods, *view.mods_page, game.name, view.notice}, x, y);
            const float footer = y + L::Footer;
            float next = x + L::Margin;
            for (const auto& prompt : Mods::ModsPrompts(*view.mods, *view.mods_page)) {
                const auto label = Tr(prompt.label);
                // B, the way out, always sits at the right end.
                if (prompt.button == Button::B) {
                    ControlPrompt(Navigation::Back, label, x + L::Width - L::Margin - 170, footer, 140);
                    continue;
                }
                const Navigation action = prompt.button == Button::A      ? Navigation::Play
                                          : prompt.button == Button::X    ? Navigation::AddFolder
                                          : prompt.button == Button::Y    ? Navigation::Refresh
                                          : prompt.button == Button::Menu ? Navigation::Options
                                                                          : Navigation::Tabs;
                const float width = MeasureText(label, 19, false) + 8;
                ControlPrompt(action, label, next, footer, width);
                next += width + (action == Navigation::Tabs ? 120 : 64);
            }
            return;
        }
        DrawNsGameOptions(view, game);
    }
    void AssetPrompt(const wchar_t* asset, std::wstring_view label, std::wstring_view fallback,
                float x, float y, float width, unsigned color = 0xa6b5b7) {
        auto found = prompts.find(asset);
        if (found == prompts.end()) {
            auto bitmap = LoadAssetBitmap(std::filesystem::path{L"InputPrompts"} /
                                          (std::wstring{asset} + L".png"));
            found = prompts.emplace(asset, std::move(bitmap)).first;
        }
        if (found->second) {
            target->DrawBitmap(found->second.Get(), D2D1::RectF(x, y, x + 32, y + 32), 1,
                               D2D1_INTERPOLATION_MODE_LINEAR);
            Text(label, x + 40, y + 3, 19, color, width);
        } else {
            Text(std::wstring{fallback} + L"  " + std::wstring{label}, x, y + 3, 19, color, width + 40);
        }
    }
    /// The entry's decoded cover, or null while it has none (the grid draws the fallback art).
    ID2D1Bitmap1* CoverBitmap(const LibraryEntry& entry) {
        // The launch path is unique; relative paths repeat across internal and USB sources.
        auto found = covers.find(entry.launch_path);
        if (found == covers.end() && !entry.icon.empty()) {
            ComPtr<ID2D1Bitmap1> bitmap;
            try {
                ComPtr<IWICStream> stream;
                winrt::check_hresult(wic->CreateStream(&stream));
                winrt::check_hresult(stream->InitializeFromMemory(const_cast<BYTE*>(entry.icon.data()),
                                                                 static_cast<DWORD>(entry.icon.size())));
                ComPtr<IWICBitmapDecoder> decoder;
                winrt::check_hresult(wic->CreateDecoderFromStream(stream.Get(), nullptr,
                    WICDecodeMetadataCacheOnDemand, &decoder));
                ComPtr<IWICBitmapFrameDecode> frame;
                winrt::check_hresult(decoder->GetFrame(0, &frame));
                UINT w, h;
                winrt::check_hresult(frame->GetSize(&w, &h));
                if (w == 0 || h == 0 || w > 4096 || h > 4096)
                    winrt::throw_hresult(E_INVALIDARG);
                ComPtr<IWICBitmapScaler> scaler;
                winrt::check_hresult(wic->CreateBitmapScaler(&scaler));
                winrt::check_hresult(scaler->Initialize(frame.Get(), 256, 256, WICBitmapInterpolationModeFant));
                ComPtr<IWICFormatConverter> converter;
                winrt::check_hresult(wic->CreateFormatConverter(&converter));
                winrt::check_hresult(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
                winrt::check_hresult(target->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap));
            } catch (const winrt::hresult_error&) {}
            // Three pages of 256x256 BGRA covers: the visible one and both neighbours, 256 KiB each.
            if (covers.size() >= MaxCovers) covers.clear();
            found = covers.emplace(entry.launch_path, std::move(bitmap)).first;
        }
        return found != covers.end() ? found->second.Get() : nullptr;
    }
    void Cover(const LibraryEntry& entry, float x, float y, float size) {
        Round(x, y, size, size, 12, 0x28292f);
        if (ID2D1Bitmap1* const art = CoverBitmap(entry)) {
            // Geometries are cached by size; transform places the rounded mask
            // without reallocating it on each selection or focus redraw.
            auto& mask = masks[static_cast<unsigned>(size)];
            if (!mask) {
                ComPtr<ID2D1Factory> factory;
                target->GetFactory(&factory);
                winrt::check_hresult(factory->CreateRoundedRectangleGeometry(
                    D2D1::RoundedRect(D2D1::RectF(0, 0, size, size), 12, 12), &mask));
            }
            D2D1_LAYER_PARAMETERS1 layer{};
            layer.contentBounds = D2D1::RectF(x, y, x + size, y + size);
            layer.geometricMask = mask.Get();
            layer.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
            layer.maskTransform = D2D1::Matrix3x2F::Translation(x, y);
            layer.opacity = 1;
            target->PushLayer(&layer, nullptr);
            target->DrawBitmap(art, D2D1::RectF(x, y, x + size, y + size), 1,
                               D2D1_INTERPOLATION_MODE_LINEAR);
            target->PopLayer();
        } else {
            Text(L"NS HUEHUE", x + size * 0.12f, y + size * 0.32f, size > 150 ? 34.0f : 20.0f,
                 0x8aff00, size * 0.85f, 54, true);
            Text(entry.relative_path.extension().wstring().substr(1), x + size * 0.12f,
                 y + size * 0.58f, 16, 0xa6b5b7, size * 0.85f);
        }
    }
    /// The mods store's pictures (UiPainter::Picture): decoded once, at most 640 px wide.
    bool Picture(const std::filesystem::path& file, float x, float y, float w, float h) override {
        if (file.empty() || w < 1 || h < 1) return false;
        auto found = pictures.find(file.native());
        if (found == pictures.end()) {
            if (pictures.size() >= MaxPictures) pictures.clear();
            found = pictures.emplace(file.native(), DecodePicture(file, 640)).first;
        }
        const auto& bitmap = found->second;
        if (!bitmap) return false;
        // Cropped around the middle to the rectangle's shape.
        const auto size = bitmap->GetSize();
        float crop_w = size.width, crop_h = size.height;
        if (crop_w / crop_h > w / h) crop_w = crop_h * w / h;
        else crop_h = crop_w * h / w;
        const auto source = D2D1::RectF((size.width - crop_w) / 2, (size.height - crop_h) / 2,
                                        (size.width + crop_w) / 2, (size.height + crop_h) / 2);
        auto& mask = picture_masks[(static_cast<unsigned>(w) << 16) | static_cast<unsigned>(h)];
        if (!mask) {
            ComPtr<ID2D1Factory> factory;
            target->GetFactory(&factory);
            if (FAILED(factory->CreateRoundedRectangleGeometry(
                    D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), 12, 12), &mask)))
                return false;
        }
        D2D1_LAYER_PARAMETERS1 layer{};
        layer.contentBounds = D2D1::RectF(x, y, x + w, y + h);
        layer.geometricMask = mask.Get();
        layer.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
        layer.maskTransform = D2D1::Matrix3x2F::Translation(x, y);
        layer.opacity = 1;
        target->PushLayer(&layer, nullptr);
        target->DrawBitmap(bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), 1, D2D1_INTERPOLATION_MODE_LINEAR,
                           &source);
        target->PopLayer();
        return true;
    }
    ComPtr<ID2D1Bitmap1> DecodePicture(const std::filesystem::path& file, unsigned max_width) {
        ComPtr<ID2D1Bitmap1> bitmap;
        try {
            ComPtr<IWICBitmapDecoder> decoder;
            winrt::check_hresult(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnDemand, &decoder));
            ComPtr<IWICBitmapFrameDecode> frame;
            winrt::check_hresult(decoder->GetFrame(0, &frame));
            UINT w, h;
            winrt::check_hresult(frame->GetSize(&w, &h));
            if (w == 0 || h == 0 || w > 8192 || h > 8192) winrt::throw_hresult(E_INVALIDARG);
            const UINT width = std::min(w, max_width);
            const UINT height = std::max(1U, static_cast<UINT>(static_cast<unsigned long long>(h) * width / w));
            ComPtr<IWICBitmapScaler> scaler;
            winrt::check_hresult(wic->CreateBitmapScaler(&scaler));
            winrt::check_hresult(scaler->Initialize(frame.Get(), width, height, WICBitmapInterpolationModeFant));
            ComPtr<IWICFormatConverter> converter;
            winrt::check_hresult(wic->CreateFormatConverter(&converter));
            winrt::check_hresult(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
            winrt::check_hresult(target->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap));
        } catch (const winrt::hresult_error&) {
            bitmap.Reset(); // Not a picture WIC reads: the placeholder stays.
        }
        return bitmap;
    }
    static constexpr size_t MaxCovers = 32;
    /// Two screens of the grid and a mod's gallery: 640 px wide at most, ~0.6 MiB each.
    static constexpr size_t MaxPictures = 48;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<ID2D1DeviceContext> target;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1Bitmap1> wall, ns_background, ns_logo, ns_mascot, ns_fallback;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap1>> ns_icons;
    ComPtr<IDWriteTextFormat> graffiti_format;
    std::wstring player_name; ///< The System page's name for the header chip (empty: "Jogador 1").
    std::wstring version_string;
    std::array<ComPtr<ID2D1PathGeometry>, 5> controller_paths;
    PromptFamily prompt_family{PromptFamily::Keyboard};
    bool swap_prompt_faces{};
    std::filesystem::path asset_root;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap1>> prompts;
    std::unordered_map<unsigned, ComPtr<ID2D1RoundedRectangleGeometry>> masks;
    ComPtr<IDWriteTextLayout> version_layout;
    std::unordered_map<std::string, ComPtr<ID2D1Bitmap1>> covers;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap1>> pictures;
    std::unordered_map<unsigned, ComPtr<ID2D1RoundedRectangleGeometry>> picture_masks;
};
LibraryCanvas::LibraryCanvas(void* window, unsigned width, unsigned height)
    : impl{std::make_unique<Impl>(window, width, height)} {}
LibraryCanvas::~LibraryCanvas() = default;
void LibraryCanvas::InvalidateCovers() { impl->InvalidateCovers(); }
void LibraryCanvas::Draw(const LibraryView& view) { impl->Draw(view); }
} // namespace EdenXbox
