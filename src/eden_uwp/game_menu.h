// SPDX-FileCopyrightText: Copyright 2026 JulianDr14
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "eden_uwp/button_remap.h"
#include "eden_uwp/controller_style.h"
#include "eden_uwp/ui_actions.h"
#include "eden_uwp/localization.h"
#include "eden_uwp/ui_text.h"
#include "eden_uwp/upscaling.h"

namespace EdenXbox {
// In-game menu: View + Menu held for two seconds (Escape on a keyboard) pauses the game and opens
// it (the player asked for 2 s, 19/10/2026: long enough that no game press opens it by accident).
// The guide button belongs to the system, so the shortcut has to come from game buttons: Switch -
// and +, which almost no game holds together.

/// Holds back a lone + or - for a moment, so pressing View and Menu together never reaches the
/// game as a pause press; both held for HoldMs fire Toggle. A lone press is delivered after
/// PendingMs (or at once on release, so taps still arrive).
class MenuComboFilter {
public:
    static constexpr std::uint32_t PendingMs = 100;
    static constexpr std::uint32_t HoldMs = 2000;

    /// Filters this poll's + / - and returns true when the combo fires.
    bool Filter(bool& plus, bool& minus, std::uint32_t now_ms) {
        const bool any = plus || minus;
        if (plus && minus && state != State::Combo) {
            state = State::Combo;
            if (pending_since == NotPending) pending_since = now_ms;
            fired = false;
        }
        switch (state) {
        case State::Idle:
            if (!any) return false;
            state = State::Pending;
            pending_since = now_ms;
            held_plus = plus;
            held_minus = minus;
            plus = minus = false;
            return false;
        case State::Pending:
            if (!any) {
                // Released before the window closed: a tap, delivered now and released next poll.
                state = State::Idle;
                pending_since = NotPending;
                plus = held_plus;
                minus = held_minus;
                return false;
            }
            held_plus = plus;
            held_minus = minus;
            if (now_ms - pending_since < PendingMs) {
                plus = minus = false;
                return false;
            }
            state = State::Passing;
            pending_since = NotPending;
            return false;
        case State::Passing:
            if (!any) state = State::Idle;
            return false;
        case State::Combo: {
            const bool fire = !fired && plus && minus && now_ms - pending_since >= HoldMs;
            if (fire) fired = true;
            if (!any) {
                state = State::Idle;
                pending_since = NotPending;
            }
            plus = minus = false;
            return fire;
        }
        }
        return false;
    }

private:
    static constexpr std::uint32_t NotPending = ~0U;
    enum class State : std::uint8_t { Idle, Pending, Passing, Combo };
    State state{State::Idle};
    std::uint32_t pending_since{NotPending};
    bool held_plus{}, held_minus{}, fired{};
};

struct GameMenuSettings {
    ConsoleControllerStyle style{ConsoleControllerStyle::Auto};
    bool swap_face_buttons{};
    float deadzone{0.12f};
    /// PC only: the window can switch to full screen from the menu (the Series always is).
    bool can_fullscreen{};
    bool fullscreen{};
    bool show_performance_stats{};
    /// Pulse sessions are on: leaving for home keeps the game where it is, unless the player
    /// turns that off on the row (pulse_sessions.h).
    bool pulse_sessions{};
    /// Shown live: the paused frame is presented again with each change.
    Upscaling upscaling{Upscaling::Bilinear};
    /// The row is offered (off in NS HUEHUE: the choice is per game, in Desempenho e graficos).
    bool offer_upscaling{};
    /// Opt-in diagnostic package: capture from a controller without a keyboard.
    bool material_capture{};
    /// Quick settings (NS HUEHUE): applied at once to this session, not saved.
    bool speed_limit{true};
    std::uint8_t volume{100}; ///< Percent, in steps of 10, up to 200.
    /// Percent of the console's speed, in steps of 10 (10 to 200); 0: no limit.
    std::uint16_t speed{100};
    /// Mapear botoes (button_remap.h): the mapping this game uses, and whether it is its own.
    ButtonRemap remap{IdentityRemap()};
    bool game_remap{};
};

/// Speed in percent of the console's (10 to 200, steps of 10); 0 is no limit, below 10%.
inline std::uint16_t StepSpeed(std::uint16_t speed, bool back) {
    if (back) return speed <= 10 ? 0 : static_cast<std::uint16_t>(speed - 10);
    if (speed == 0) return 10;
    return speed >= 200 ? 200 : static_cast<std::uint16_t>(speed + 10);
}

/// Library: back to home, keeping a Pulse session when they are on. LibraryWithoutSession: the
/// player chose to leave without one.
enum class GameMenuResult : std::uint8_t {
    None, Resume, Library, SettingsChanged, FullScreen, Cheats, PerformanceStatsChanged, UpscalingChanged,
    LibraryWithoutSession,
    CaptureGraphics,
    QuickSettingsChanged, ///< Speed limit or volume: Settings() has the new values.
    RemapChanged,         ///< This game's button mapping: Settings().remap.
};

/// One row as drawn: a label, and the value of a setting (changed with left/right) or of a page.
struct GameMenuRow {
    enum class Kind : std::uint8_t { Action, Choice, Page };
    std::wstring label;
    std::wstring value;
    Kind kind{Kind::Action};
};

/// The menu's items and cursor, with texts in the interface language.
class GameMenu {
public:
    enum Item : std::size_t {
        Continue, Cheats, Mods, Style, FaceButtons, Deadzone, FullScreen, ImageUpscaling, PerformanceStats, Library,
        CaptureGraphics, SpeedLimit, Volume, Remap,
    };

