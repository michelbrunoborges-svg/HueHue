// SPDX-FileCopyrightText: Copyright 2026 JulianDr14
// SPDX-License-Identifier: GPL-3.0-or-later
// Build with src/eden_uwp/localization.cpp and src/eden_uwp/locales/*.cpp.
#include <cstdio>
#include <cstdlib>
#include "eden_uwp/game_menu.h"
#include "eden_uwp/system_page.h"
#include "eden_uwp/button_remap.h"

int main() {
    using namespace EdenXbox;
    auto check = [](bool ok) { if (!ok) std::abort(); };

    // Both pressed together: never reach the game, fire once after two seconds held.
    {
        MenuComboFilter f;
        int fires = 0;
        for (std::uint32_t t = 0; t <= 1900; t += 4) {
            bool plus = true, minus = true;
            check(!f.Filter(plus, minus, t));
        }
        for (std::uint32_t t = 1904; t <= 2500; t += 4) {
            bool plus = true, minus = true;
            fires += f.Filter(plus, minus, t);
            check(!plus && !minus);
        }
        check(fires == 1);
        bool plus = false, minus = false;
        check(!f.Filter(plus, minus, 2504) && !plus && !minus);
    }
    // View first, Menu 40 ms later: still the combo, - never delivered; released early: no fire.
    {
        MenuComboFilter f;
        bool fired = false;
        for (std::uint32_t t = 0; t < 600; t += 4) {
            bool plus = t >= 40, minus = true;
            fired |= f.Filter(plus, minus, t);
            check(!plus && !minus);
        }
        bool plus = false, minus = false;
        f.Filter(plus, minus, 604);
        check(!fired && !plus && !minus);
    }
    // A quick tap of + reaches the game (on release), then releases.
    {
        MenuComboFilter f;
        bool plus = true, minus = false;
        f.Filter(plus, minus, 0);
        check(!plus);
        plus = true;
        f.Filter(plus, minus, 30);
        check(!plus);
        plus = false;
        f.Filter(plus, minus, 60);
        check(plus && !minus);
        plus = false;
        f.Filter(plus, minus, 64);
        check(!plus);
    }
    // A held + is delivered after the pending window and stays delivered.
    {
        MenuComboFilter f;
        bool delivered = false;
        for (std::uint32_t t = 0; t <= 400; t += 4) {
            bool plus = true, minus = false;
            f.Filter(plus, minus, t);
            if (t >= MenuComboFilter::PendingMs) check(plus);
            else check(!plus);
            delivered |= plus;
        }
        check(delivered);
    }

    // Menu model, in Spanish.
    I18n::SetUiLanguage(*I18n::FindUiLanguage(L"es"));
    GameMenu menu{{ConsoleControllerStyle::Auto, false, 0.12f}};
    check(menu.Rows().size() == 12 && menu.SelectedItem() == GameMenu::Continue);
    check(menu.Apply(MenuAction::Confirm) == GameMenuResult::Resume);
    check(menu.Apply(MenuAction::Back) == GameMenuResult::Resume);
    check(menu.Apply(MenuAction::Toggle) == GameMenuResult::Resume);
    check(menu.Apply(MenuAction::Up) == GameMenuResult::None && menu.SelectedItem() == GameMenu::Library);
    check(menu.Apply(MenuAction::Confirm) == GameMenuResult::Library);
    check(menu.Apply(MenuAction::Down) == GameMenuResult::None && menu.SelectedItem() == GameMenu::Continue);
    menu.Apply(MenuAction::Down);
    check(menu.Apply(MenuAction::Right) == GameMenuResult::SettingsChanged);
    check(menu.Settings().style == ConsoleControllerStyle::Pro);
    check(menu.Apply(MenuAction::Left) == GameMenuResult::SettingsChanged);
    check(menu.Apply(MenuAction::Left) == GameMenuResult::SettingsChanged);
    check(menu.Settings().style == ConsoleControllerStyle::RightJoycon);
    check(menu.Rows()[1].label == L"La consola lo ve como");
    check(menu.Rows()[1].kind == GameMenuRow::Kind::Choice);
    check(menu.Rows()[1].value.find(L"Joy-Con derecho") != std::wstring::npos);
    menu.Apply(MenuAction::Down);
    check(menu.Apply(MenuAction::Confirm) == GameMenuResult::SettingsChanged && menu.Settings().swap_face_buttons);
    menu.Apply(MenuAction::Down);
    check(menu.Apply(MenuAction::Right) == GameMenuResult::SettingsChanged && menu.Settings().deadzone > 0.15f);
    check(menu.Apply(MenuAction::Right) == GameMenuResult::SettingsChanged && menu.Settings().deadzone < 0.10f);
    check(menu.Rows()[3].value == L"8%");
    check(menu.Rows()[0].kind == GameMenuRow::Kind::Action && menu.Rows()[0].value.empty());
    // Presses the menu's own rows ignore.
    for (const auto action : {MenuAction::Alternate, MenuAction::Refresh, MenuAction::PreviousTab,
                              MenuAction::NextTab})
        check(menu.Apply(action) == GameMenuResult::None);

    // A game with a title id offers its cheats, second, with how many are on.
    GameMenu with_cheats{{ConsoleControllerStyle::Auto, false, 0.12f}, true};
    with_cheats.SetCheatsOn(3);
    check(with_cheats.Rows().size() == 13);
    check(with_cheats.Rows()[1].kind == GameMenuRow::Kind::Page && with_cheats.Rows()[1].value == L"3 activos");
    check(with_cheats.Apply(MenuAction::Down) == GameMenuResult::None && with_cheats.SelectedItem() == GameMenu::Cheats);
    check(with_cheats.Apply(MenuAction::Right) == GameMenuResult::None);
    check(with_cheats.Apply(MenuAction::Confirm) == GameMenuResult::Cheats);
    with_cheats.SetCheatsOn(1);
    check(with_cheats.Rows()[1].value == L"1 activo");

    // PC: full screen sits above the display rows and "back to the library"; the Series menu never
    // shows it.
    GameMenu pc{{ConsoleControllerStyle::Auto, false, 0.12f, true, false}};
    check(pc.Rows().size() == 13 && pc.Rows()[5].label == L"Pantalla completa" && pc.Rows()[5].value == L"No");
    while (pc.SelectedItem() != GameMenu::FullScreen) pc.Apply(MenuAction::Up);
    check(pc.SelectedItem() == GameMenu::FullScreen && pc.Selected() == 5);
    check(pc.Apply(MenuAction::Confirm) == GameMenuResult::FullScreen && pc.Settings().fullscreen);
    check(pc.Apply(MenuAction::Right) == GameMenuResult::FullScreen && !pc.Settings().fullscreen);
    for (const auto& row : menu.Rows()) check(row.label != L"Pantalla completa");
    // The same menu follows the interface language.
    I18n::SetUiLanguage(*I18n::FindUiLanguage(L"en"));
    check(pc.Rows()[0].label == L"Resume" && pc.Rows()[5].label == L"Full screen" && pc.Rows()[5].value == L"No");
    // Both entry points start off and toggle the same persisted setting model.
    SystemPage system_page;
    check(!system_page.settings.show_performance_stats);
    system_page.selected = SystemPage::PerformanceStats;
    check(HandleSystemInput(system_page, SystemInput::Confirm) == SystemPageEffect::Changed);
    check(system_page.settings.show_performance_stats);
    check(HandleSystemInput(system_page, SystemInput::Left) == SystemPageEffect::Changed);
    check(!system_page.settings.show_performance_stats);
    check(SystemPageLayout::RowTop + (SystemPage::RowCount - 1) * SystemPageLayout::RowStride +
          SystemPageLayout::RowHeight < SystemPageLayout::Notice);
    GameMenu stats_menu{{}};
    check(!stats_menu.Settings().show_performance_stats);
    while (stats_menu.SelectedItem() != GameMenu::PerformanceStats) stats_menu.Apply(MenuAction::Up);
    check(stats_menu.Apply(MenuAction::Confirm) == GameMenuResult::PerformanceStatsChanged);
    check(stats_menu.Settings().show_performance_stats);
    auto saved = stats_menu.Settings();
    GameMenu reopened{saved};
    check(reopened.Settings().show_performance_stats);
    check(stats_menu.Apply(MenuAction::Right) == GameMenuResult::PerformanceStatsChanged);
    check(!stats_menu.Settings().show_performance_stats);

    // Image upscaling: above the statistics, a choice that steps both ways and wraps around.
    I18n::SetUiLanguage(*I18n::FindUiLanguage(L"es"));
    GameMenuSettings scaling_settings{};
    scaling_settings.offer_upscaling = true;
    GameMenu scaling_menu{scaling_settings};
    while (scaling_menu.SelectedItem() != GameMenu::ImageUpscaling) scaling_menu.Apply(MenuAction::Up);
    const auto scaling_row = [&] { return scaling_menu.Rows()[scaling_menu.Selected()]; };
    check(scaling_row().label == L"Escalado de imagen" && scaling_row().value == L"Bilineal");
    check(scaling_row().kind == GameMenuRow::Kind::Choice);
    check(scaling_menu.Apply(MenuAction::Right) == GameMenuResult::UpscalingChanged);
    check(scaling_menu.Settings().upscaling == Upscaling::FsrSharp && scaling_row().value == L"FSR 1 · nítido");
    check(scaling_menu.Apply(MenuAction::Confirm) == GameMenuResult::UpscalingChanged);
    check(scaling_row().value == L"FSR 1 · equilibrado");
    check(scaling_menu.Apply(MenuAction::Left) == GameMenuResult::UpscalingChanged);
    check(scaling_menu.Apply(MenuAction::Left) == GameMenuResult::UpscalingChanged);
    check(scaling_menu.Settings().upscaling == Upscaling::Bilinear);
    check(scaling_menu.Apply(MenuAction::Left) == GameMenuResult::UpscalingChanged);
    const auto stepped = scaling_menu.Settings().upscaling;
    check(stepped == StepUpscaling(Upscaling::Bilinear, true));
    GameMenu scaling_reopened{scaling_menu.Settings()};
    check(scaling_reopened.Settings().upscaling == stepped);
    I18n::SetUiLanguage(*I18n::FindUiLanguage(L"en"));
    GameMenuSettings diagnostic_settings{};
    diagnostic_settings.material_capture = true;
    GameMenu diagnostic{diagnostic_settings};
    const auto normal_rows = GameMenu{GameMenuSettings{}}.Rows();
    check(diagnostic.Rows().size() == normal_rows.size() + 1);
    while (diagnostic.SelectedItem() != GameMenu::CaptureGraphics) diagnostic.Apply(MenuAction::Down);
    check(diagnostic.Rows()[diagnostic.Selected()].label == L"Capture graphics and resume");
    check(diagnostic.Apply(MenuAction::Left) == GameMenuResult::None);
    check(diagnostic.Apply(MenuAction::Right) == GameMenuResult::None);
    check(diagnostic.Apply(MenuAction::Confirm) == GameMenuResult::CaptureGraphics);
    check(diagnostic.Apply(MenuAction::Back) == GameMenuResult::Resume);
    I18n::SetUiLanguage(*I18n::FindUiLanguage(L"es"));
    check(diagnostic.Rows()[diagnostic.Selected()].label == L"Capturar gráficos y continuar");
    for (const auto& row : normal_rows) check(row.label != L"Capture graphics and resume");

    // NS HUEHUE 0.4.4: speed 10..200 by tens (off below 10), volume up to 200, button mapping.
    check(StepSpeed(100, false) == 110 && StepSpeed(200, false) == 200 && StepSpeed(10, true) == 0);
    check(StepSpeed(0, false) == 10 && StepSpeed(0, true) == 0 && StepSpeed(20, true) == 10);
    {
        GameMenu quick{GameMenuSettings{}};
        while (quick.SelectedItem() != GameMenu::Volume) quick.Apply(MenuAction::Down);
        for (int i = 0; i < 12; ++i) quick.Apply(MenuAction::Right);
        check(quick.Settings().volume == 200);
        check(quick.Apply(MenuAction::Confirm) == GameMenuResult::QuickSettingsChanged && quick.Settings().volume == 0);
        while (quick.SelectedItem() != GameMenu::SpeedLimit) quick.Apply(MenuAction::Up);
        check(quick.Apply(MenuAction::Right) == GameMenuResult::QuickSettingsChanged && quick.Settings().speed == 110);
        for (int i = 0; i < 12; ++i) quick.Apply(MenuAction::Left);
        check(quick.Settings().speed == 0 && !quick.Settings().speed_limit);
        while (quick.SelectedItem() != GameMenu::Remap) quick.Apply(MenuAction::Up);
        check(quick.Apply(MenuAction::Confirm) == GameMenuResult::None && quick.RemapOpen());
        check(quick.Apply(MenuAction::Right) == GameMenuResult::RemapChanged && quick.Settings().remap[0] == 1);
        check(quick.Settings().game_remap);
        check(quick.Apply(MenuAction::Back) == GameMenuResult::None && !quick.RemapOpen());
        // 0.4.5: Shaders pendentes, the passes that wait and Silenciar, live.
        while (quick.SelectedItem() != GameMenu::PsoDefer) quick.Apply(MenuAction::Down);
        check(quick.Apply(MenuAction::Confirm) == GameMenuResult::QuickSettingsChanged && quick.Settings().pso_defer);
        quick.Apply(MenuAction::Confirm);
        check(!quick.Settings().pso_defer);
        quick.Apply(MenuAction::Down);
        check(quick.SelectedItem() == GameMenu::CriticalVertices);
        check(quick.Apply(MenuAction::Right) == GameMenuResult::QuickSettingsChanged &&
              quick.Settings().critical_vertices == 12 && quick.Settings().pso_defer);
        quick.Apply(MenuAction::Left);
        quick.Apply(MenuAction::Left);
        quick.Apply(MenuAction::Left);
        check(quick.Settings().critical_vertices == 0);
        quick.Apply(MenuAction::Left);
        check(quick.Settings().critical_vertices == 48);
        while (quick.SelectedItem() != GameMenu::Mute) quick.Apply(MenuAction::Down);
        check(quick.Apply(MenuAction::Confirm) == GameMenuResult::QuickSettingsChanged && quick.Settings().muted);
    }
    {
        auto remap = IdentityRemap();
        check(IsIdentity(remap) && ParseRemap(SerializeRemap(remap)) == remap);
        remap[3] = RemapNone;
        remap[0] = 2;
        check(ParseRemap(SerializeRemap(remap)) == remap && !ParseRemap("1,2,3").has_value());
        check(StepRemapSource(15, false) == RemapNone && StepRemapSource(RemapNone, false) == 0);
        check(StepRemapSource(0, true) == RemapNone);
    }
    std::puts("game menu: controller combo, navigation, settings, upscaling, cheats and opt-in capture PASS");
}
