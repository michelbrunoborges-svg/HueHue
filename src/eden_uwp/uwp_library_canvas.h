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
#include "eden_uwp/game_profile.h"
#include "eden_uwp/online_settings.h"
#include "eden_uwp/system_page.h"
#include "eden_uwp/text_entry_panel.h"
#include "eden_uwp/uwp_input.h"
#include "eden_uwp/uwp_controllers.h"
#include "eden_uwp/uwp_file_manager.h"
#include "eden_uwp/theme_catalog.h"
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
    // NS HUEHUE: what the game has, shown on its rows (empty / 0 when none).
    std::wstring_view update_version{}; ///< The active update's version.
    std::size_t dlc_count{};            ///< DLC turned on.
    std::size_t mods_count{};           ///< Mods turned on.
    std::uint8_t profile{}; ///< NS HUEHUE: the game's GameProfile, for its row.
    const GameTuning* tuning{}; ///< NS HUEHUE: the Desempenho e graficos page, while open.
    std::size_t tuning_row{};
    bool favorite{}; ///< NS HUEHUE: the game is a favorite (the list's last row adds or removes it).
    bool own_remap{}; ///< NS HUEHUE: the game has its own button mapping (Controles do jogo).
};
/// NS HUEHUE: Configuracoes > Saves (USB), the copy of the saves on an external drive.
struct SavesView {
    bool page{};          ///< Its rows take the cursor.
    unsigned row{};
    bool has_folder{};
    std::wstring folder;  ///< The picked folder's path.
    bool automatic{};
    std::int64_t last_time{};
    std::uint32_t last_games{};
    bool busy{}, restoring{};
    std::uint32_t done{}, total{};
    std::wstring_view notice;
    // 0.4.5: the shader caches (shader_backup.h).
    bool shaders_folder{};
    std::wstring shaders_path;
    bool shaders_sync{};
};
/// NS HUEHUE Configuracoes > Sistema > Temas (theme.h): the cards, the buttons and the preview.
struct ThemesView {
    bool open{};
    bool previewing{};     ///< The candidate's home is on screen; nothing is saved yet.
    unsigned card{};       ///< The candidate: a ThemeId below ThemeCount, else packs[card - ThemeCount].
    unsigned focus{};      ///< 0 the cards, 1 Previa, 2 Aplicar tema, 3 the home's ground, 4 Importar temas.
    unsigned current{};    ///< The card applied.
    unsigned wallpaper{};  ///< The candidate ground (0 solid, 1 Aurora).
    std::wstring_view notice;
    const std::vector<ThemePack>* packs{}; ///< The catalog's packs.
};
/// NS HUEHUE Mapear botoes (button_remap.h): for every game or for one.
struct RemapView {
    bool open{};
    bool game{};        ///< One game's mapping (its options); else the saved one (Controles).
    bool own{};         ///< The game has its own mapping.
    std::size_t row{};  ///< 0-15 the Switch buttons, then Restaurar padrao [, Usar o geral].
    ButtonRemap remap{IdentityRemap()};
    std::wstring_view title;
    std::wstring_view notice;
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
    const OnlineSettings* online{};        ///< NS HUEHUE online play (Configuracoes > Online).
    bool online_page{};                    ///< Its rows take the cursor.
    unsigned online_row{};
    unsigned edit_target{};                ///< 0: the keyboard types the search; 1-3: server, port, password.
    const SavesView* saves{};              ///< NS HUEHUE Configuracoes > Saves (USB).
    // NS HUEHUE - Switch layout (Sistema > Layout do menu).
    unsigned theme{};                      ///< ThemeId of this frame (the candidate while previewing).
    unsigned wallpaper{};                  ///< The Switch home's ground (0 solid, 1 Aurora).
    const ThemesView* themes{};            ///< Sistema > Temas, while open.
    const std::vector<std::size_t>* home_tiles{}; ///< Its game tiles (entries), then Todos os jogos.
    unsigned home_tile_row{};              ///< 0 the tiles, 1 the buttons.
    std::size_t home_tile{};
    unsigned home_button{};
    const ThemePack* pack{};               ///< The frame's theme is this catalog pack (its colors and art).
    unsigned exit_prompt{};                ///< Sair do NS: 1 asking about the saves copy, 2 copying.
    const RemapView* remap{};              ///< Mapear botoes, while open.
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