    /// `cheats`: the game can have cheats (it has a title id), so the menu offers them.
    explicit GameMenu(GameMenuSettings settings_, bool cheats = false) : settings{settings_} {
        items = {Continue};
        if (cheats) items.push_back(Cheats);
        items.insert(items.end(), {Style, FaceButtons, Deadzone, Remap});
        if (settings.can_fullscreen) items.push_back(FullScreen);
        // NS HUEHUE: Ampliacao da imagem lives in the game's Desempenho e graficos, saved per game.
        if (settings.offer_upscaling) items.push_back(ImageUpscaling);
        items.push_back(PerformanceStats);
        // Quick settings, as the player knows them from other builds: speed and sound.
        items.insert(items.end(), {SpeedLimit, Volume});
        if (settings.material_capture) items.push_back(CaptureGraphics);
        items.push_back(Library);
    }

    /// How many cheats are on, shown on the Cheats row.
    void SetCheatsOn(std::size_t count) { cheats_on = count; }

    /// How many mods are on: with any, a row says so. It only informs; mods change in the library,
    /// and a game reads them when it boots.
    void SetModsOn(std::size_t count) {
        mods_on = count;
        const auto found = std::find(items.begin(), items.end(), Mods);
        if (count == 0 || found != items.end()) return;
        const auto after = std::find(items.begin(), items.end(), Cheats);
        items.insert(after != items.end() ? after + 1 : items.begin() + 1, Mods);
    }

    /// The Mapear botoes page is open in place of the list.
    [[nodiscard]] bool RemapOpen() const { return remap_open; }

    GameMenuResult Apply(MenuAction action) {
        if (remap_open) return ApplyRemap(action);
        switch (action) {
        case MenuAction::Toggle:
        case MenuAction::Back:
            return GameMenuResult::Resume;
        case MenuAction::Up:
            selected = (selected + items.size() - 1) % items.size();
            return GameMenuResult::None;
        case MenuAction::Down:
            selected = (selected + 1) % items.size();
            return GameMenuResult::None;
        case MenuAction::Left:
        case MenuAction::Right:
        case MenuAction::Confirm:
            return Change(action);
        case MenuAction::Alternate:
        case MenuAction::Refresh:
        case MenuAction::PreviousTab:
        case MenuAction::NextTab:
            break;
        }
        return GameMenuResult::None;
    }

