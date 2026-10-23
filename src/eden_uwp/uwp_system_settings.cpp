// SPDX-FileCopyrightText: Copyright 2026 JulianDr14
// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden_uwp/uwp_system_settings.h"
#include "eden_uwp/theme.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.UserProfile.h>
#include "common/logging.h"
#include "common/settings.h"
#include "video_core/performance_overlay.h"
#include "core/hle/service/acc/profile_manager.h"

namespace EdenXbox {
namespace {
constexpr auto UiLanguageKey = L"system_ui_language";
constexpr auto GameLanguageKey = L"system_game_language";
constexpr auto RegionKey = L"system_region";
constexpr auto UserNameKey = L"system_user_name";
constexpr auto PerformanceStatsKey = L"system_show_performance_stats";
constexpr auto AdultModsKey = L"system_show_adult_mods";
constexpr auto PulseSessionsKey = L"system_pulse_sessions";
constexpr auto ThemeKeyName = L"system_theme_id";
constexpr auto WallpaperKeyName = L"system_wallpaper_id";
constexpr auto UpscalingKey = L"system_upscaling";
constexpr auto VolumeKey = L"system_volume";
constexpr auto AirplaneKey = L"system_airplane";

/// Saved as the enum's value, or -1 for "follow the system". A value that is not a selectable
/// option (an older or newer build's) also falls back to the system.
template <typename T>
std::optional<T> LoadChoice(const winrt::Windows::Foundation::Collections::IPropertySet& values,
                            const wchar_t* key, std::span<const T> options) {
    if (!values.HasKey(key)) return std::nullopt;
    const auto raw = winrt::unbox_value<int32_t>(values.Lookup(key));
    for (const T option : options)
        if (static_cast<int32_t>(option) == raw) return option;
    return std::nullopt;
}
template <typename T>
int32_t ChoiceValue(std::optional<T> choice) {
    return choice ? static_cast<int32_t>(*choice) : -1;
}
} // namespace

SystemSettings LoadSystemSettings() {
    SystemSettings settings;
    try {
        const auto values = winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values();
        if (values.HasKey(UiLanguageKey)) {
            settings.ui_language = winrt::unbox_value<winrt::hstring>(values.Lookup(UiLanguageKey)).c_str();
            if (!I18n::FindUiLanguage(settings.ui_language)) settings.ui_language.clear();
        }
        settings.game_language = LoadChoice(values, GameLanguageKey, SelectableGameLanguages());
        settings.region = LoadChoice(values, RegionKey, SelectableRegions());
        if (values.HasKey(UserNameKey))
            settings.user_name = SanitizeUserName(
                winrt::unbox_value<winrt::hstring>(values.Lookup(UserNameKey)).c_str());
        if (values.HasKey(PerformanceStatsKey)) settings.show_performance_stats = winrt::unbox_value<bool>(values.Lookup(PerformanceStatsKey));
        if (values.HasKey(AdultModsKey)) settings.show_adult_mods = winrt::unbox_value<bool>(values.Lookup(AdultModsKey));
        if (values.HasKey(PulseSessionsKey)) settings.pulse_sessions = winrt::unbox_value<bool>(values.Lookup(PulseSessionsKey));
        // An unknown or broken id keeps the graffiti theme (the default of existing installs).
        if (values.HasKey(ThemeKeyName)) {
            const auto key = winrt::unbox_value_or<winrt::hstring>(values.Lookup(ThemeKeyName), L"");
            if (const auto id = ParseTheme(key)) settings.theme = static_cast<std::uint8_t>(*id);
            else if (!key.empty() && key.size() <= 64) settings.theme_pack = key.c_str(); // checked against the catalog
        }
        if (values.HasKey(WallpaperKeyName)) {
            if (const auto id = ParseWallpaper(winrt::unbox_value_or<winrt::hstring>(values.Lookup(WallpaperKeyName), L"")))
                settings.wallpaper = static_cast<std::uint8_t>(*id);
        }
        if (values.HasKey(VolumeKey))
            settings.volume = static_cast<std::uint8_t>(std::clamp(winrt::unbox_value_or<int32_t>(values.Lookup(VolumeKey), 100), 0, 200));
        if (values.HasKey(AirplaneKey)) settings.airplane = winrt::unbox_value_or<bool>(values.Lookup(AirplaneKey), false);
        if (values.HasKey(UpscalingKey))
            settings.upscaling = UpscalingFromValue(winrt::unbox_value<int32_t>(values.Lookup(UpscalingKey)))
                                     .value_or(Upscaling::Bilinear);
    } catch (...) { /* Missing or invalid values keep following the system. */ }
    return settings;
}

bool SaveSystemSettings(const SystemSettings& settings) {
    try {
        const auto values = winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values();
        values.Insert(UiLanguageKey, winrt::box_value(winrt::hstring{settings.ui_language}));
        values.Insert(GameLanguageKey, winrt::box_value(ChoiceValue(settings.game_language)));
        values.Insert(RegionKey, winrt::box_value(ChoiceValue(settings.region)));
        values.Insert(UserNameKey, winrt::box_value(winrt::hstring{SanitizeUserName(settings.user_name)}));
        values.Insert(PerformanceStatsKey, winrt::box_value(settings.show_performance_stats));
        values.Insert(AdultModsKey, winrt::box_value(settings.show_adult_mods));
        values.Insert(PulseSessionsKey, winrt::box_value(settings.pulse_sessions));
        values.Insert(ThemeKeyName, winrt::box_value(settings.theme_pack.empty()
                                                         ? winrt::hstring{ThemeKey(static_cast<ThemeId>(std::min<int>(settings.theme, 2)))}
                                                         : winrt::hstring{settings.theme_pack}));
        values.Insert(WallpaperKeyName, winrt::box_value(winrt::hstring{WallpaperKey(settings.wallpaper ? WallpaperId::Aurora : WallpaperId::Solid)}));
        values.Insert(UpscalingKey, winrt::box_value(static_cast<int32_t>(settings.upscaling)));
        values.Insert(VolumeKey, winrt::box_value(static_cast<int32_t>(settings.volume)));
        values.Insert(AirplaneKey, winrt::box_value(settings.airplane));
        return true;
    } catch (...) { return false; }
}

SystemLocale DetectSystemLocale() {
    SystemLocale locale;
    using winrt::Windows::System::UserProfile::GlobalizationPreferences;
    try {
        for (const auto& language : GlobalizationPreferences::Languages())
            locale.languages.emplace_back(language.c_str());
    } catch (const winrt::hresult_error&) {}
    try {
        locale.region = GlobalizationPreferences::HomeGeographicRegion().c_str();
    } catch (const winrt::hresult_error&) {}
    return locale;
}

ResolvedSystem ApplySystemSettings() {
    const auto locale = DetectSystemLocale();
    const auto settings = LoadSystemSettings();
    VideoCore::performance_overlay_enabled.store(settings.show_performance_stats, std::memory_order_relaxed);
    ApplyUpscaling(settings.upscaling);
    const auto resolved = ResolveSystem(settings, locale);
    I18n::SetUiLanguage(*resolved.ui_language);
    Settings::values.language_index = resolved.game_language;
    Settings::values.region_index = resolved.region;
    Settings::values.volume.SetValue(settings.volume);
    Settings::values.airplane_mode.SetValue(settings.airplane);
    LOG_INFO(Frontend, "System: Windows language {}, region {}; interface {}, game language {}, region {}",
             locale.languages.empty() ? std::string{"(none)"} : I18n::ToUtf8(locale.languages.front()),
             locale.region.empty() ? std::string{"(none)"} : I18n::ToUtf8(locale.region),
             I18n::ToUtf8(resolved.ui_language->tag), Settings::CanonicalizeEnum(resolved.game_language),
             Settings::CanonicalizeEnum(resolved.region));
    return resolved;
}

void ApplyUpscaling(Upscaling upscaling) {
    const UpscalingSettings values = ToSettings(upscaling);
    Settings::values.scaling_filter.SetValue(values.filter);
    Settings::values.fsr_sharpening_slider.SetValue(values.fsr_sharpening_slider);
}

bool ApplyUserName(Service::Account::ProfileManager& profiles, std::wstring_view name) {
    // No saved name is the default one: a profile renamed before goes back to it.
    auto clean = SanitizeUserName(name);
    if (clean.empty()) clean = DefaultUserName;
    const auto index = static_cast<std::size_t>(std::clamp<s32>(
        Settings::values.current_user.GetValue(), 0, static_cast<s32>(Service::Account::MAX_USERS) - 1));
    const auto uuid = profiles.GetUser(index);
    Service::Account::ProfileBase base{};
    if (!uuid || !profiles.GetProfileBase(*uuid, base)) return false;
    // Ten UTF-16 units are at most 30 UTF-8 bytes, inside the 32-byte field with room to spare.
    const auto utf8 = I18n::ToUtf8(clean);
    Service::Account::ProfileUsername username{};
    std::copy_n(utf8.begin(), std::min(utf8.size(), username.size()), username.begin());
    if (base.username == username) return true;
    base.username = username;
    const bool saved = profiles.SetProfileBase(*uuid, base);
    LOG_INFO(Frontend, "System: profile {} renamed to '{}'{}", index, utf8, saved ? "" : " (failed)");
    return saved;
}
} // namespace EdenXbox
