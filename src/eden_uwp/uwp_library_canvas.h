// SPDX-FileCopyrightText: Copyright 2026 JulianDr14
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "eden_uwp/content/content_session.h"
#include <string_view>
#include "eden_uwp/cheats/cheat_session.h"
#include "eden_uwp/cheats/cheats_page.h"
#include "eden_uwp/game_library.h"
#include "eden_uwp/game_options_page.h"
#include "eden_uwp/mods/mod_session.h"
#include "eden_uwp/mods/mods_page.h"
#include "eden_uwp/system_page.h"
#include "eden_uwp/text_entry_panel.h"
#include "eden_uwp/uwp_input.h"
#include "eden_uwp/uwp_controllers.h"
#include "eden_uwp/uwp_file_manager.h"
namespace EdenXbox {
/// NS HUEHUE search page (1920x1080 design units): where the keyboard's keys sit, so the canvas
/// and the pointer agree.
struct NsSearchLayout {
    static constexpr float KeysX = 500, KeysY = 420;
    static constexpr float PanelX() { return KeysX - TextEntryPanelLayout::Left; }
    static constexpr float PanelY() { return KeysY - TextEntryPanelLayout::Top; }
};
/// The selected game's options page, when open.
struct GameOptionsView {
    const GameOptionsPage& page;
    const LibraryEntry* game;            ///< nullptr when the page is closed.
    const Cheats::CheatSession* cheats;  ///< The open cheats page's.
    const Cheats::CheatsPage& cheats_page;
    std::wstring_view notice;
    const Content::ContentSession* content{};
    const Mods::ModSession* mods{};      ///< The open mods page's.
    const Mods::ModsPage* mods_page{};
    bool favorite{}; ///< NS HUEHUE: the game is a favorite (the list's last row adds or removes it).
};
/// Everything one frame of the library shows.
struct LibraryView {
    const LibraryScan& scan;
    std::size_t selected;
    bool loading;
    bool controller_panel;
    unsigned controller_row;
    const ControllerOptions& options;
    const std::wstring& notice;
    const ControllerPanel& panel;
    const ConfigurationPanel& configuration;
    const SystemPage& system;
    const GameOptionsView& game_options;
    bool missing_content{};
    // NX Zero HueHue: the home menu in front of the library, and its About page.
    bool home{};
    unsigned home_row{};
    bool about{};
    // NS HUEHUE: the grid of "Meus jogos".
    unsigned library_tab{};                     ///< 0 all, 1 favorites, 2 recent.
    int library_focus{-1};                      ///< -1 the grid, 0..2 a tab, 3 the search box.
    const std::vector<std::size_t>* shown{};    ///< The entries the grid shows, in order.
    const std::vector<std::string>* favorites{}; ///< Launch paths.
    std::wstring_view query{};
    const TextEntry* search{};                  ///< The search keyboard, while open.
    bool about_credits{};
    const std::vector<bool>* translated{}; ///< By entry: a fan translation exists (Traducoes tab).
    bool translations_ready{};             ///< The translation index was read (or could not be).
};
class LibraryCanvas {
public:
    LibraryCanvas(void* window, unsigned width, unsigned height);
    ~LibraryCanvas();
    /// Drops decoded covers, e.g. after the library cache is cleared.
    void InvalidateCovers();
    void Draw(const LibraryView& view);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