    [[nodiscard]] std::vector<GameMenuRow> Rows() const {
        std::vector<GameMenuRow> rows;
        if (remap_open) {
            // Each Switch button and its Xbox control, then Restaurar padrao and Voltar.
            for (std::size_t i = 0; i < RemapCount; ++i)
                rows.push_back({L"Switch " + std::wstring{SwitchButtonName(i)},
                                std::wstring{XboxControlName(settings.remap[i])}, GameMenuRow::Kind::Choice});
            rows.push_back({Tr(I18n::Text::NsRemapReset)});
            rows.push_back({Tr(I18n::Text::Back)});
            return rows;
        }
        for (const Item item : items) rows.push_back(Row(item));
        return rows;
    }

    /// Row of the cursor, as drawn.
    [[nodiscard]] std::size_t Selected() const { return remap_open ? remap_selected : selected; }
    [[nodiscard]] Item SelectedItem() const { return items[selected]; }
    [[nodiscard]] const GameMenuSettings& Settings() const { return settings; }

private:
    static std::wstring Tr(I18n::Text id) { return std::wstring{I18n::Tr(id)}; }

    GameMenuRow Row(Item item) const {
        using I18n::Text;
        constexpr auto Choice = GameMenuRow::Kind::Choice;
        switch (item) {
        case Continue:
            return {Tr(Text::MenuResume)};
        case CaptureGraphics:
            return {Tr(Text::MenuCaptureGraphics)};
        case Cheats:
            return {Tr(Text::CheatsTitle), I18n::Plural(Text::CheatsOnOne, Text::CheatsOnOther, cheats_on),
                    GameMenuRow::Kind::Page};
        case Mods:
            return {Tr(Text::ModsTitle), I18n::Plural(Text::ModsActiveOne, Text::ModsActiveOther, mods_on) +
                                             L" · " + Tr(Text::MenuModsAtBoot)};
        case Style:
            return {Tr(Text::ControllerSeenAs), Tr(ControllerStyleText(settings.style)), Choice};
        case FaceButtons:
            return {Tr(Text::FaceButtons), Tr(settings.swap_face_buttons ? Text::ByPosition : Text::ByLetter),
                    Choice};
        case Deadzone:
            return {Tr(Text::DeadZone), std::to_wstring(static_cast<int>(settings.deadzone * 100 + 0.5f)) + L"%",
                    Choice};
        case ImageUpscaling:
            return {Tr(Text::Upscaling), Tr(UpscalingText(settings.upscaling)), Choice};
        case PerformanceStats:
            return {Tr(Text::PerformanceStats), Tr(settings.show_performance_stats ? Text::Yes : Text::No), Choice};
        case Remap:
            return {Tr(Text::NsRemapGame), Tr(settings.game_remap ? Text::NsRemapOwn : Text::NsRemapDefault),
                    GameMenuRow::Kind::Page};
        case SpeedLimit:
            return {Tr(Text::MenuSpeedLimit),
                    settings.speed == 0 ? Tr(Text::NsSpeedOff) : std::to_wstring(settings.speed) + L"%", Choice};
        case Volume:
            return {Tr(Text::MenuVolume), settings.volume == 0 ? Tr(Text::MenuMuted)
                                                               : std::to_wstring(settings.volume) + L"%", Choice};
        case FullScreen:
            return {Tr(Text::MenuFullScreen), Tr(settings.fullscreen ? Text::Yes : Text::No), Choice};
        case Library:
            if (!settings.pulse_sessions) return {Tr(Text::MenuBackToHome)};
            return {Tr(Text::MenuBackToHome), Tr(keep_session ? Text::MenuKeepSession : Text::MenuSkipSession),
                    Choice};
        }
        return {};
    }

    GameMenuResult Change(MenuAction action) {
        const bool back = action == MenuAction::Left;
        switch (SelectedItem()) {
        case Continue:
            return action == MenuAction::Confirm ? GameMenuResult::Resume : GameMenuResult::None;
        case CaptureGraphics:
            return action == MenuAction::Confirm ? GameMenuResult::CaptureGraphics : GameMenuResult::None;
        case Cheats:
            return action == MenuAction::Confirm ? GameMenuResult::Cheats : GameMenuResult::None;
        case Mods:
            return GameMenuResult::None;
        case Remap:
            if (action == MenuAction::Confirm) {
                remap_open = true;
                remap_selected = 0;
            }
            return GameMenuResult::None;
        case Library:
            if (action != MenuAction::Confirm) {
                keep_session = !keep_session;
                return GameMenuResult::None;
            }
            return !settings.pulse_sessions || keep_session ? GameMenuResult::Library
                                                            : GameMenuResult::LibraryWithoutSession;
        case Style:
            settings.style = StepConsoleControllerStyle(settings.style, back);
            return GameMenuResult::SettingsChanged;
        case FaceButtons:
            settings.swap_face_buttons = !settings.swap_face_buttons;
            return GameMenuResult::SettingsChanged;
        case Deadzone: {
            // Same three steps as the library panel.
            constexpr float steps[] = {0.08f, 0.12f, 0.18f};
            std::size_t i = settings.deadzone < 0.10f ? 0 : settings.deadzone < 0.15f ? 1 : 2;
            i = back ? (i + 2) % 3 : (i + 1) % 3;
            settings.deadzone = steps[i];
            return GameMenuResult::SettingsChanged;
        }
        case ImageUpscaling:
            settings.upscaling = StepUpscaling(settings.upscaling, back);
            return GameMenuResult::UpscalingChanged;
        case PerformanceStats:
            settings.show_performance_stats = !settings.show_performance_stats;
            return GameMenuResult::PerformanceStatsChanged;
        case SpeedLimit:
            settings.speed = StepSpeed(settings.speed, back);
            settings.speed_limit = settings.speed != 0;
            return GameMenuResult::QuickSettingsChanged;
        case Volume:
            // Right/A louder, left quieter, 0 to 200%; A past 200% wraps to mute.
            if (back) settings.volume = settings.volume >= 10 ? static_cast<std::uint8_t>(settings.volume - 10) : 0;
            else settings.volume = settings.volume >= 200 ? (action == MenuAction::Confirm ? 0 : 200)
                                                          : static_cast<std::uint8_t>(settings.volume + 10);
            return GameMenuResult::QuickSettingsChanged;
        case FullScreen:
            settings.fullscreen = !settings.fullscreen;
            return GameMenuResult::FullScreen;
        }
        return GameMenuResult::None;
    }

    GameMenuResult ApplyRemap(MenuAction action) {
        const std::size_t count = RemapCount + 2;
        switch (action) {
        case MenuAction::Toggle:
            return GameMenuResult::Resume;
        case MenuAction::Back:
            remap_open = false;
            return GameMenuResult::None;
        case MenuAction::Up:
            remap_selected = (remap_selected + count - 1) % count;
            return GameMenuResult::None;
        case MenuAction::Down:
            remap_selected = (remap_selected + 1) % count;
            return GameMenuResult::None;
        case MenuAction::Left:
        case MenuAction::Right:
        case MenuAction::Confirm:
            if (remap_selected < RemapCount) {
                auto& source = settings.remap[remap_selected];
                source = StepRemapSource(source, action == MenuAction::Left);
                settings.game_remap = true;
                return GameMenuResult::RemapChanged;
            }
            if (action != MenuAction::Confirm) return GameMenuResult::None;
            if (remap_selected == RemapCount) {
                settings.remap = IdentityRemap();
                settings.game_remap = true;
                return GameMenuResult::RemapChanged;
            }
            remap_open = false;
            return GameMenuResult::None;
        default:
            return GameMenuResult::None;
        }
    }

    GameMenuSettings settings;
    std::vector<Item> items;
    std::size_t selected{};
    bool remap_open{};
    std::size_t remap_selected{};
    std::size_t cheats_on{};
    std::size_t mods_on{};
    bool keep_session{true};
};
} // namespace EdenXbox
