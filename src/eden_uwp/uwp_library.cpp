// SPDX-FileCopyrightText: Copyright 2026 JulianDr14
// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden_uwp/uwp_library.h"
#include "eden_uwp/stick_navigation.h"
#include "eden_uwp/save_backup.h"
#include "eden_uwp/theme.h"
#include "eden_uwp/theme_catalog.h"
#include "eden_uwp/controller_catalog.h"
#include "eden_uwp/text_entry_input.h"
#include "eden_uwp/uwp_library_canvas.h"
#include "eden_uwp/uwp_rom_storage.h"
#include "eden_uwp/storage_path.h"
#include "eden_uwp/uwp_file_manager.h"

#include <algorithm>
#include <map>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <future>
#include <thread>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Profile.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include "eden_uwp/cheats/cheat_session.h"
#include "eden_uwp/cheats/cheats_page.h"
#include "eden_uwp/game_library.h"
#include "eden_uwp/game_options_page.h"
#include "eden_uwp/game_compat.h"
#include "eden_uwp/game_profile.h"
#include "eden_uwp/online_settings.h"
#include "eden_uwp/content/auto_import.h"
#include "eden_uwp/content/content_view.h"
#include "eden_uwp/content/content_service.h"
#include "eden_uwp/mods/mod_settings.h"
#include "eden_uwp/mods/cnx_index.h"
#include "eden_uwp/mods/mod_installer.h"
#include "eden_uwp/mods/mod_session.h"
#include "eden_uwp/mods/mod_web.h"
#include "eden_uwp/mods/mods_page.h"
#include "eden_uwp/mods/mods_view.h"
#include "eden_uwp/pulse_sessions.h"
#include "eden_uwp/library_cache.h"
#include "eden_uwp/localization.h"
#include "eden_uwp/system_page.h"
#include "eden_uwp/uwp_input.h"
#include "eden_uwp/uwp_library_metadata.h"
#include "eden_uwp/uwp_system_settings.h"
#include <fmt/format.h>
#include "common/logging.h"
#include "core/file_sys/common_funcs.h"
#include "core/core.h"
#include "core/hle/service/filesystem/filesystem.h"

namespace EdenXbox {
namespace {
using winrt::Windows::Gaming::Input::Gamepad;
using winrt::Windows::Gaming::Input::GamepadButtons;
using winrt::Windows::System::VirtualKey;
using I18n::Tr;
using T = I18n::Text;

/// Games per row of the grid (the NS HUEHUE grid has five); two rows show at a time, and the
/// metadata window is that page and a row on each side.
constexpr size_t PageSize = 5;
constexpr size_t PageVisible = 2 * PageSize;
constexpr size_t RecentLimit = 30;
/// Background batches stay short so a page change never waits long behind them: games opened
/// through the loaders per fill batch, cache records read per names batch.
constexpr size_t FillBatch = 6;
constexpr size_t NamesBatch = 64;

std::wstring Wide(std::string_view utf8) {
    return winrt::to_hstring(utf8).c_str();
}

// NS HUEHUE: favorites and recent games, one launch path per line in LocalState.
std::filesystem::path ListPath(const wchar_t* name) {
    try {
        return std::filesystem::path{winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str()} / name;
    } catch (...) { return {}; }
}
std::vector<std::string> LoadList(const wchar_t* name) {
    std::vector<std::string> list;
    const auto path = ListPath(name);
    if (path.empty()) return list;
    std::ifstream in(path, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && std::find(list.begin(), list.end(), line) == list.end()) list.push_back(std::move(line));
    }
    return list;
}
bool SaveList(const wchar_t* name, const std::vector<std::string>& list) {
    const auto path = ListPath(name);
    if (path.empty()) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const auto& line : list) out << line << '\n';
    return static_cast<bool>(out);
}
/// Lower case without accents, so "pokemon" finds "Pokémon".
std::wstring Fold(std::wstring_view text) {
    static constexpr std::wstring_view from = L"áàâãäéèêëíìîïóòôõöúùûüçñ";
    static constexpr std::wstring_view to = L"aaaaaeeeeiiiiooooouuuucn";
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text) {
        const auto lower = static_cast<wchar_t>(std::towlower(c));
        const auto at = from.find(lower);
        out += at == std::wstring_view::npos ? lower : to[at];
    }
    return out;
}
} // namespace

std::optional<std::string> ShowGameLibrary(void* core_window, unsigned width, unsigned height,
                                         const std::filesystem::path& root,
                                         const std::function<void()>& seed,
                                         std::string_view auto_pick, std::string_view initial_notice) {
    using namespace winrt::Windows::UI::Core;
    auto window = CoreWindow::GetForCurrentThread();
    auto canvas = std::make_unique<LibraryCanvas>(core_window, width, height);
    LibraryScan scan;
    size_t selected = 0;
    bool dirty = true, loading = true, settings = false;
    ConfigurationPanel configuration;
    SystemPage system_page;
    // The selected game's options (Menu), and its cheats page while open.
    GameOptionsPage game_options;
    std::unique_ptr<Content::ContentSession> content_session;
    bool missing_content = false;
    std::optional<Cheats::CheatSession> cheat_session;
    Cheats::CheatsPage cheats_page;
    std::unique_ptr<Mods::ModSession> mod_session;
    Mods::ModsPage mods_page;
    std::wstring options_notice;
    system_page.settings = LoadSystemSettings();
    system_page.locale = DetectSystemLocale();
    // Titles are read in the game language, so the library shows what the games will.
    Settings::Language title_language = ApplySystemSettings().game_language;
    const LibraryCache cache{DefaultLibraryCacheDirectory()};
    bool return_to_configuration = false;
    unsigned picker_kind = 0; // 0 games, 1 keys, 2 firmware
    std::future<FileSetupStatus> setup_future;
    std::jthread setup_worker;
    FileImportProgress import_progress;
    std::future<std::string> import_future;
    // NS HUEHUE firmware (Arquivos e USB): a validated copy waiting for the player's A, and what the
    // worker is doing (0 import, 1 install the staged copy, 2 restore the backup).
    std::optional<StagedFirmware> firmware_staged;
    unsigned firmware_job = 0;
    bool restore_confirm = false;
    std::jthread import_worker;
    std::chrono::steady_clock::time_point auto_pick_at{};
    unsigned setting_row = 0;
    ControllerPanel panel;
    panel.xbox = winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily() == L"Windows.Xbox";
    ControllerOptions options = LoadControllerOptions();
    if (panel.xbox) options.controller_id.clear();
    panel.keyboard.bindings = LoadKeyboardBindings();
    auto capture_until = std::chrono::steady_clock::time_point{};
    std::wstring notice; // Library scan/metadata messages only.
    auto keyboard_notice_until = std::chrono::steady_clock::time_point{};
    auto controller_notice_until = std::chrono::steady_clock::time_point{};
    auto system_notice_until = std::chrono::steady_clock::time_point{};
    auto options_notice_until = std::chrono::steady_clock::time_point{};
    // Set by input handlers when the name editor closes: true keeps the name, false discards it.
    std::optional<bool> finish_name_editor;
    auto save_keyboard = [&] {
        const bool saved = SaveKeyboardBindings(panel.keyboard.bindings);
        panel.keyboard.notice = Tr(saved ? T::KeyboardSaved : T::KeyboardSaveFailed);
        keyboard_notice_until = saved ? std::chrono::steady_clock::now() + std::chrono::seconds{3} :
                                       std::chrono::steady_clock::time_point::max();
        dirty = true;
    };
    // The Pulse session each game keeps, as the tiles and the options show it.
    auto refresh_pulse = [&] {
        for (auto& entry : scan.entries) {
            const auto kept = PulseSessions::Find(entry.launch_path);
            entry.pulse_saved = kept ? PulseSessions::FormatTime(kept->saved) : std::wstring{};
            entry.pulse_current = kept && kept->current;
        }
        dirty = true;
    };
    auto begin_capture = [&] {
        panel.keyboard.notice.clear();
        panel.keyboard.capturing = true;
        capture_until = std::chrono::steady_clock::now() + std::chrono::seconds{4};
        dirty = true;
    };
    bool device_changed = false;
    size_t retained_page = SIZE_MAX;
    bool metadata_dirty = true;
    size_t names_cursor = 0; // Next game whose title is looked up in the cache, after a scan.
    size_t fill_cursor = 0;  // Next game the background fill reads into the cache.
    unsigned actions = 0;
    enum : unsigned { Up = 1, Down = 2, Play = 4, Quit = 8, Swap = 16, Zone = 32,
                      Refresh = 64, Panel = 128, Left = 256, Right = 512, AddFolder = 1024,
                      Options = 2048, PreviousTab = 4096, NextTab = 8192, Search = 16384 };
    // NS HUEHUE state: the sections, the library's tabs, search and focus, and the lists kept.
    // Inicio (19/10/2026, pedido do Michel): Jogos, Favoritos, Recentes, Configuracoes, Sair do NS.
    // B does not leave the app: Sair do NS does, asking first for a copy of the saves.
    constexpr unsigned NxHomeRows = 5;
    bool home = true;
    unsigned home_row = 0;
    // Sair do NS: 1 asks about the saves copy (A copy and leave, Y leave, B stay), 2 copying then
    // leaving, 3 leave now.
    unsigned exit_prompt = 0;
    // NS HUEHUE - Switch layout (Sistema > Layout do menu): the home is a row of game tiles
    // (recent first, then the rest, up to 12, and Todos os jogos) over a row of round buttons.
    constexpr unsigned SwitchButtons = 7; // ... Buscar, then Sair do NS
    constexpr size_t SwitchTiles = 12;
    unsigned sw_row = 0;     // 0 the tiles, 1 the buttons
    size_t sw_tile = 0;
    unsigned sw_button = 0;
    std::vector<size_t> sw_tiles;
    // Sistema > Temas: the candidate is only drawn (previewed) until Aplicar saves it.
    bool themes_page = false;
    bool theme_previewing = false;
    unsigned themes_card = 0, themes_focus = 0, themes_wallpaper = 0;
    std::wstring themes_notice;
    auto themes_notice_until = std::chrono::steady_clock::time_point::max();
    // The catalog (theme_catalog.h): the built-in three, then the packs. Card k of Temas is the
    // built-in ThemeId k for k < ThemeCount, else theme_packs[k - ThemeCount].
    std::vector<ThemePack> theme_packs = LoadThemePacks();
    // The pack in use (its index), when the saved theme is one that still exists.
    const auto active_pack = [&]() -> std::optional<size_t> {
        const auto& id = system_page.settings.theme_pack;
        if (id.empty()) return std::nullopt;
        for (size_t i = 0; i < theme_packs.size(); ++i)
            if (theme_packs[i].id == id) return i;
        return std::nullopt;
    };
    const auto active_theme = [&]() -> ThemeId {
        if (const auto pack = active_pack()) return theme_packs[*pack].base;
        if (!system_page.settings.theme_pack.empty()) return ThemeId::Graffiti; // the pack is gone
        return static_cast<ThemeId>(std::min<int>(system_page.settings.theme, 2));
    };
    const auto active_card = [&]() -> unsigned {
        if (const auto pack = active_pack()) return static_cast<unsigned>(ThemeCount + *pack);
        return static_cast<unsigned>(active_theme());
    };
    const auto switch_home = [&] { return IsSwitchTheme(active_theme()); };
    std::future<ThemeImportResult> themes_import;
    std::jthread themes_import_worker;
    unsigned library_tab = 0;
    int library_focus = -1;
    std::wstring query;
    std::optional<TextEntry> search_editor;
    std::wstring search_before;
    // NS HUEHUE online play (online_settings.h): Configuracoes > Online and the field being typed.
    // The search keyboard types the fields too; edit_target says where its text goes.
    OnlineSettings online = LoadOnlineSettings();
    bool online_page = false;
    unsigned online_row = 0;
    // NS HUEHUE: Configuracoes > Saves (USB), the copy of the saves on an external drive
    // (save_backup.h). Its jobs run on a worker; the progress is declared before it, so it outlives it.
    bool saves_page = false;
    unsigned saves_row = 0;
    SaveBackup::Settings saves = SaveBackup::LoadSettings();
    bool saves_folder = SaveBackup::HasFolder();
    enum class SavesJob : std::uint8_t { None, Pick, Backup, Replace, Restore, Automatic, SettingsBackup, SettingsRestore };
    SavesJob saves_job = SavesJob::None;
    unsigned saves_confirm = 0; // pressed once: 2 replace another installation's copy, 3 restore
    auto saves_confirm_until = std::chrono::steady_clock::time_point{};
    std::wstring saves_notice;
    auto saves_notice_until = std::chrono::steady_clock::time_point::max();
    std::uint32_t saves_shown_done = 0;
    struct SavesDone {
        SaveBackup::Result result;
        SaveBackup::FolderInfo info;
        bool restored{}; ///< Pick: the folder's saves were brought in.
    };
    SaveBackup::Progress saves_progress;
    std::future<SavesDone> saves_future;
    std::jthread saves_worker;
    enum class EditTarget : unsigned { Search, Server, Port, Password };
    EditTarget edit_target = EditTarget::Search;
    auto sync_editor = [&] {
        if (search_editor && edit_target == EditTarget::Search) query = search_editor->Value();
    };
    std::vector<std::string> favorites = LoadList(L"ns_favorites.txt");
    std::vector<std::string> recents = LoadList(L"ns_recent.txt");
    std::vector<size_t> shown;
    std::vector<size_t> position_of;
    // NS HUEHUE: the fan translations of the CNX index, by base title id, read once after the scan;
    // the Traducoes tab lists the games that have one and A downloads and installs it.
    using TranslationIndex = std::map<std::string, std::vector<Mods::Cnx::Entry>>;
    TranslationIndex translations;
    std::future<std::optional<TranslationIndex>> translations_future;
    std::jthread translations_worker;
    bool translations_started = false, translations_ready = false;
    std::vector<bool> translated; ///< By entry: the game has a translation in the index.
    std::optional<std::string> pending_translation; ///< The entry the mods page downloads once listed.
    bool open_mods_page = false;
    // NS HUEHUE: Mapear botoes (button_remap.h), for every game (Controles) or one (its options).
    bool remap_open = false, remap_game = false, remap_own = false;
    size_t remap_row = 0;
    ButtonRemap remap_edit = IdentityRemap();
    std::string remap_path;
    std::wstring remap_title;
    // NS HUEHUE: the game's Desempenho e graficos page (game_profile.h).
    bool tuning_page = false;
    size_t tuning_row = 0;
    GameTuning tuning;
    // NS HUEHUE: what the game whose options are open has (update, DLC, mods), for its rows.
    std::wstring status_update;
    std::size_t status_dlc = 0, status_mods = 0;
    auto refresh_status = [&](const LibraryEntry& game) {
        status_update.clear();
        status_dlc = status_mods = 0;
        if (!game.title_loaded || game.program_id == 0) return;
        const auto application = FileSys::GetBaseTitleID(game.program_id);
        Content::Catalog catalog;
        if (Content::DefaultStore().Load(catalog) == Content::Error::None) {
            for (const auto& revision : catalog.revisions) {
                if (revision.application != application || !revision.enabled) continue;
                if (revision.kind == Content::Kind::Update) status_update = Wide(revision.display_version);
                else ++status_dlc;
            }
        }
        try { status_mods = Mods::ActiveMods(Mods::DefaultModPaths(), application); } catch (...) {}
    };
    const auto translation_of = [&](const LibraryEntry& entry) -> const std::vector<Mods::Cnx::Entry>* {
        if (!entry.title_loaded || entry.program_id == 0) return nullptr;
        const auto found = translations.find(Mods::TitleFolder(FileSys::GetBaseTitleID(entry.program_id)));
        return found == translations.end() || found->second.empty() ? nullptr : &found->second;
    };
    std::wstring filtered_query;
    bool launch_from_options = false;
    bool options_from_home = false;
    std::vector<size_t> kept_tiles; // the Switch home's tiles whose covers stay loaded // the Switch home's X: B on the options goes back to the home
    unsigned filtered_tab = 0;
    auto library_notice_until = std::chrono::steady_clock::time_point::max();
    // The game grid is what the player sees: no page, overlay or modal over it.
    auto library_top = [&] {
        return !home && !settings && !configuration.open && !game_options.open &&
               !panel.keyboard.open && !missing_content && !search_editor;
    };
    // The entries the grid shows (the tab, then the search), and each one's place in it.
    auto rebuild_shown = [&] {
        shown.clear();
        const std::wstring folded = Fold(query);
        const auto matches = [&](const LibraryEntry& entry) {
            return folded.empty() || Fold(entry.name).find(folded) != std::wstring::npos;
        };
        translated.assign(scan.entries.size(), false);
        for (size_t i = 0; i < scan.entries.size(); ++i) translated[i] = translation_of(scan.entries[i]) != nullptr;
        // NS HUEHUE: one card per game. Files of the same title (a second dump, an update or a DLC
        // the content store did not take) share the base title id (bits 0x1FFF cleared); the base
        // game's own file is kept, else the first one.
        std::vector<bool> duplicate(scan.entries.size(), false);
        {
            std::map<std::uint64_t, size_t> kept;
            for (size_t i = 0; i < scan.entries.size(); ++i) {
                const std::uint64_t id = scan.entries[i].program_id;
                if (id == 0) continue;
                const auto [at, inserted] = kept.try_emplace(id & ~std::uint64_t{0x1FFF}, i);
                if (inserted) continue;
                const bool base = (id & 0x1FFF) == 0;
                const bool kept_base = (scan.entries[at->second].program_id & 0x1FFF) == 0;
                if (base && !kept_base) {
                    duplicate[at->second] = true;
                    at->second = i;
                } else {
                    duplicate[i] = true;
                }
            }
        }
        if (library_tab == 2) {
            for (const auto& path : recents)
                for (size_t i = 0; i < scan.entries.size(); ++i)
                    if (scan.entries[i].launch_path == path && !duplicate[i] && matches(scan.entries[i]))
                        shown.push_back(i);
        } else {
            for (size_t i = 0; i < scan.entries.size(); ++i) {
                const auto& entry = scan.entries[i];
                if (duplicate[i]) continue;
                if (library_tab == 1 && std::find(favorites.begin(), favorites.end(), entry.launch_path) == favorites.end())
                    continue;
                if (library_tab == 3 && !translated[i]) continue;
                if (matches(entry)) shown.push_back(i);
            }
        }
        position_of.assign(scan.entries.size(), SIZE_MAX);
        for (size_t k = 0; k < shown.size(); ++k) position_of[shown[k]] = k;
    };
    // The Switch layout's tiles: the recent games, then the others in the library's order.
    auto rebuild_tiles = [&] {
        const auto saved_tab = library_tab;
        const auto saved_query = query;
        library_tab = 0;
        query.clear();
        rebuild_shown();
        sw_tiles.clear();
        for (const auto& path : recents) {
            for (const size_t k : shown) {
                if (sw_tiles.size() < SwitchTiles && scan.entries[k].launch_path == path &&
                    std::find(sw_tiles.begin(), sw_tiles.end(), k) == sw_tiles.end())
                    sw_tiles.push_back(k);
            }
        }
        for (const size_t k : shown) {
            if (sw_tiles.size() >= SwitchTiles) break;
            if (std::find(sw_tiles.begin(), sw_tiles.end(), k) == sw_tiles.end()) sw_tiles.push_back(k);
        }
        library_tab = saved_tab;
        query = saved_query;
        rebuild_shown();
        sw_tile = std::min(sw_tile, sw_tiles.size()); // the last tile is Todos os jogos
    };
    using winrt::Windows::Storage::StorageFolder;
    winrt::Windows::Foundation::IAsyncOperation<StorageFolder> picker{nullptr};
    std::future<std::string> validation;
    std::jthread validation_worker;
    std::string selected_path;
    bool resize = false, window_closed = false;
    auto size_changed = window.SizeChanged(winrt::auto_revoke, [&](auto&&, WindowSizeChangedEventArgs const& e) {
        const auto scale = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
        width = std::max(1U, static_cast<unsigned>(e.Size().Width * scale));
        height = std::max(1U, static_cast<unsigned>(e.Size().Height * scale));
        resize = true;
    });
    auto closed = window.Closed(winrt::auto_revoke, [&](auto&&, CoreWindowEventArgs const&) { window_closed = true; });
    auto visible = window.VisibilityChanged(winrt::auto_revoke, [&](auto&&, VisibilityChangedEventArgs const&) { dirty = true; });
    auto activation = window.Activated(winrt::auto_revoke, [&](auto&&, WindowActivatedEventArgs const& e) {
        if (e.WindowActivationState() == CoreWindowActivationState::Deactivated) {
            panel.keyboard.capturing = false; dirty = true;
        }
    });
    // Typing on a real keyboard while the name editor is open; KeyDown handles the rest.
    // The search keyboard (NS HUEHUE) takes typing the same way, and filters as it goes.
    std::optional<bool> finish_search;
    auto characters = window.CharacterReceived(winrt::auto_revoke, [&](auto&&, CharacterReceivedEventArgs const& e) {
        if (search_editor) {
            search_editor->Type(static_cast<wchar_t>(e.KeyCode()));
            sync_editor();
            dirty = true;
            e.Handled(true);
            return;
        }
        if (!system_page.name_editor) return;
        system_page.name_editor->Type(static_cast<wchar_t>(e.KeyCode()));
        system_page.editor_notice.clear();
        dirty = true;
        e.Handled(true);
    });
    auto keyboard = window.KeyDown(winrt::auto_revoke, [&](auto&&, KeyEventArgs const& e) {
        if (e.KeyStatus().WasKeyDown) return;
        if (search_editor) {
            using Command = TextEntry::Command;
            std::optional<Command> command;
            switch (e.VirtualKey()) {
            case VirtualKey::Escape: command = Command::Cancel; break;
            case VirtualKey::Enter: command = Command::Done; break;
            case VirtualKey::Back: command = Command::Delete; break;
            case VirtualKey::Up: command = Command::Up; break;
            case VirtualKey::Down: command = Command::Down; break;
            case VirtualKey::Left: command = Command::Left; break;
            case VirtualKey::Right: command = Command::Right; break;
            default: break;
            }
            if (command) {
                const auto result = search_editor->Apply(*command);
                sync_editor();
                if (result != TextEntry::Result::None) finish_search = result == TextEntry::Result::Done;
            }
            dirty = true;
            e.Handled(true);
            return;
        }
        if (system_page.name_editor) {
            using Command = TextEntry::Command;
            std::optional<Command> command;
            switch (e.VirtualKey()) {
            case VirtualKey::Escape: command = Command::Cancel; break;
            case VirtualKey::Enter: command = Command::Done; break;
            case VirtualKey::Back: command = Command::Delete; break;
            case VirtualKey::Up: command = Command::Up; break;
            case VirtualKey::Down: command = Command::Down; break;
            case VirtualKey::Left: command = Command::Left; break;
            case VirtualKey::Right: command = Command::Right; break;
            default: break; // Characters arrive through CharacterReceived.
            }
            if (command) {
                const auto result = system_page.name_editor->Apply(*command);
                if (result != TextEntry::Result::None) finish_name_editor = result == TextEntry::Result::Done;
            }
            dirty = true;
            e.Handled(true);
            return;
        }
        auto& editor = panel.keyboard;
        if (editor.open) {
            const auto key = e.VirtualKey();
            if (editor.capturing) {
                if (key != VirtualKey::Escape) {
                    if (AssignKeyboardKey(editor.bindings, editor.selected, static_cast<unsigned>(key))) save_keyboard();
                    else {
                        editor.notice = Tr(T::KeyUnsupported);
                        keyboard_notice_until = std::chrono::steady_clock::time_point::max();
                    }
                }
                editor.capturing = false;
                dirty = true;
            } else if (key == VirtualKey::Escape) { editor.open = false; dirty = true; }
            else if (key == VirtualKey::Tab) {
                editor.page = (editor.page + 1) % 3; editor.selected = KeyboardPageActions(editor.page).front(); dirty = true;
            } else if (key == VirtualKey::Enter) begin_capture();
            else if (key == VirtualKey::Delete) {
                AssignKeyboardKey(editor.bindings, editor.selected, 0); save_keyboard();
            } else if (key == VirtualKey::Up || key == VirtualKey::Down ||
                       key == VirtualKey::Left || key == VirtualKey::Right) {
                const auto items = KeyboardPageActions(editor.page);
                const size_t count = items.size();
                const size_t index = static_cast<size_t>(std::find(items.begin(), items.end(), editor.selected) - items.begin());
                const size_t step = key == VirtualKey::Up || key == VirtualKey::Down ? 2 : 1;
                const bool reverse = key == VirtualKey::Up || key == VirtualKey::Left;
                editor.selected = items[(index + (reverse ? count - step : step)) % count];
                dirty = true;
            }
            e.Handled(true);
            return;
        }
        switch (e.VirtualKey()) {
        case VirtualKey::Up: actions |= Up; break;
        case VirtualKey::Down: actions |= Down; break;
        case VirtualKey::Left: actions |= Left; break;
        case VirtualKey::Right: actions |= Right; break;
        case VirtualKey::F1: actions |= Panel; break;
        case VirtualKey::Escape: actions |= Quit; break;
        case VirtualKey::Enter: actions |= Play; break;
        case VirtualKey::X: actions |= library_top() || (home && switch_home()) ? Options : Swap; break;
        case VirtualKey::Y: actions |= library_top() || home ? Search : Refresh; break;
        case VirtualKey::R: actions |= Refresh; break;
        case VirtualKey::O: actions |= AddFolder; break;
        case VirtualKey::F2: actions |= Options; break;
        case VirtualKey::Q: actions |= PreviousTab; break;
        case VirtualKey::E: actions |= NextTab; break;
        default: break;
        }
        e.Handled(true);
    });
    auto pointer = window.PointerPressed(winrt::auto_revoke, [&](auto&&, PointerEventArgs const& e) {
        const auto p = e.CurrentPoint().Position();
        const auto dpi = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
        const auto scale = std::max(0.01f, std::min(width / 1280.0f, height / 720.0f));
        const float x = static_cast<float>((p.X * dpi - (width - 1280 * scale) / 2) / scale);
        const float y = static_cast<float>((p.Y * dpi - (height - 720 * scale) / 2) / scale);
        // The NS HUEHUE screens are laid out on 1920x1080: the same point there.
        const float dx = x * 1.5f, dy = y * 1.5f;
        if (search_editor) {
            using S = NsSearchLayout;
            if (const auto key = TextEntryPanelLayout::KeyAt(dx - S::PanelX(), dy - S::PanelY())) {
                search_editor->Focus(key->first, key->second);
                if (search_editor->Press() == TextEntry::Result::Done) finish_search = true;
                sync_editor();
                dirty = true;
            }
            e.Handled(true);
            return;
        }
        if (system_page.name_editor) {
            using L = SystemPageLayout;
            if (const auto key = TextEntryPanelLayout::KeyAt(x - L::EditorX, y - L::EditorY)) {
                auto& editor = *system_page.name_editor;
                editor.Focus(key->first, key->second);
                if (editor.Press() == TextEntry::Result::Done) finish_name_editor = true;
                system_page.editor_notice.clear();
                dirty = true;
            }
            e.Handled(true);
            return;
        }
        if (system_page.open && configuration.open && !settings) {
            using L = SystemPageLayout;
            if (y >= L::Footer - 7 && y < L::Footer + 40 && x >= L::RowX + L::RowWidth - 230 &&
                x < L::RowX + L::RowWidth) {
                actions |= Quit;
            } else if (x >= L::RowX && x < L::RowX + L::RowWidth && y >= L::RowTop) {
                const auto row = static_cast<size_t>((y - L::RowTop) / L::RowStride);
                if (row < SystemPage::RowCount && y < L::RowTop + row * L::RowStride + L::RowHeight) {
                    system_page.selected = row;
                    actions |= Play;
                    dirty = true;
                }
            }
            e.Handled(true);
            return;
        }
        if (configuration.open && !settings && !content_session) {
            // NS HUEHUE settings: the sections on the left, the file rows on the right.
            if (configuration.busy) {}
            else if (dx >= 96 && dx < 446 && dy >= 294 && dy < 294 + static_cast<float>(HomeRowCount) * 89.0f) {
                configuration.files = false;
                configuration.selected = std::min<size_t>(static_cast<size_t>((dy - 294) / 89), HomeRowCount - 1);
                actions |= Play; dirty = true;
            } else if (configuration.files && dx >= 521 && dx < 1801 && dy >= 356 && dy < 356 + 5 * 78.0f) {
                const auto choice = ConfigurationFirstRow(configuration) + static_cast<size_t>((dy - 356) / 78);
                if (choice < ConfigurationRowCount(configuration)) {
                    configuration.selected = choice; actions |= Play; dirty = true;
                }
            } else if (dy < 152 || dy > 996) actions |= Quit;
            e.Handled(true);
            return;
        }
        if (panel.keyboard.open) {
            auto& editor = panel.keyboard;
            if (Contains({1120, 112, 48, 40}, x, y)) {
                editor.open = editor.capturing = false; dirty = true;
            } else if (!editor.capturing) {
                if (Contains({635, 203, 514, 36}, x, y)) {
                    editor.page = std::min(2U, static_cast<unsigned>((x - 635) / 174));
                    editor.selected = KeyboardPageActions(editor.page).front(); dirty = true;
                } else if (Contains({635, 603, 155, 36}, x, y)) {
                    AssignKeyboardKey(editor.bindings, editor.selected, 0); save_keyboard();
                } else if (Contains({806, 603, 170, 36}, x, y)) {
                    editor.bindings = DefaultKeyboardBindings; save_keyboard();
                } else {
                    for (size_t i = 0; i < KeyboardButtonCount; ++i) {
                        if (Contains(KeyboardDiagramTile(i), x, y)) {
                            editor.page = KeyboardPageFor(i); editor.selected = i; begin_capture(); break;
                        }
                    }
                    for (const size_t i : KeyboardPageActions(editor.page)) {
                        if (!editor.capturing && Contains(KeyboardTileFor(i), x, y)) {
                            editor.selected = i; begin_capture(); break;
                        }
                    }
                }
            }
            e.Handled(true); return;
        }
        if (settings) {
            // NS HUEHUE controls page: rows of 76 (choices 70) from (1114, 392), 710 wide.
            if (panel.expanded) {
                const size_t first = ControllerChoiceFirst(panel);
                if (dx >= 1114 && dx < 1824 && dy >= 396) {
                    const size_t i = first + static_cast<size_t>((dy - 396) / 70);
                    if (i < panel.devices.size() + 2 && i < first + ControllerVisibleChoices) {
                        panel.choice = i;
                        actions |= Play;
                    } else actions |= Quit;
                } else actions |= Quit;
            } else if (dx >= 1114 && dx < 1824 && dy >= 396 && dy < 396 + (panel.xbox ? 4 : 6) * 76.0f) {
                setting_row = static_cast<unsigned>((dy - 396) / 76);
                if (!panel.xbox && setting_row == 5) {
                    panel.keyboard.open = true; panel.keyboard.capturing = false; dirty = true;
                } else actions |= Play;
            } else if (dy < 152 || dy > 996) actions |= Quit;
        } else if (home && !switch_home()) {
            // The home menu: five 500x70 buttons from (96, 444), 88 apart.
            if (dx >= 96 && dx < 596 && dy >= 431 && dy < 431 + NxHomeRows * 91.0f) {
                home_row = std::min(NxHomeRows - 1, static_cast<unsigned>((dy - 431) / 91));
                actions |= Play; dirty = true;
            }
        } else if (dx >= 1414 && dx < 1824 && dy >= 268 && dy < 329) {
            actions |= Search;
        } else if (dx >= 96 && dx < 1000 && dy >= 285 && dy < 344) {
            // The tabs (their widths follow the text; about 200 each is close enough to pick one).
            library_tab = std::min(3U, static_cast<unsigned>((dx - 96) / 200));
            library_focus = -1; metadata_dirty = true; dirty = true;
        } else if (!loading && dx >= 96 && dx < 1824 && dy >= 376 && dy < 376 + 2 * 319.0f) {
            // The grid: two rows of five 323x291 cards, 351 / 319 apart.
            rebuild_shown();
            const size_t column = std::min<size_t>(static_cast<size_t>((dx - 96) / 351.2f), PageSize - 1);
            const size_t row = dy < 681 ? 0 : 1;
            const size_t cursor = selected < position_of.size() && position_of[selected] != SIZE_MAX ? position_of[selected] : 0;
            const size_t at = cursor / PageVisible * PageVisible + row * PageSize + column;
            if (at < shown.size()) {
                if (shown[at] == selected) actions |= Play;
                selected = shown[at]; library_focus = -1; dirty = true;
            }
        }
        e.Handled(true);
    });
    auto wheel = window.PointerWheelChanged(winrt::auto_revoke, [&](auto&&, PointerEventArgs const& e) {
        if (settings && panel.expanded && !panel.keyboard.open) {
            const int delta = e.CurrentPoint().Properties().MouseWheelDelta();
            const size_t count = panel.devices.size() + 2;
            if (delta > 0 && panel.choice) --panel.choice;
            if (delta < 0 && panel.choice + 1 < count) ++panel.choice;
            dirty = true;
            e.Handled(true);
        }
    });
    std::future<LibraryScan> future;
    std::jthread scanner;
    struct MetadataBatch {
        std::vector<LibraryEntry> entries;
        unsigned generation{};
        bool icons{};      // The window around the cursor.
        bool stopped{};    // Interrupted: some entries were not read.
    };
    bool metadata_background = false; // The running job is a names or fill batch.
    std::future<MetadataBatch> metadata;
    std::jthread metadata_worker;
    unsigned generation = 0;
    bool logging_ready = false;
    auto refresh_setup = [&] {
        if (setup_future.valid()) return;
        std::packaged_task<FileSetupStatus()> task{[] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Uninitialize { ~Uninitialize() { winrt::uninit_apartment(); } } guard;
            return ReadFileSetupStatus();
        }};
        setup_future = task.get_future();
        setup_worker = std::jthread(std::move(task));
    };
    // Forget what the entries know and read it again (cache first): after the game language
    // changed or the cache was cleared. Results in flight belong to the old generation.
    auto reload_metadata = [&] {
        ++generation;
        metadata_worker.request_stop();
        for (auto& entry : scan.entries) {
            entry.metadata_loaded = entry.title_loaded = false;
            entry.display_version.clear();
            std::vector<unsigned char>{}.swap(entry.icon);
        }
        metadata_dirty = true;
        retained_page = SIZE_MAX;
        names_cursor = fill_cursor = 0;
        dirty = true;
    };
    auto start_scan = [&](StorageFolder added = nullptr) {
        loading = dirty = true;
        ++generation;
        metadata_dirty = true;
        retained_page = SIZE_MAX;
        names_cursor = fill_cursor = 0;
        metadata_worker.request_stop();
        canvas->InvalidateCovers();
        std::packaged_task<LibraryScan(std::stop_token)> task{[&, added](std::stop_token stop) {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Uninitialize { ~Uninitialize() { winrt::uninit_apartment(); } } guard;
            seed();
            // Recover interrupted publication before a metadata worker constructs NCA readers.
            ReadFileSetupStatus();
            std::error_code ec;
            std::filesystem::create_directories(root, ec);
            auto result = ScanGameLibrary(root, stop);
            const std::wstring internal{Tr(T::SourceInternal)};
            for (auto& entry : result.entries) {
                const auto path = (root / entry.relative_path).u8string();
                entry.launch_path.assign(reinterpret_cast<const char*>(path.data()), path.size());
                entry.source_name = internal;
            }
            // NS HUEHUE: updates and DLC dropped in the games folder install by themselves and
            // leave the list of games (the metadata reader has not started yet: one key user).
            if (HasUsableHeaderKey() && !stop.stop_requested()) {
                std::vector<std::string> packages;
                for (const auto& entry : result.entries) {
                    if (entry.relative_path.extension() == ".nsp" || entry.relative_path.extension() == ".NSP")
                        packages.push_back(entry.launch_path);
                }
                try {
                    auto imported = Content::AutoImportPackages(packages, ListPath(L"ns_content_scan.tsv"), stop);
                    result.content_added = imported.added;
                    std::erase_if(result.entries, [&](const LibraryEntry& entry) {
                        return std::find(imported.content_files.begin(), imported.content_files.end(),
                                         entry.launch_path) != imported.content_files.end();
                    });
                } catch (const std::exception& e) {
                    LOG_ERROR(Frontend, "Content: auto-import failed: {}", e.what());
                }
            }
            try {
                if (added) RememberGameFolder(added);
                auto external = ScanExternalGameFolders(stop);
                if (!external.error.empty()) {
                    if (!result.error.empty()) result.error += " | ";
                    result.error += external.error;
                }
                result.limited |= external.limited;
                result.entries.insert(result.entries.end(),
                    std::make_move_iterator(external.entries.begin()), std::make_move_iterator(external.entries.end()));
            } catch (const winrt::hresult_error& e) {
                result.error = I18n::ToUtf8(I18n::Format(T::FolderAuthFailed, {e.message()}));
            } catch (const std::exception& e) { result.error = e.what(); }
            std::sort(result.entries.begin(), result.entries.end(), [](const auto& a, const auto& b) {
                return a.launch_path < b.launch_path;
            });
            return result;
        }};
        future = task.get_future();
        scanner = std::jthread(std::move(task));
    };
    // Saves and applies the System page; the interface switches language on the next frame.
    auto apply_system_effect = [&](SystemPageEffect effect, std::chrono::steady_clock::time_point now) {
        switch (effect) {
        case SystemPageEffect::Changed:
        case SystemPageEffect::NameDone: {
            const bool saved = SaveSystemSettings(system_page.settings);
            const auto resolved = ApplySystemSettings();
            system_page.notice = Tr(saved ? T::SettingsSaved : T::SystemSaveFailed);
            system_notice_until = saved ? now + std::chrono::seconds{3} : std::chrono::steady_clock::time_point::max();
            if (resolved.game_language != title_language) {
                title_language = resolved.game_language;
                reload_metadata();
            }
            break;
        }
        case SystemPageEffect::ClearCache: {
            reload_metadata();
            canvas->InvalidateCovers();
            // The mods store's pictures, index and unfinished downloads go with it.
            std::error_code ec;
            std::filesystem::remove_all(Mods::Web::CacheFolder(), ec);
            system_page.notice = Tr(cache.Clear() ? T::LibraryCacheCleared : T::SystemSaveFailed);
            system_notice_until = now + std::chrono::seconds{3};
            break;
        }
        case SystemPageEffect::OpenThemes:
            themes_page = true;
            theme_previewing = false;
            themes_card = active_card();
            themes_wallpaper = system_page.settings.wallpaper ? 1 : 0;
            themes_focus = 0;
            themes_notice.clear();
            break;
        case SystemPageEffect::None:
        case SystemPageEffect::EditName:
            break;
        }
        dirty = true;
    };
    start_scan();
    GamepadButtons previous{};
    StickNavigation stick_navigation;
    unsigned navigation_context = 0;
    std::optional<ControllerDevice> pad;
    ControllerCatalog<ControllerDevice> controllers;
    auto library_say = [&](std::wstring text, std::chrono::steady_clock::time_point until) {
        notice = std::move(text);
        library_notice_until = until;
        dirty = true;
    };
    auto remember_recent = [&](const std::string& path) {
        recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());
        recents.insert(recents.begin(), path);
        if (recents.size() > RecentLimit) recents.resize(RecentLimit);
        SaveList(L"ns_recent.txt", recents);
    };
    // The sections: 1 Jogos, 2 Configuracoes, 3 Controles (0 is the home screen, which B returns to).
    auto go_section = [&](unsigned section) {
        online_page = false;
        saves_page = false;
        settings = section == 3;
        return_to_configuration = false;
        panel.expanded = false;
        panel.keyboard.open = panel.keyboard.capturing = false;
        if (section == 2) {
            configuration.open = true; configuration.files = false; configuration.selected = HomeSystem;
            configuration.notice.clear(); refresh_setup();
        } else {
            configuration.open = false; configuration.files = false;
        }
        if (section <= 1) home = section == 0;
        if (section == 1) library_focus = -1;
        dirty = true;
    };
    // Jogos, Favoritos and Recentes of the home: the library on that tab, the cursor on its button.
    auto open_library = [&](unsigned tab) {
        go_section(1);
        library_tab = tab;
        library_focus = static_cast<int>(tab);
        metadata_dirty = true;
    };
    // Sair do NS: with a saves folder, ask about a last copy first.
    auto request_exit = [&] {
        exit_prompt = saves_folder ? 1 : 3;
        dirty = true;
    };
    auto start_saves = [&](SavesJob job, winrt::Windows::Storage::StorageFolder folder) {
        if (saves_future.valid()) return;
        saves_job = job;
        saves_confirm = 0;
        saves_shown_done = 0;
        std::packaged_task<SavesDone(std::stop_token)> task{[job, folder, &saves_progress](std::stop_token stop) {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Uninitialize { ~Uninitialize() { winrt::uninit_apartment(); } } guard;
            SavesDone done;
            try {
                switch (job) {
                case SavesJob::Pick:
                    // A folder that already holds another installation's copy (before a reinstall)
                    // is offered for Restaurar, never written over.
                    SaveBackup::RememberFolder(folder);
                    done.info = SaveBackup::Inspect();
                    if (done.info.has_backup && done.info.other_install) {
                        // Its saves come in at once (the player asked, 19/10/2026): Restore keeps
                        // this console's first and makes the folder this installation's.
                        done.result = SaveBackup::Restore(saves_progress, stop);
                        done.restored = true;
                    } else {
                        done.result = SaveBackup::Backup(false, saves_progress, stop);
                    }
                    break;
                case SavesJob::Restore: done.result = SaveBackup::Restore(saves_progress, stop); break;
                case SavesJob::SettingsBackup: done.result = SaveBackup::BackupSettings(); break;
                case SavesJob::SettingsRestore: done.result = SaveBackup::RestoreSettings(); break;
                default: done.result = SaveBackup::Backup(job == SavesJob::Replace, saves_progress, stop); break;
                }
            } catch (const winrt::hresult_error& e) {
                done.result.outcome = SaveBackup::Outcome::Failed;
                done.result.error = winrt::to_string(e.message());
            } catch (const std::exception& e) {
                done.result.outcome = SaveBackup::Outcome::Failed;
                done.result.error = e.what();
            }
            return done;
        }};
        saves_future = task.get_future();
        saves_worker = std::jthread(std::move(task));
        dirty = true;
    };
    // No automatic copy any more (19/10/2026): the copy is made from Saves (USB) or on leaving
    // through Sair do NS.
    while (true) {
        window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        const auto now = std::chrono::steady_clock::now();
        if (!panel.keyboard.notice.empty() && now >= keyboard_notice_until) {
            panel.keyboard.notice.clear(); dirty = true;
        }
        if (!panel.notice.empty() && now >= controller_notice_until) {
            panel.notice.clear(); dirty = true;
        }
        if (!system_page.notice.empty() && now >= system_notice_until) {
            system_page.notice.clear(); dirty = true;
        }
        if (!notice.empty() && now >= library_notice_until) {
            notice.clear(); library_notice_until = std::chrono::steady_clock::time_point::max(); dirty = true;
        }
        if (!options_notice.empty() && now >= options_notice_until) {
            options_notice.clear(); dirty = true;
        }
        if (panel.keyboard.capturing && now >= capture_until) {
            panel.keyboard.capturing = false; dirty = true;
        }
        const bool catalog_changed = controllers.Update(now, [] {
            RefreshProControllers();
            return EnumerateControllers();
        }, SameControllerList);
        if (catalog_changed) {
            panel.devices = controllers.Devices();
            panel.choice = std::min(panel.choice, panel.devices.size() + 1);
            // An open list must not apply a stale row after hotplug/reorder.
            if (panel.expanded) {
                panel.expanded = false;
                actions &= ~Play;
            }
            dirty = true;
        }
        // Selection follows settings immediately; discovery remains rate limited.
        const auto next = controllers.Select(options.controller_id);
        if (bool(pad) != bool(next) || (pad && next &&
            (pad->id != next->id || pad->pad != next->pad || pad->pro != next->pro))) {
            previous = {}; stick_navigation.Reset();
        }
        if (catalog_changed || !pad || !next || pad->id != next->id)
            pad = next ? std::optional<ControllerDevice>{*next} : std::nullopt;
        const unsigned context = game_options.open ? (cheat_session ? 8 : mod_session ? 9 : 7) :
            system_page.name_editor ? 6 : system_page.open ? 5 :
            panel.keyboard.open ? 3 : panel.expanded ? 2 : settings ? 1 : configuration.open ? 4 : 0;
        if (context != navigation_context) {
            stick_navigation.Reset(); navigation_context = context;
        }
        if (pad) {
            try {
                const auto reading = ReadController(*pad).gamepad;
                const auto buttons = reading.Buttons;
                // Track physical edges before mapping: changing the option while holding a
                // button must not synthesize a second press of its new logical counterpart.
                const auto edges = MapFaceButtons(buttons & ~previous,
                    SwapControllerFaceButtons(*pad, options.swap_face_buttons));
                previous = buttons;
                auto hit = [&](GamepadButtons b) { return (edges & b) == b; };
                // The d-pad takes precedence; do not combine opposite directions.
                constexpr auto dpad = GamepadButtons::DPadUp | GamepadButtons::DPadDown |
                                      GamepadButtons::DPadLeft | GamepadButtons::DPadRight;
                unsigned directions = 0;
                if ((buttons & dpad) != GamepadButtons::None) stick_navigation.Reset();
                else {
                    switch (stick_navigation.Update(reading.LeftThumbstickX, reading.LeftThumbstickY, now)) {
                    case StickDirection::Up: directions |= Up; break;
                    case StickDirection::Down: directions |= Down; break;
                    case StickDirection::Left: directions |= Left; break;
                    case StickDirection::Right: directions |= Right; break;
                    case StickDirection::None: break;
                    }
                }
                if (hit(GamepadButtons::DPadUp)) directions |= Up;
                if (hit(GamepadButtons::DPadDown)) directions |= Down;
                if (hit(GamepadButtons::DPadLeft)) directions |= Left;
                if (hit(GamepadButtons::DPadRight)) directions |= Right;
                if (search_editor) {
                    // The same keyboard as the name: A types, X deletes, Y space, Menu done, B cancels.
                    const std::pair<bool, MenuAction> presses[] = {
                        {(directions & Up) != 0, MenuAction::Up},
                        {(directions & Down) != 0, MenuAction::Down},
                        {(directions & Left) != 0, MenuAction::Left},
                        {(directions & Right) != 0, MenuAction::Right},
                        {hit(GamepadButtons::A), MenuAction::Confirm},
                        {hit(GamepadButtons::X), MenuAction::Alternate},
                        {hit(GamepadButtons::Y), MenuAction::Refresh},
                        {hit(GamepadButtons::View) || hit(GamepadButtons::LeftShoulder), MenuAction::PreviousTab},
                        {hit(GamepadButtons::Menu) || hit(GamepadButtons::RightShoulder), MenuAction::NextTab},
                        {hit(GamepadButtons::B), MenuAction::Back},
                    };
                    for (const auto& [pressed, action] : presses) {
                        if (!pressed || finish_search) continue;
                        const auto result = search_editor->Apply(*TextEntryCommand(action));
                        if (result != TextEntry::Result::None) finish_search = result == TextEntry::Result::Done;
                    }
                    sync_editor();
                    if (directions || edges != GamepadButtons::None) dirty = true;
                } else if (system_page.name_editor) {
                    // Like the console's keyboard: A types, X deletes, Y adds a space, Menu (or RB)
                    // is done, View (or LB) is Shift and B leaves without saving. The bumpers are
                    // what a game's keyboard uses, where + and - belong to the game menu.
                    auto& editor = *system_page.name_editor;
                    const std::pair<bool, MenuAction> presses[] = {
                        {(directions & Up) != 0, MenuAction::Up},
                        {(directions & Down) != 0, MenuAction::Down},
                        {(directions & Left) != 0, MenuAction::Left},
                        {(directions & Right) != 0, MenuAction::Right},
                        {hit(GamepadButtons::A), MenuAction::Confirm},
                        {hit(GamepadButtons::X), MenuAction::Alternate},
                        {hit(GamepadButtons::Y), MenuAction::Refresh},
                        {hit(GamepadButtons::View) || hit(GamepadButtons::LeftShoulder), MenuAction::PreviousTab},
                        {hit(GamepadButtons::Menu) || hit(GamepadButtons::RightShoulder), MenuAction::NextTab},
                        {hit(GamepadButtons::B), MenuAction::Back},
                    };
                    for (const auto& [pressed, action] : presses) {
                        if (!pressed || finish_name_editor) continue;
                        const auto result = editor.Apply(*TextEntryCommand(action));
                        if (result != TextEntry::Result::None) finish_name_editor = result == TextEntry::Result::Done;
                    }
                    if (directions || edges != GamepadButtons::None) {
                        system_page.editor_notice.clear();
                        dirty = true;
                    }
                } else {
                    // Back must remain available while editing/capturing keyboard input.
                    // Track all edges, but suppress other pad actions inside that modal.
                    if (hit(GamepadButtons::B)) actions |= Quit;
                    if (!panel.keyboard.open) {
                        actions |= directions;
                        if (hit(GamepadButtons::A)) actions |= Play;
                        // The Switch home's tiles take X (Opcoes) and Y (Buscar) as the grid does.
                        const bool tiles = home && switch_home() && !configuration.open && !settings &&
                                           !game_options.open && !themes_page;
                        const bool top = library_top() || tiles;
                        if (hit(GamepadButtons::X)) {
                            if (settings || content_session || missing_content) actions |= Swap;
                            else if (top) actions |= Options; // NS HUEHUE: X Opcoes
                            else if (!configuration.open && !home) actions |= AddFolder;
                        }
                        if (hit(GamepadButtons::Y)) actions |= top || home ? Search : Refresh; // Y Buscar
                        if (hit(GamepadButtons::Menu) && !home) actions |= top ? Refresh : Options;
                        if (hit(GamepadButtons::LeftShoulder)) actions |= PreviousTab;
                        if (hit(GamepadButtons::RightShoulder)) actions |= NextTab;
                    }
                }
            } catch (...) {
                controllers.ReadFailed();
                pad.reset(); previous = {}; stick_navigation.Reset();
            }
        }
        if (window_closed || QuitRequested()) return std::nullopt;
        if (finish_name_editor) {
            apply_system_effect(FinishNameEditor(system_page, *finish_name_editor), now);
            finish_name_editor.reset();
            dirty = true;
        }
        if (finish_search && edit_target != EditTarget::Search) {
            // An online field: Done keeps what was typed, B leaves it as it was.
            if (*finish_search && search_editor) {
                const std::string text = I18n::ToUtf8(search_editor->Value());
                if (edit_target == EditTarget::Server) online.server = text;
                else if (edit_target == EditTarget::Password) online.password = text;
                else {
                    try {
                        const auto port = std::stoul(text);
                        if (port > 0 && port <= 65535) online.port = static_cast<std::uint16_t>(port);
                    } catch (...) {}
                }
                library_say(std::wstring{Tr(SaveOnlineSettings(online) ? T::SettingsSaved : T::SystemSaveFailed)},
                            now + std::chrono::seconds{3});
            }
            search_editor.reset();
            finish_search.reset();
            edit_target = EditTarget::Search;
            dirty = true;
        }
        if (finish_search) {
            // Done keeps the filter; B puts back the one from before.
            if (!*finish_search) query = search_before;
            search_editor.reset();
            finish_search.reset();
            library_focus = -1;
            metadata_dirty = true;
            dirty = true;
        }
        if (search_editor) actions = 0;
        if (picker) {
            actions = 0; // The system picker owns input until it returns.
            if (picker.Status() != winrt::Windows::Foundation::AsyncStatus::Started) {
                try {
                    auto folder = picker.GetResults();
                    if (folder && picker_kind == 0) start_scan(folder);
                    else if (folder && picker_kind == 3 && content_session) content_session->AuthorizeFolder(folder);
                    else if (folder && picker_kind == 4 && mod_session) mod_session->AddFolder(folder);
                    else if (folder && picker_kind == 40 && mod_session) mod_session->SetDownloadFolder(folder);
                    else if (folder && picker_kind == 5) start_saves(SavesJob::Pick, folder);
                    else if (folder && picker_kind == 6 && !themes_import.valid()) {
                        std::packaged_task<ThemeImportResult(std::stop_token)> task{[folder](std::stop_token stop) {
                            winrt::init_apartment(winrt::apartment_type::multi_threaded);
                            struct Uninitialize { ~Uninitialize() { winrt::uninit_apartment(); } } guard;
                            return ImportThemePacks(folder, stop);
                        }};
                        themes_import = task.get_future();
                        themes_import_worker = std::jthread(std::move(task));
                        themes_notice = Tr(T::NsThemesImporting);
                        themes_notice_until = std::chrono::steady_clock::time_point::max();
                    }
                    else if (picker_kind >= 3) {} // A page's folder whose page closed meanwhile.
                    else if (folder) {
                        configuration.busy = true;
                        configuration.notice.clear();
                        import_progress.completed = 0;
                        import_progress.total = 0;
                        metadata_worker.request_stop();
                        std::packaged_task<std::string(std::stop_token)> task{
                            [folder, kind = picker_kind, old_metadata = std::move(metadata_worker), &import_progress](std::stop_token stop) mutable {
                                if (old_metadata.joinable()) old_metadata.join();
                                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                                struct Uninitialize { ~Uninitialize() { winrt::uninit_apartment(); } } guard;
                                return ImportSystemFiles(folder, kind == 1 ? FileImport::Keys : FileImport::Firmware, import_progress, stop,
                                                         kind == 2);
                            }};
                        import_future = task.get_future();
                        import_worker = std::jthread(std::move(task));
                    } else { configuration.notice = Tr(T::SelectionCancelled); dirty = true; }
                } catch (const winrt::hresult_error& e) {
                    configuration.notice = I18n::Format(T::FolderPickFailed, {e.message()}); dirty = true;
                }
                picker = nullptr;
                previous = {};
                stick_navigation.Reset();
            }
        }
        if (missing_content) {
            if (actions & Quit) { missing_content = false; actions = 0; }
            else if (actions & Swap) {
                Content::AllowMissingForNextLaunch(selected_path);
                remember_recent(selected_path);
                return selected_path;
            }
            else if (actions & Play) missing_content = false;
            else actions = 0;
            dirty = true;
        }
        if (validation.valid()) {
            actions = 0;
            if (validation.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
                try {
                    const auto error = validation.get();
                    if (error.empty()) { remember_recent(selected_path); return selected_path; }
                    if (error == "content-missing") missing_content = true;
                    else notice = Wide(error);
                } catch (const std::exception& e) { notice = Wide(e.what()); }
                dirty = true;
            }
        }
        if (setup_future.valid() && setup_future.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
            try { configuration.status = setup_future.get(); }
            catch (...) { configuration.notice = Tr(T::SetupCheckFailed); }
            configuration.selected = std::min(configuration.selected, ConfigurationRowCount(configuration) - 1);
            dirty = true;
        }
        if (configuration.busy) {
            if (actions & Quit) { import_worker.request_stop(); configuration.notice = Tr(T::CancellingImport); }
            actions = 0;
            const auto done = import_progress.completed.load(std::memory_order_relaxed);
            const auto total = import_progress.total.load(std::memory_order_relaxed);
            if (done != configuration.completed || total != configuration.total) {
                configuration.completed = done; configuration.total = total; dirty = true;
            }
            if (import_future.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
                try {
                    const auto error = import_future.get();
                    if (error == FirmwareStagedMarker) {
                        // Copied and validated: say what it is against the installed one; A installs.
                        firmware_staged = TakeStagedFirmware();
                        if (firmware_staged) {
                            const auto& candidate = firmware_staged->version;
                            const auto& now_installed = configuration.status.firmware;
                            const std::wstring next_name = candidate.known ? Wide(candidate.display) : std::wstring{L"?"};
                            const std::wstring now_name = now_installed.known ? Wide(now_installed.display) : std::wstring{};
                            const std::wstring size = std::to_wstring(firmware_staged->files) + L" · " +
                                                      std::to_wstring(firmware_staged->bytes >> 20) + L" MB";
                            if (!now_installed.known || configuration.status.firmware_files == 0)
                                configuration.notice = I18n::Format(T::NsFirmwareConfirmInstall, {next_name, size});
                            else if (!candidate.known || candidate.Packed() > now_installed.Packed())
                                configuration.notice = I18n::Format(T::NsFirmwareConfirmUpdate, {now_name, next_name, size});
                            else if (candidate.Packed() == now_installed.Packed())
                                configuration.notice = I18n::Format(T::NsFirmwareConfirmReinstall, {next_name});
                            else
                                configuration.notice = I18n::Format(T::NsFirmwareConfirmDowngrade, {now_name, next_name});
                            if (firmware_staged->HasWarnings()) {
                                // The checks only warn: everything was copied and A still installs it.
                                std::wstring found;
                                const auto add = [&found](std::wstring part) {
                                    if (!found.empty()) found += L"; ";
                                    found += part;
                                };
                                if (firmware_staged->foreign)
                                    add(I18n::Format(T::NsFirmwareWarnForeign, {std::to_wstring(firmware_staged->foreign)}));
                                if (firmware_staged->unreadable)
                                    add(I18n::Format(T::NsFirmwareWarnUnreadable, {std::to_wstring(firmware_staged->unreadable)}));
                                if (firmware_staged->incomplete) add(std::wstring{Tr(T::NsFirmwareWarnIncomplete)});
                                if (firmware_staged->copy_failed)
                                    add(I18n::Format(T::NsFirmwareWarnCopy, {std::to_wstring(firmware_staged->copy_failed)}));
                                configuration.notice = I18n::Format(T::NsFirmwareWarn, {found}) + L" " + configuration.notice;
                            }
                        } else {
                            configuration.notice = Tr(T::ImportFailedKept);
                        }
                    } else if (firmware_job == 1) {
                        configuration.notice = error.empty() ? std::wstring{Tr(T::NsFirmwareInstalled)} : Wide(error);
                    } else if (firmware_job == 2) {
                        configuration.notice = error.empty() ? std::wstring{Tr(T::NsFirmwareRestored)} : Wide(error);
                    } else {
                        configuration.notice = error.empty() ? std::wstring{Tr(T::ImportDone)} : Wide(error);
                    }
                } catch (...) { configuration.notice = Tr(T::ImportFailedKept); }
                firmware_job = 0;
                configuration.busy = false;
                // The old metadata worker has joined; discard results before rescanning.
                metadata = {};
                refresh_setup();
                start_scan();
                dirty = true;
            }
        }
        if (!saves_notice.empty() && now >= saves_notice_until) {
            saves_notice.clear();
            saves_notice_until = std::chrono::steady_clock::time_point::max();
            dirty = true;
        }
        if (saves_confirm && now >= saves_confirm_until) {
            saves_confirm = 0;
            saves_notice.clear();
            dirty = true;
        }
        if (saves_future.valid()) {
            if (const auto done = saves_progress.done.load(); done != saves_shown_done) {
                saves_shown_done = done;
                dirty = true;
            }
            if (saves_future.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
                SavesDone done;
                try { done = saves_future.get(); } catch (...) { done.result.outcome = SaveBackup::Outcome::Failed; }
                saves = SaveBackup::LoadSettings();
                saves_folder = SaveBackup::HasFolder();
                using O = SaveBackup::Outcome;
                const auto& result = done.result;
                const auto number = [](std::uint32_t n) { return std::to_wstring(n); };
                std::wstring text;
                if (saves_job == SavesJob::Automatic) {
                    // Said in the library (only what needs the player: nothing when nothing changed).
                    if (result.outcome == O::Done && result.copied) text = I18n::Format(T::NsSavesAutoDone, {number(result.copied)});
                    else if (result.outcome == O::OtherInstall) text = Tr(T::NsSavesAutoOther);
                    else if (result.outcome == O::FolderUnavailable) text = Tr(T::NsSavesAutoUnavailable);
                    else if (result.outcome == O::Failed) text = I18n::Format(T::NsSavesFailed, {Wide(result.error)});
                    if (!text.empty() && notice.empty()) library_say(text, now + std::chrono::seconds{6});
                } else {
                    switch (result.outcome) {
                    case O::Done:
                        if (saves_job == SavesJob::SettingsBackup) {
                            text = Tr(T::NsConfigSaved);
                            break;
                        }
                        if (saves_job == SavesJob::SettingsRestore) {
                            // The copy's settings: everything the library holds is read again.
                            text = Tr(T::NsConfigRestored);
                            system_page.settings = LoadSystemSettings();
                            ApplySystemSettings();
                            options = LoadControllerOptions();
                            if (panel.xbox) options.controller_id.clear();
                            favorites = LoadList(L"ns_favorites.txt");
                            recents = LoadList(L"ns_recent.txt");
                            online = LoadOnlineSettings();
                            theme_packs = LoadThemePacks();
                            metadata_dirty = true;
                            break;
                        }
                        text = saves_job == SavesJob::Restore || done.restored
                                   ? I18n::Format(T::NsSavesRestored, {number(result.games), number(result.copied)})
                               : result.copied ? I18n::Format(T::NsSavesDone, {number(result.copied), number(result.games)})
                                               : std::wstring{Tr(T::NsSavesUpToDate)};
                        break;
                    case O::OtherInstall:
                        if (saves_job == SavesJob::Pick) {
                            text = I18n::Format(T::NsSavesFolderFound,
                                                {number(result.games), SaveBackup::FormatTime(done.info.time)});
                            saves_row = 2; // Restaurar saves da pasta
                        } else {
                            text = I18n::Format(T::NsSavesOtherInstall, {number(result.games)});
                            saves_confirm = 2;
                            saves_confirm_until = now + std::chrono::seconds{10};
                        }
                        break;
                    case O::NothingToRestore:
                        text = Tr(saves_job == SavesJob::SettingsRestore ? T::NsConfigNothing : T::NsSavesNothing);
                        break;
                    case O::FolderUnavailable: text = Tr(T::NsSavesUnavailable); break;
                    case O::NoFolder: text = Tr(T::NsSavesNoFolder); break;
                    case O::Stopped: break;
                    case O::Failed: text = I18n::Format(T::NsSavesFailed, {Wide(result.error)}); break;
                    }
                    saves_notice = std::move(text);
                    saves_notice_until = saves_confirm ? std::chrono::steady_clock::time_point::max()
                                                       : now + std::chrono::seconds{12};
                }
                if (exit_prompt == 2) {
                    // Sair do NS: copied, the app closes; a copy that failed keeps it open and says why.
                    if (result.outcome == O::Done || result.outcome == O::Stopped) return std::nullopt;
                    exit_prompt = 0;
                    library_say(saves_notice.empty() ? std::wstring{Tr(T::NsSavesFailedExit)}
                                                     : saves_notice + L" " + std::wstring{Tr(T::NsSavesFailedExit)},
                                now + std::chrono::seconds{10});
                }
                saves_job = SavesJob::None;
                dirty = true;
            }
        }
        if (!themes_notice.empty() && now >= themes_notice_until) {
            themes_notice.clear();
            themes_notice_until = std::chrono::steady_clock::time_point::max();
            dirty = true;
        }
        if (themes_import.valid() && themes_import.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
            ThemeImportResult result;
            try { result = themes_import.get(); } catch (...) { result.error = "?"; }
            theme_packs = LoadThemePacks();
            themes_card = std::min<unsigned>(themes_card, static_cast<unsigned>(ThemeCount + theme_packs.size()) - 1);
            themes_notice = !result.error.empty() ? Wide(result.error)
                          : I18n::Format(T::NsThemesImported, {std::to_wstring(result.imported), std::to_wstring(result.skipped)});
            themes_notice_until = now + std::chrono::seconds{8};
            dirty = true;
        }
        if (remap_open && actions) {
            // Mapear botoes: Up/Down a Switch button, Left/Right (or A) its Xbox control; the
            // last rows reset it (and, for a game, go back to the saved mapping). Saved at once.
            const size_t count = RemapCount + (remap_game ? 2 : 1);
            if (actions & Up) remap_row = (remap_row + count - 1) % count;
            if (actions & Down) remap_row = (remap_row + 1) % count;
            bool changed = false;
            if (actions & (Left | Right | Play)) {
                if (remap_row < RemapCount) {
                    remap_edit[remap_row] = StepRemapSource(remap_edit[remap_row], (actions & Left) != 0);
                    remap_own = true;
                    changed = true;
                } else if (actions & Play) {
                    if (remap_row == RemapCount) {
                        remap_edit = IdentityRemap();
                        remap_own = true;
                    } else {
                        remap_edit = options.remap;
                        remap_own = false;
                    }
                    changed = true;
                }
            }
            if (changed) {
                bool saved;
                if (remap_game) {
                    auto game_tuning = LoadGameTuning(remap_path);
                    game_tuning.remap = remap_own ? std::optional<ButtonRemap>{remap_edit} : std::nullopt;
                    saved = SaveGameTuning(remap_path, game_tuning);
                } else {
                    options.remap = remap_edit;
                    saved = SaveControllerOptions(options);
                }
                options_notice = Tr(saved ? T::SettingsSaved : T::SystemSaveFailed);
                options_notice_until = now + std::chrono::seconds{3};
            }
            if (actions & Quit) remap_open = false;
            actions = 0;
            dirty = true;
        }
        if (themes_page && actions) {
            // Temas: Left/Right pick the candidate, Down reaches Previa / Aplicar tema / the ground.
            // Focusing or previewing saves nothing; Aplicar keeps the previous theme on failure.
            const unsigned cards = static_cast<unsigned>(ThemeCount + theme_packs.size());
            const auto card_name = [&](unsigned card) -> std::wstring {
                if (card < ThemeCount) return std::wstring{Tr(ThemeName(static_cast<ThemeId>(card)))};
                return theme_packs[card - ThemeCount].name;
            };
            const auto apply = [&] {
                const auto before = system_page.settings;
                if (themes_card < ThemeCount) {
                    system_page.settings.theme = static_cast<std::uint8_t>(themes_card);
                    system_page.settings.theme_pack.clear();
                } else {
                    const auto& pack = theme_packs[themes_card - ThemeCount];
                    system_page.settings.theme = static_cast<std::uint8_t>(pack.base);
                    system_page.settings.theme_pack = pack.id;
                }
                system_page.settings.wallpaper = static_cast<std::uint8_t>(themes_wallpaper);
                if (SaveSystemSettings(system_page.settings)) {
                    themes_page = theme_previewing = false;
                    system_page.open = false;
                    configuration.open = false;
                    configuration.files = false;
                    home = true;
                    sw_row = 0;
                    library_say(I18n::Format(T::NsThemeApplied, {card_name(themes_card)}), now + std::chrono::seconds{3});
                } else {
                    system_page.settings = before;
                    theme_previewing = false;
                    themes_notice = Tr(T::NsThemeFailed);
                    themes_notice_until = now + std::chrono::seconds{6};
                }
            };
            if (theme_previewing) {
                if (actions & Play) apply();
                else if (actions & Quit) theme_previewing = false;
            } else if (actions & Quit) {
                themes_page = false;
            } else if (themes_focus == 0) {
                if (actions & Left) themes_card = (themes_card + cards - 1) % cards;
                if (actions & Right) themes_card = (themes_card + 1) % cards;
                if (actions & (Down | Play)) themes_focus = 1;
            } else {
                if ((actions & Left) && themes_focus > 1) --themes_focus;
                if ((actions & Right) && themes_focus < 4) ++themes_focus;
                if (actions & Up) themes_focus = 0;
                if (actions & Play) {
                    if (themes_focus == 1) theme_previewing = true;
                    else if (themes_focus == 2) apply();
                    else if (themes_focus == 3) themes_wallpaper = themes_wallpaper ? 0 : 1;
                    else if (!themes_import.valid() && !picker) {
                        // Importar temas: theme folders (theme.json + art) from a folder the
                        // player picks, copied to LocalState/themes.
                        picker_kind = 6;
                        try {
                            winrt::Windows::Storage::Pickers::FolderPicker folder_picker;
                            folder_picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::ComputerFolder);
                            folder_picker.FileTypeFilter().Append(L"*");
                            picker = folder_picker.PickSingleFolderAsync();
                            themes_notice = Tr(T::NsThemesPick);
                            themes_notice_until = now + std::chrono::seconds{8};
                        } catch (...) {
                            themes_notice = Tr(T::PickerOpenFailed);
                            themes_notice_until = now + std::chrono::seconds{4};
                        }
                    }
                }
            }
            actions = 0;
            dirty = true;
        }
        if (system_page.open && configuration.open && !settings) {
            // The name editor takes its input in the handlers above; the page waits behind it.
            if (!system_page.name_editor && actions) {
                const auto send = [&](unsigned bit, SystemInput input) {
                    if (actions & bit) apply_system_effect(HandleSystemInput(system_page, input), now);
                };
                send(Up, SystemInput::Up);
                send(Down, SystemInput::Down);
                send(Left, SystemInput::Left);
                send(Right, SystemInput::Right);
                send(Play, SystemInput::Confirm);
                send(Quit, SystemInput::Back);
                if (!system_page.open) configuration.selected = HomeSystem;
            }
            actions = 0;
        }
        // NS HUEHUE (19/10/2026): LB/RB no longer walk the sections; each screen is entered with A
        // and left with B. In Jogos they walk the tabs (Todos, Favoritos, Recentes, Traducoes).
        if (library_top() && (actions & (PreviousTab | NextTab))) {
            library_tab = (library_tab + ((actions & NextTab) ? 1 : 3)) % 4;
            if (library_focus >= 0 && library_focus < 4) library_focus = static_cast<int>(library_tab);
            metadata_dirty = true;
            actions &= ~(PreviousTab | NextTab);
            dirty = true;
        }
        if (exit_prompt == 3) return std::nullopt;
        if (exit_prompt == 1 && actions) {
            // Sair do NS: A copies the saves then leaves, Y leaves without a copy, B stays.
            const unsigned pressed = actions;
            actions = 0;
            if (pressed & Play) {
                exit_prompt = 2;
                if (!saves_future.valid()) start_saves(SavesJob::Backup, nullptr);
            } else if (pressed & (Search | Refresh)) {
                return std::nullopt;
            } else if (pressed & Quit) {
                exit_prompt = 0;
            }
            dirty = true;
        }
        if (exit_prompt == 2) actions = 0; // copying; the result leaves (or stays, on a failure)
        // Sections are entered from the home only: what is left of LB/RB (pages that do not take
        // them) does nothing.
        if (!game_options.open && !search_editor && !(home && switch_home())) actions &= ~(PreviousTab | NextTab);
        if (home && switch_home() && !themes_page && actions && !game_options.open && !settings &&
            !configuration.open && !panel.keyboard.open && !picker && !validation.valid() && !missing_content &&
            !configuration.busy && !search_editor) {
            // NS HUEHUE - Switch: Left/Right along a row, Up/Down between the tiles and the buttons.
            const unsigned pressed = actions;
            actions = 0;
            if (!loading) rebuild_tiles();
            // B does nothing here (Sair do NS is a shortcut); LB/RB walk the games.
            if (pressed & (PreviousTab | NextTab)) {
                sw_row = 0;
                if ((pressed & PreviousTab) && sw_tile > 0) --sw_tile;
                if ((pressed & NextTab) && sw_tile < sw_tiles.size()) ++sw_tile;
            }
            const auto open_search = [&] {
                go_section(1);
                library_tab = 0;
                search_before = query;
                search_editor.emplace(query, 32);
            };
            if (pressed & Search) open_search();
            else if (sw_row == 0) {
                if ((pressed & Left) && sw_tile > 0) --sw_tile;
                if ((pressed & Right) && sw_tile < sw_tiles.size()) ++sw_tile;
                if (pressed & Down) sw_row = 1;
                const bool game = sw_tile < sw_tiles.size();
                if (pressed & Play) {
                    if (game) {
                        // Started as the library starts it (the grid's A, below).
                        selected = sw_tiles[sw_tile];
                        library_tab = 0;
                        query.clear();
                        home = false;
                        library_focus = -1;
                        actions = Play;
                    } else if (!loading && scan.entries.empty()) {
                        go_section(2);
                        configuration.files = true;
                        configuration.selected = 0;
                    } else {
                        go_section(1);
                        library_tab = 0;
                    }
                } else if ((pressed & Options) && game) {
                    selected = sw_tiles[sw_tile];
                    library_tab = 0;
                    query.clear();
                    home = false;
                    library_focus = -1;
                    actions = Options;
                    options_from_home = true;
                }
            } else {
                if (pressed & Left) sw_button = (sw_button + SwitchButtons - 1) % SwitchButtons;
                if (pressed & Right) sw_button = (sw_button + 1) % SwitchButtons;
                if (pressed & Up) sw_row = 0;
                if (pressed & Play) {
                    switch (sw_button) {
                    case 0: go_section(1); library_tab = 0; break;            // Biblioteca
                    case 1:                                                   // Arquivos e USB
                        go_section(2);
                        configuration.files = true;
                        configuration.selected = 0;
                        break;
                    case 2:                                                   // Saves (USB)
                        go_section(2);
                        configuration.selected = HomeSaves;
                        saves_page = true;
                        saves_row = saves_folder ? 1 : 0;
                        saves_confirm = 0;
                        break;
                    case 3: go_section(3); break;                             // Controles
                    case 4: go_section(2); break;                             // Configuracoes
                    case 5: open_search(); break;                             // Buscar
                    default: request_exit(); break;                           // Sair do NS
                    }
                    metadata_dirty = true;
                }
            }
            dirty = true;
        }
        if (home && actions && !game_options.open && !settings && !configuration.open &&
            !panel.keyboard.open && !picker && !validation.valid() && !missing_content &&
            !configuration.busy) {
            const unsigned pressed = actions;
            actions = 0;
            if (pressed & Up) home_row = (home_row + NxHomeRows - 1) % NxHomeRows;
            if (pressed & Down) home_row = (home_row + 1) % NxHomeRows;
            // Jogos, Favoritos, Recentes, Configuracoes, Sair do NS (B does nothing here).
            if (pressed & Play) {
                switch (home_row) {
                case 0: case 1: case 2: open_library(home_row); break;
                case 3: go_section(2); break;
                default: request_exit(); break;
                }
            }
            dirty = true;
        }
        // The library's top row: the three tabs and the search box (Up from the first grid row).
        if (library_top() && library_focus >= 0 && actions) {
            // The tabs change as the cursor reaches them; A (or Down) goes to the games, B home.
            if (actions & Left) library_focus = library_focus > 0 ? library_focus - 1 : 4;
            if (actions & Right) library_focus = (library_focus + 1) % 5;
            if ((actions & (Left | Right)) && library_focus < 4 && library_tab != static_cast<unsigned>(library_focus)) {
                library_tab = static_cast<unsigned>(library_focus);
                metadata_dirty = true;
            }
            if (actions & Quit) { home = true; library_focus = -1; }
            else if (actions & Down) library_focus = -1;
            if (actions & Play) {
                if (library_focus < 4) {
                    library_tab = static_cast<unsigned>(library_focus);
                    metadata_dirty = true;
                    rebuild_shown();
                    if (!shown.empty()) {
                        library_focus = -1;
                        if (position_of[std::min(selected, position_of.size() - 1)] == SIZE_MAX) selected = shown.front();
                    }
                } else {
                    search_before = query;
                    search_editor.emplace(query, 32);
                }
            }
            if (actions & Search) { search_before = query; search_editor.emplace(query, 32); }
            actions &= Options | Refresh | Panel;
            dirty = true;
        }
        if (library_top() && (actions & Search)) {
            search_before = query;
            search_editor.emplace(query, 32);
            actions = 0;
            dirty = true;
        }
        if (library_top() && !loading && scan.entries.empty() && (actions & Play) && library_focus < 0) {
            // Biblioteca vazia: A opens Configuracoes > Arquivos e USB, where games, keys and
            // firmware come in.
            go_section(2);
            configuration.files = true;
            configuration.selected = 0;
            actions = 0;
        }
        if (library_top() && !loading) {
            rebuild_shown();
            if (!shown.empty() && position_of[std::min(selected, position_of.size() - 1)] == SIZE_MAX) {
                selected = shown.front();
                dirty = true;
            }
            // Nothing on screen to start or to open options for.
            if (shown.empty()) actions &= ~(Play | Options);
            // Traducoes: A opens the game's mods page on its translation and starts the download.
            if (library_tab == 3 && (actions & Play) && library_focus < 0 && !shown.empty()) {
                if (const auto* found = translation_of(scan.entries[selected])) {
                    game_options = {true, selected};
                    game_options.page = GameOption::Mods;
                    pending_translation = found->front().name;
                    open_mods_page = true;
                    options_notice.clear();
                }
                actions &= ~Play;
                dirty = true;
            }
        }
        if (actions & Options && !game_options.open && !settings && !configuration.open &&
            !panel.keyboard.open && !loading && !picker && !validation.valid() && !scan.entries.empty()) {
            game_options = {true, selected};
            options_notice.clear();
            actions = 0;
            dirty = true;
        }
        if (game_options.open) {
            // A rescan may have replaced the list under the page.
            if (!game_options.open) tuning_page = false;
            if (!content_session && game_options.game >= scan.entries.size()) {
                game_options = {};
                cheat_session.reset();
                mod_session.reset();
            }
            if (cheat_session && cheat_session->Poll()) {
                Cheats::ClampCheatsPage(cheats_page, cheat_session->Catalog(), Cheats::CheatsLayout::VisibleRows);
                dirty = true;
            }
            const auto notify = [&](T text) {
                options_notice = Tr(text);
                options_notice_until = now + std::chrono::seconds{4};
            };
            if (content_session) {
                dirty |= content_session->Poll();
                if (content_session->Busy()) dirty = true;
                const auto send = [&](unsigned bits, Content::Input input) {
                    if ((actions & bits) && content_session && content_session->Handle(input)) {
                        const bool changed = content_session->Changed();
                        const bool overview = configuration.open;
                        content_session.reset(); game_options.page.reset();
                        if (overview) game_options = {};
                        if (changed) {
                            cache.Clear(); metadata = {}; ++generation;
                            for (auto& e : scan.entries) { e.title_loaded = false; e.metadata_loaded = false; e.build_id = {}; e.display_version.clear(); }
                            metadata_dirty = true; names_cursor = fill_cursor = 0;
                        }
                    }
                };
                send(Up, Content::Input::Up); send(Down, Content::Input::Down);
                send(Play, Content::Input::Confirm); send(Swap | AddFolder, Content::Input::Import);
                send(PreviousTab | NextTab, Content::Input::Storage); send(Refresh, Content::Input::Refresh);
                send(Quit, Content::Input::Back);
                if (content_session && content_session->TakeAuthorizeRequest() && !picker) {
                    try {
                        winrt::Windows::Storage::Pickers::FolderPicker folder_picker;
                        folder_picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::ComputerFolder);
                        folder_picker.FileTypeFilter().Append(L"*"); picker_kind = 3;
                        picker = folder_picker.PickSingleFolderAsync();
                    } catch (...) { options_notice = Tr(T::PickerOpenFailed); }
                }
                dirty |= actions != 0;
            } else if (mod_session) {
                using Kind = Mods::ModsCommand::Kind;
                using Mods::ModJob;
                if (pending_translation) {
                    // The translation picked in the library: once the CNX list is in, open it and
                    // queue its file; the page installs it when the download ends.
                    const auto& store = mod_session->Store();
                    if (store.source == Mods::StoreSource::Cnx && store.status == Mods::StoreStatus::Ready) {
                        const auto card = std::find_if(store.cards.begin(), store.cards.end(),
                                                       [&](const Mods::StoreCard& c) { return c.name == *pending_translation; });
                        const size_t index = card == store.cards.end() ? 0 : static_cast<size_t>(card - store.cards.begin());
                        mods_page.card = index;
                        mods_page.screen = Mods::ModsScreen::Detail;
                        mods_page.file = mods_page.image = 0;
                        mod_session->OpenDetail(index);
                        if (mod_session->Download(0)) {
                            options_notice = I18n::Format(T::ModsDownloadQueued, {Wide(*pending_translation)});
                            options_notice_until = now + std::chrono::seconds{6};
                        } else {
                            notify(T::ModsDownloadRefused);
                        }
                        pending_translation.reset();
                        dirty = true;
                    } else if (store.status == Mods::StoreStatus::Offline || store.status == Mods::StoreStatus::Empty) {
                        notify(store.status == Mods::StoreStatus::Offline ? T::NsTranslationsOffline : T::NsNoTranslations);
                        pending_translation.reset();
                        dirty = true;
                    }
                }
                const ModJob finished = mod_session->Poll();
                if (finished != ModJob::None) {
                    const auto& result = mod_session->View();
                    const bool from_store = result.origin.kind == Mods::SourceKind::GameBanana ||
                                            result.origin.kind == Mods::SourceKind::Cnx;
                    if (result.error != Mods::ModError::None) notify(Mods::ErrorText(result.error));
                    if (finished == ModJob::Reading && result.error == Mods::ModError::None)
                        Mods::ShowVariants(mods_page, result.candidates.size());
                    // A download with several mods in it: the player picks, wherever they are.
                    if (finished == ModJob::Installing && result.error == Mods::ModError::None &&
                        result.installed == 0 && !result.candidates.empty())
                        Mods::ShowVariants(mods_page, result.candidates.size());
                    if (finished == ModJob::Installing && result.installed > 0) {
                        // From a folder, back to the list where the new mods sit on top; from the
                        // store, the player stays where they browse.
                        if (!from_store) Mods::ShowInstalled(mods_page);
                        if (result.error == Mods::ModError::None) {
                            options_notice = result.updated ? std::wstring{Tr(T::ModsUpdated)}
                                                            : I18n::Plural(T::ModsInstalledOne, T::ModsInstalledOther,
                                                                           result.installed);
                            options_notice_until = now + std::chrono::seconds{4};
                        }
                    }
                    if (finished == ModJob::Browsing) mods_page.selected = mods_page.first = 0;
                    if (mods_page.tab != Mods::ModsTab::Explore)
                        Mods::ClampModsList(mods_page, mods_page.tab == Mods::ModsTab::Installed ? result.mods.size()
                                                                                                : result.files.size());
                    dirty = true;
                }
                if (mod_session->Tick()) dirty = true;
                const auto failed = mod_session->TakeNotice();
                if (failed) notify(Mods::ErrorText(*failed));
                // The log keeps lines in memory until an error: a job's end or a failure writes
                // them out, so the file pulled from the console tells what happened.
                if (finished != ModJob::None || failed) Common::Log::Stop();
                if (mod_session->Busy()) dirty = true; // The progress moves.
                const std::wstring mods_game = scan.entries[game_options.game].name;
                const auto send = [&](unsigned bits, Mods::ModsInput input) {
                    if (!(actions & bits) || !mod_session) return;
                    const auto counts = Mods::CountsFor(*mod_session, mods_page, mods_game);
                    const auto command = Mods::HandleModsInput(mods_page, counts, input);
                    switch (command.kind) {
                    case Kind::Toggle:
                        if (!mod_session->Toggle(command.index)) notify(T::ModsSaveFailed);
                        break;
                    case Kind::Move:
                        if (!mod_session->Move(command.index, command.to)) notify(T::ModsSaveFailed);
                        break;
                    case Kind::Remove: mod_session->Remove(command.index); break;
                    case Kind::Open: options_notice.clear(); mod_session->Open(command.index); break;
                    case Kind::Up: mod_session->Up(); break;
                    case Kind::Install:
                        mod_session->Install(mods_page.chosen);
                        mods_page.screen = Mods::ModsScreen::List;
                        break;
                    case Kind::Dismiss: mod_session->Dismiss(); break;
                    case Kind::Cancel: mod_session->Cancel(); break;
                    case Kind::Store: mod_session->OpenStore(); break;
                    case Kind::Retry: mod_session->RetryStore(); break;
                    case Kind::Want: mod_session->Want(command.index); break;
                    case Kind::Detail: mod_session->OpenDetail(command.index); break;
                    case Kind::Update: mod_session->OpenUpdate(command.index); break;
                    case Kind::CloseDetail: mod_session->CloseDetail(); break;
                    case Kind::Download:
                        if (mod_session->Download(command.index)) {
                            const auto& detail = mod_session->Store().detail;
                            if (detail && command.index < detail->files.size()) {
                                options_notice = I18n::Format(T::ModsDownloadQueued,
                                                              {Wide(detail->files[command.index].name)});
                                options_notice_until = now + std::chrono::seconds{4};
                            }
                        } else {
                            notify(T::ModsDownloadRefused);
                        }
                        break;
                    case Kind::Sort: mod_session->CycleSort(); break;
                    case Kind::Search: mod_session->Search(I18n::ToUtf8(command.text)); break;
                    case Kind::Source: mod_session->SwitchSource(); break;
                    case Kind::FindGame: mod_session->FindGame(I18n::ToUtf8(command.text)); break;
                    case Kind::PickGame: mod_session->PickGame(command.index); break;
                    case Kind::CancelDownloads: mod_session->CancelDownloads(); break;
                    case Kind::Categories: mod_session->OpenCategories(); break;
                    case Kind::EnterCategory: mod_session->EnterCategory(command.index); break;
                    case Kind::LeaveCategory: mod_session->LeaveCategory(); break;
                    case Kind::PickCategory: mod_session->PickCategory(command.index); break;
                    case Kind::Close:
                        mod_session.reset();
                        game_options.page.reset();
                        break;
                    case Kind::None:
                    case Kind::Moved:
                        break;
                    }
                };
                send(Up, Mods::ModsInput::Up);
                send(Down, Mods::ModsInput::Down);
                send(Left, Mods::ModsInput::Left);
                send(Right, Mods::ModsInput::Right);
                send(PreviousTab, Mods::ModsInput::PreviousTab);
                send(NextTab, Mods::ModsInput::NextTab);
                send(Play, Mods::ModsInput::Confirm);
                send(Swap | AddFolder, Mods::ModsInput::Alternate);
                send(Refresh, Mods::ModsInput::Menu);
                send(Options, Mods::ModsInput::Start);
                send(Quit, Mods::ModsInput::Back);
                const bool downloads_folder = mod_session && mod_session->TakeDownloadFolderRequest();
                if (mod_session && (mod_session->TakeFolderRequest() || downloads_folder) && !picker) {
                    try {
                        winrt::Windows::Storage::Pickers::FolderPicker folder_picker;
                        folder_picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::ComputerFolder);
                        folder_picker.FileTypeFilter().Append(L"*");
                        picker_kind = downloads_folder ? 40u : 4u;
                        picker = folder_picker.PickSingleFolderAsync();
                    } catch (...) { options_notice = Tr(T::PickerOpenFailed); }
                }
                dirty |= actions != 0;
            } else if (cheat_session && actions) {
                using Cheats::CheatsEffect;
                const auto send = [&](unsigned bits, MenuAction action) {
                    if (!(actions & bits)) return;
                    const auto input = Cheats::CheatsInputForAction(action);
                    if (!input) return;
                    switch (Cheats::HandleCheatsInput(cheats_page, *cheat_session, *input,
                                                      Cheats::CheatsLayout::VisibleRows)) {
                    case CheatsEffect::Changed:
                        if (!cheat_session->Saved()) notify(T::CheatsSaveFailed);
                        else options_notice.clear();
                        break;
                    case CheatsEffect::OverBudget: notify(T::CheatsOverBudget); break;
                    case CheatsEffect::Refresh: cheat_session->Retry(); break;
                    case CheatsEffect::Back:
                        cheat_session.reset();
                        game_options.page.reset();
                        break;
                    case CheatsEffect::None:
                    case CheatsEffect::Moved:
                        break;
                    }
                };
                send(Up, MenuAction::Up);
                send(Down, MenuAction::Down);
                send(Left | PreviousTab, MenuAction::PreviousTab);
                send(Right | NextTab, MenuAction::NextTab);
                send(Play, MenuAction::Confirm);
                send(Swap | AddFolder, MenuAction::Alternate);
                send(Refresh, MenuAction::Refresh);
                if (cheat_session) send(Quit, MenuAction::Back);
                dirty = true;
            } else if (tuning_page && actions) {
                // Desempenho e graficos: Up/Down pick a row, Left/Right (or A) change it, B goes back.
                if (actions & Up) tuning_row = (tuning_row + TuningRowCount - 1) % TuningRowCount;
                if (actions & Down) tuning_row = (tuning_row + 1) % TuningRowCount;
                // LB/RB move the manual FPS by 10 (Left/Right by 1).
                const bool big = (actions & (PreviousTab | NextTab)) != 0 &&
                                 static_cast<TuningRow>(tuning_row) == TuningRow::FpsManual;
                if ((actions & (Left | Right | Play)) || big) {
                    StepTuning(tuning, static_cast<TuningRow>(tuning_row), (actions & (Left | PreviousTab)) != 0, big);
                    notify(SaveGameTuning(scan.entries[game_options.game].launch_path, tuning) ? T::NsTuningSaved
                                                                                             : T::SystemSaveFailed);
                }
                if (actions & Quit) tuning_page = false;
                dirty = true;
            } else if (actions || open_mods_page) {
                open_mods_page = false;
                const auto send = [&](unsigned bit, GameOptionsInput input) {
                    if (actions & bit) HandleGameOptionsInput(game_options, input, 1, 3);
                };
                send(Up, GameOptionsInput::Up);
                send(Down, GameOptionsInput::Down);
                send(Play, GameOptionsInput::Confirm);
                send(Quit, GameOptionsInput::Back);
                if (game_options.extra == 0u) {
                    // NS HUEHUE: the first row starts the game.
                    game_options.extra.reset();
                    selected = game_options.game;
                    game_options = {};
                    launch_from_options = true;
                } else if (game_options.extra == 1u) {
                    // NS HUEHUE: Desempenho e graficos opens the game's tuning page.
                    game_options.extra.reset();
                    const auto& tuned = scan.entries[game_options.game];
                    ApplyKnownRenderer(tuned.launch_path, tuned.program_id);
                    tuning = LoadGameTuning(tuned.launch_path);
                    tuning_page = true;
                    tuning_row = 0;
                } else if (game_options.extra == 2u) {
                    // NS HUEHUE: Controles do jogo, the game's own button mapping.
                    game_options.extra.reset();
                    const auto& game = scan.entries[game_options.game];
                    const auto own = LoadGameTuning(game.launch_path).remap;
                    remap_open = true;
                    remap_game = true;
                    remap_own = own.has_value();
                    remap_edit = own.value_or(options.remap);
                    remap_row = 0;
                    remap_path = game.launch_path;
                    remap_title = game.name;
                } else if (game_options.extra) {
                    // NS HUEHUE: the last row adds the game to the favorites or removes it.
                    game_options.extra.reset();
                    const auto& path = scan.entries[game_options.game].launch_path;
                    const auto found = std::find(favorites.begin(), favorites.end(), path);
                    const bool added = found == favorites.end();
                    if (added) favorites.push_back(path);
                    else favorites.erase(found);
                    if (SaveList(L"ns_favorites.txt", favorites)) notify(added ? T::NsFavoriteAdded : T::NsFavoriteRemoved);
                    else notify(T::SystemSaveFailed);
                    metadata_dirty = true;
                }
                if (game_options.confirm_discard || game_options.discard) {
                    auto& game = scan.entries[game_options.game];
                    if (game.pulse_saved.empty()) {
                        game_options.confirm_discard = game_options.discard = false;
                        notify(T::PulseNone);
                    } else if (std::exchange(game_options.discard, false)) {
                        notify(PulseSessions::Discard(game.launch_path) ? T::PulseDiscarded : T::PulseDiscardFailed);
                        refresh_pulse();
                    } else {
                        options_notice = Tr(T::PulseDiscardConfirm);
                        options_notice_until = std::chrono::steady_clock::time_point::max();
                    }
                } else if (options_notice == Tr(T::PulseDiscardConfirm)) {
                    options_notice.clear();
                }
                if (game_options.page == GameOption::Updates || game_options.page == GameOption::Dlc) {
                    const auto& game = scan.entries[game_options.game];
                    if (!game.title_loaded || !game.program_id) {
                        notify(T::CheatsUnavailable); game_options.page.reset();
                    } else {
                        std::vector<std::uint64_t> bases;
                        for (const auto& e : scan.entries) if (e.program_id) bases.push_back(FileSys::GetBaseTitleID(e.program_id));
                        metadata_worker.request_stop();
                        content_session = std::make_unique<Content::ContentSession>(game.program_id, game.launch_path,
                            *game_options.page == GameOption::Updates ? Content::Kind::Update : Content::Kind::Dlc,
                            std::move(bases), std::move(metadata_worker));
                        metadata = {};
                    }
                }
                if (game_options.page == GameOption::Mods) {
                    // Mods are kept per title id; the build id (of the version that boots, update
                    // included) tells which mods' patches fit it.
                    const auto& game = scan.entries[game_options.game];
                    if (game.title_loaded && game.program_id == 0) {
                        notify(T::ModsHomebrew);
                        game_options.page.reset();
                    } else if (!game.title_loaded) {
                        notify(T::CheatsUnavailable);
                        game_options.page.reset();
                    } else {
                        std::string build_id;
                        if (std::any_of(game.build_id.begin(), game.build_id.end(), [](std::uint8_t b) { return b != 0; }))
                            for (const auto b : game.build_id) build_id += fmt::format("{:02X}", b);
                        // The store and the installs log what went wrong; the log opens with the page.
                        if (!logging_ready) { Common::Log::Initialize(); logging_ready = true; }
                        mod_session = std::make_unique<Mods::ModSession>(game.program_id, std::move(build_id),
                                                                         game.launch_path, I18n::ToUtf8(game.name),
                                                                         system_page.settings.show_adult_mods);
                        mods_page = {};
                        options_notice.clear();
                        if (pending_translation) {
                            // From the Traducoes tab: the store opens on the CNX index.
                            mods_page.tab = Mods::ModsTab::Explore;
                            mod_session->SwitchSource();
                            options_notice = Tr(T::NsSearchingTranslations);
                            options_notice_until = std::chrono::steady_clock::time_point::max();
                        }
                    }
                }
                if (game_options.page == GameOption::Cheats) {
                    // Cheat files are named after the game's title and main build ids, which the
                    // library reads with the title (uwp_library_metadata.cpp).
                    const auto& game = scan.entries[game_options.game];
                    const bool known = std::any_of(game.build_id.begin(), game.build_id.end(),
                                                   [](std::uint8_t b) { return b != 0; });
                    if (game.title_loaded && game.program_id == 0) {
                        notify(T::CheatsHomebrew);
                        game_options.page.reset();
                    } else if (!game.title_loaded || !known) {
                        notify(T::CheatsUnavailable);
                        game_options.page.reset();
                    } else {
                        Cheats::CheatGame cheat_game{game.program_id};
                        std::copy(game.build_id.begin(), game.build_id.end(), cheat_game.build_id.begin());
                        cheat_session.emplace(cheat_game);
                        cheats_page = {};
                        options_notice.clear();
                    }
                }
                dirty = true;
            }
            actions = 0;
            if (std::exchange(launch_from_options, false)) actions = Play;
            else if (!game_options.open && std::exchange(options_from_home, false)) home = true;
        }
        if (actions & Quit) {
            if (panel.keyboard.open) { panel.keyboard.open = panel.keyboard.capturing = false; dirty = true; actions &= ~Quit; }
            else if (panel.expanded) { panel.expanded = false; dirty = true; actions &= ~Quit; }
            else if (settings) { settings = false; configuration.open = return_to_configuration; return_to_configuration = false; dirty = true; actions &= ~Quit; }
            else if (configuration.open && online_page) { online_page = false; dirty = true; actions &= ~Quit; }
            else if (configuration.open && saves_page) {
                saves_page = false; saves_confirm = 0; saves_notice.clear(); dirty = true; actions &= ~Quit;
            }
            else if (configuration.open) {
                if (configuration.files) { configuration.files = false; configuration.selected = HomeFiles; }
                else configuration.open = false;
                dirty = true; actions &= ~Quit;
            }
            else if (!home) { home = true; library_focus = -1; dirty = true; actions &= ~Quit; }
            else actions &= ~Quit; // the home: Sair do NS leaves, not B
        }
        if (actions & Panel && !configuration.open) { settings = !settings; panel.keyboard.open = panel.keyboard.capturing = false; panel.expanded = false; dirty = true; }
        if (actions & AddFolder && !settings && !picker && !configuration.busy && !validation.valid()) {
            configuration.open = true; configuration.files = false; configuration.selected = HomeSystem;
            configuration.notice.clear(); refresh_setup(); dirty = true;
        }
        if (configuration.open && online_page && !settings && !picker && !configuration.busy && actions) {
            // Online: Jogar online, Servidor, Porta, Senha da sala.
            if (actions & Up) online_row = (online_row + 3) % 4;
            if (actions & Down) online_row = (online_row + 1) % 4;
            if (actions & (Play | Left | Right)) {
                if (online_row == 0) {
                    online.enabled = !online.enabled;
                    library_say(std::wstring{Tr(SaveOnlineSettings(online) ? T::SettingsSaved : T::SystemSaveFailed)},
                                now + std::chrono::seconds{3});
                } else if (actions & Play) {
                    edit_target = online_row == 1 ? EditTarget::Server : online_row == 2 ? EditTarget::Port
                                                                                      : EditTarget::Password;
                    const std::string& value = online_row == 1 ? online.server : online_row == 3 ? online.password
                                                                                             : std::to_string(online.port);
                    search_editor.emplace(Wide(value), online_row == 2 ? 5 : online_row == 1 ? 63 : 32);
                }
            }
            actions = 0;
            dirty = true;
        }
        if (firmware_staged && !configuration.busy && actions) {
            // The staged firmware: A installs it (the installed one becomes the backup), B drops it.
            const auto start_firmware_job = [&](unsigned job, std::function<std::string()> work) {
                firmware_job = job;
                configuration.busy = true;
                import_progress.completed = 0;
                import_progress.total = 0;
                metadata_worker.request_stop();
                std::packaged_task<std::string(std::stop_token)> task{
                    [work = std::move(work), old_metadata = std::move(metadata_worker)](std::stop_token) mutable {
                        if (old_metadata.joinable()) old_metadata.join();
                        winrt::init_apartment(winrt::apartment_type::multi_threaded);
                        struct Uninitialize { ~Uninitialize() { winrt::uninit_apartment(); } } guard;
                        return work();
                    }};
                import_future = task.get_future();
                import_worker = std::jthread(std::move(task));
            };
            if (actions & Play) {
                const StagedFirmware staged = *firmware_staged;
                firmware_staged.reset();
                configuration.notice = Tr(T::NsFirmwareInstalling);
                start_firmware_job(1, [staged] { return CommitStagedFirmware(staged); });
            } else if (actions & Quit) {
                DiscardStagedFirmware(*firmware_staged);
                firmware_staged.reset();
                configuration.notice = Tr(T::NsFirmwareCancelled);
            }
            actions = 0;
            dirty = true;
        }
        if (configuration.open && saves_page && !settings && !picker && !configuration.busy && actions) {
            // Saves (USB): Pasta no HD externo, Fazer backup agora, Restaurar saves, Copiar as
            // configuracoes, Restaurar as configuracoes (the automatic copy is gone: 19/10/2026).
            const auto say = [&](std::wstring text, std::chrono::seconds for_how_long) {
                saves_notice = std::move(text);
                saves_notice_until = now + for_how_long;
            };
            constexpr unsigned SavesRows = 5;
            if (actions & Up) { saves_row = (saves_row + SavesRows - 1) % SavesRows; saves_confirm = 0; }
            if (actions & Down) { saves_row = (saves_row + 1) % SavesRows; saves_confirm = 0; }
            const bool pressed = (actions & Play) != 0;
            if (pressed && saves_future.valid()) {
                say(std::wstring{Tr(T::NsSavesBusy)}, std::chrono::seconds{3});
            } else if (pressed && saves_row == 0) {
                picker_kind = 5;
                try {
                    winrt::Windows::Storage::Pickers::FolderPicker folder_picker;
                    folder_picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::ComputerFolder);
                    folder_picker.FileTypeFilter().Append(L"*");
                    picker = folder_picker.PickSingleFolderAsync();
                    say(std::wstring{Tr(T::NsSavesPickHint)}, std::chrono::seconds{8});
                } catch (...) { say(std::wstring{Tr(T::PickerOpenFailed)}, std::chrono::seconds{4}); }
            } else if (pressed && !saves_folder) {
                say(std::wstring{Tr(T::NsSavesNoFolder)}, std::chrono::seconds{4});
            } else if (pressed && saves_row == 1) {
                start_saves(saves_confirm == 2 ? SavesJob::Replace : SavesJob::Backup, nullptr);
                saves_notice.clear();
            } else if (pressed && saves_row == 2) {
                if (saves_confirm == 3) {
                    start_saves(SavesJob::Restore, nullptr);
                    saves_notice.clear();
                } else {
                    saves_confirm = 3;
                    saves_confirm_until = now + std::chrono::seconds{10};
                    saves_notice = Tr(T::NsSavesConfirmRestore);
                    saves_notice_until = std::chrono::steady_clock::time_point::max();
                }
            } else if (pressed && saves_row == 3) {
                start_saves(SavesJob::SettingsBackup, nullptr);
                saves_notice.clear();
            } else if (pressed && saves_row == 4) {
                if (saves_confirm == 4) {
                    start_saves(SavesJob::SettingsRestore, nullptr);
                    saves_notice.clear();
                } else {
                    saves_confirm = 4;
                    saves_confirm_until = now + std::chrono::seconds{10};
                    saves_notice = Tr(T::NsConfigConfirmRestore);
                    saves_notice_until = std::chrono::steady_clock::time_point::max();
                }
            }
            actions = 0;
            dirty = true;
        }
        if (configuration.open && !settings && !picker && !configuration.busy) {
            const auto count = ConfigurationRowCount(configuration);
            if (actions & Up) configuration.selected = (configuration.selected + count - 1) % count;
            if (actions & Down) configuration.selected = (configuration.selected + 1) % count;
            if (actions & (Up | Down)) { dirty = true; restore_confirm = false; }
            if (actions & Play) {
                if (!configuration.files) {
                    switch (configuration.selected) {
                    case HomeFiles: configuration.files = true; configuration.selected = 0; break;
                    case HomeControllers: settings = true; return_to_configuration = true; break;
                    case HomeOnline: online_page = true; online_row = 0; break;
                    case HomeSaves:
                        saves_page = true;
                        saves_row = saves_folder ? 1 : 0;
                        saves_confirm = 0;
                        break;
                    default:
                        system_page.open = true;
                        system_page.selected = SystemPage::UserName;
                        system_page.notice.clear();
                        // Windows' language or region may have changed since the library opened.
                        system_page.locale = DetectSystemLocale();
                        break;
                    }
                } else if (loading || setup_future.valid()) configuration.notice = Tr(T::WaitForCheck);
                else if (configuration.selected == 3 || configuration.selected == 4) {
                    std::vector<std::uint64_t> bases;
                    for (const auto& e : scan.entries) if (e.program_id) bases.push_back(FileSys::GetBaseTitleID(e.program_id));
                    metadata_worker.request_stop();
                    content_session = std::make_unique<Content::ContentSession>(0, "",
                        configuration.selected == 3 ? Content::Kind::Update : Content::Kind::Dlc,
                        std::move(bases), std::move(metadata_worker));
                    metadata = {}; game_options.open = true; game_options.game = 0;
                }
                else if (configuration.selected == FileRowRestoreFirmware) {
                    if (!configuration.status.has_backup) configuration.notice = Tr(T::NsFirmwareNoBackup);
                    else if (!restore_confirm) {
                        restore_confirm = true;
                        configuration.notice = I18n::Format(T::NsFirmwareConfirmRestore,
                            {configuration.status.firmware_backup.known ? Wide(configuration.status.firmware_backup.display)
                                                                        : std::wstring{L"?"}});
                    } else {
                        restore_confirm = false;
                        firmware_job = 2;
                        configuration.busy = true;
                        import_progress.completed = 0;
                        import_progress.total = 0;
                        metadata_worker.request_stop();
                        std::packaged_task<std::string(std::stop_token)> task{
                            [old_metadata = std::move(metadata_worker)](std::stop_token) mutable {
                                if (old_metadata.joinable()) old_metadata.join();
                                return RestoreFirmwareBackup();
                            }};
                        import_future = task.get_future();
                        import_worker = std::jthread(std::move(task));
                    }
                }
                else if (configuration.selected >= FileRowSources) {
                    try {
                        ForgetGameFolder(configuration.status.sources[configuration.selected - FileRowSources].token);
                        configuration.notice = Tr(T::FolderRemoved);
                        configuration.selected = 0; refresh_setup(); start_scan();
                    } catch (...) { configuration.notice = Tr(T::FolderRemoveFailed); }
                } else {
                    picker_kind = static_cast<unsigned>(configuration.selected);
                    try {
                        winrt::Windows::Storage::Pickers::FolderPicker folder_picker;
                        folder_picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::ComputerFolder);
                        folder_picker.FileTypeFilter().Append(L"*");
                        picker = folder_picker.PickSingleFolderAsync();
                        configuration.notice = Tr(picker_kind == 0 ? T::PickGamesFolder :
                                                  picker_kind == 1 ? T::PickKeysFolder : T::PickFirmwareFolder);
                    } catch (...) { configuration.notice = Tr(T::PickerOpenFailed); }
                }
                dirty = true;
            }
            actions = 0;
        }
        if (!settings) panel.notice.clear();
        if (!panel.keyboard.open) panel.keyboard.notice.clear();
        if (loading && future.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
            try { scan = future.get(); }
            catch (const std::exception& e) { scan.error = e.what(); }
            catch (const winrt::hresult_error& e) { scan.error = winrt::to_string(e.message()); }
            loading = false;
            refresh_pulse();
            if (!configuration.busy) refresh_setup();
            selected = std::min(selected, scan.entries.empty() ? 0 : scan.entries.size() - 1);
            notice = Wide(scan.error);
            if (notice.empty() && !initial_notice.empty()) notice = Wide(initial_notice);
            initial_notice = {};
            if (scan.limited) notice = Tr(T::ScanLimit);
            if (notice.empty() && scan.content_added) {
                library_say(I18n::Plural(T::NsContentAddedOne, T::NsContentAddedOther, scan.content_added),
                            std::chrono::steady_clock::now() + std::chrono::seconds{6});
            }
            dirty = true;
        }
        if (!loading && !translations_started && !scan.entries.empty()) {
            translations_started = true;
            std::packaged_task<std::optional<TranslationIndex>(std::stop_token)> task{[](std::stop_token stop) {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                struct Uninitialize { ~Uninitialize() { winrt::uninit_apartment(); } } guard;
                const auto text = Mods::Web::GetCached(std::string{Mods::Cnx::IndexUrl},
                                                       Mods::Web::CacheFolder() / "cnx-index.json", 60, stop);
                return text ? std::optional{Mods::Cnx::ParseTranslations(*text)} : std::nullopt;
            }};
            translations_future = task.get_future();
            translations_worker = std::jthread(std::move(task));
        }
        if (translations_future.valid() &&
            translations_future.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
            try {
                if (auto index = translations_future.get()) translations = std::move(*index);
            } catch (...) {}
            translations_ready = true;
            dirty = true;
        }
        if (!auto_pick.empty() && !configuration.open && !loading && !picker && !validation.valid()) {
            if (auto_pick_at == std::chrono::steady_clock::time_point{})
                auto_pick_at = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            if (std::chrono::steady_clock::now() >= auto_pick_at) {
                for (size_t i = 0; i < scan.entries.size(); ++i) {
                    const auto path = scan.entries[i].relative_path.u8string();
                    if (scan.entries[i].launch_path == auto_pick ||
                        (!IsStoragePath(scan.entries[i].launch_path) &&
                         std::string_view{reinterpret_cast<const char*>(path.data()), path.size()} == auto_pick)) {
                        selected = i; settings = false; home = false; actions |= Play;
                    }
                }
            }
        }
        if (actions & Play && !configuration.open && !settings && !panel.keyboard.open && !loading && !scan.entries.empty()) {
            selected_path = scan.entries[selected].launch_path;
            // A title the compatibility list starts on OpenGL, the first time (game_compat.h).
            ApplyKnownRenderer(selected_path, scan.entries[selected].program_id);
            library_say(std::wstring{Tr(T::CheckingGame)}, std::chrono::steady_clock::time_point::max());
            metadata_worker.request_stop();
            metadata = {};
            std::packaged_task<std::string()> task{[path = selected_path, old_metadata = std::move(metadata_worker)]() mutable {
                if (old_metadata.joinable()) old_metadata.join();
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                struct Uninitialize { ~Uninitialize() { winrt::uninit_apartment(); } } guard;
                if (!HasUsableHeaderKey() && !path.ends_with(".nro"))
                    return I18n::ToUtf8(Tr(T::MissingKeys));
                if (IsStoragePath(path)) {
                    if (const auto error = CheckExternalGame(path); !error.empty()) return error;
                }
                if (path.ends_with(".nro")) return std::string{};
                Core::System system;
                system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
                system.SetFilesystem(MakeUwpFilesystem());
                system.GetFileSystemController().CreateFactories(*system.GetFilesystem());
                FileSys::ManualContentProvider provider;
                const auto resolved = Content::ResolveLaunchContent(*system.GetFilesystem(), path, provider, false, {}, Content::ReportedSystemVersion(system));
                if (resolved.error == Content::Error::Unavailable) return std::string{"content-missing"};
                return resolved.error == Content::Error::None ? std::string{} : I18n::ToUtf8(Tr(Content::ErrorText(resolved.error)));
            }};
            validation = task.get_future();
            validation_worker = std::jthread(std::move(task));
            actions = 0;
        }
        if (settings) {
            if (panel.keyboard.open) { actions &= ~(Swap | Zone); }
            else if (panel.expanded) {
                const size_t count = panel.devices.size() + 2;
                if (actions & Up) panel.choice = (panel.choice + count - 1) % count;
                if (actions & Down) panel.choice = (panel.choice + 1) % count;
                if (actions & (Up | Down)) dirty = true;
                if (actions & Play) {
                    if (panel.choice < 2 || (bool(panel.devices[panel.choice - 2]) &&
                        !panel.devices[panel.choice - 2].id.starts_with(L"session:"))) {
                        options.controller_id = panel.choice == 0 ? L"" : panel.choice == 1 ?
                            std::wstring{KeyboardControllerId} : panel.devices[panel.choice - 2].id;
                        device_changed = true;
                        panel.expanded = false;
                    }
                    dirty = true;
                }
                actions &= ~(Swap | Zone);
            } else {
                // Rows: [device (PC)], controller type, A/B/X/Y, deadzone, Mapear botoes, [keyboard (PC)].
                const unsigned offset = panel.xbox ? 0 : 1;
                const unsigned rows = panel.xbox ? 4 : 6;
                if (actions & Up) setting_row = (setting_row + rows - 1) % rows;
                if (actions & Down) setting_row = (setting_row + 1) % rows;
                if (actions & (Up | Down)) dirty = true;
                if ((actions & Play) && setting_row == 3 + offset) {
                    remap_open = true;
                    remap_game = false;
                    remap_own = !IsIdentity(options.remap);
                    remap_edit = options.remap;
                    remap_row = 0;
                    remap_title = Tr(T::NsRemapAllGames);
                    actions &= ~Play;
                    dirty = true;
                } else if (actions & (Play | Left | Right)) {
                    if (setting_row == 3 + offset) {
                    } else if (!panel.xbox && setting_row == 5) {
                        panel.keyboard.open = true; panel.keyboard.capturing = false; dirty = true;
                    } else if (!panel.xbox && setting_row == 0) {
                        panel.expanded = true;
                        panel.choice = options.controller_id == KeyboardControllerId ? 1 : 0;
                        for (size_t i = 0; i < panel.devices.size(); ++i)
                            if (panel.devices[i].id == options.controller_id) panel.choice = i + 2;
                        dirty = true;
                    } else if (setting_row == offset) {
                        options.style = StepConsoleControllerStyle(options.style, (actions & Left) != 0);
                        device_changed = true; dirty = true;
                    } else actions |= setting_row == offset + 1 ? Swap : Zone;
                }
            }
        } else {
            actions &= ~(Swap | Zone);
            // NS HUEHUE: the grid walks the entries it shows, five to a row; Up from the first row
            // reaches the tabs.
            if (library_top() && !loading && (actions & (Left | Right | Up | Down))) {
                rebuild_shown(); // A scan may have just finished.
                if (shown.empty()) {
                    if (actions & Up) library_focus = static_cast<int>(library_tab);
                } else {
                    size_t at = position_of[selected] == SIZE_MAX ? 0 : position_of[selected];
                    if (actions & Left && at) --at;
                    if (actions & Right && at + 1 < shown.size()) ++at;
                    if (actions & Up) {
                        if (at >= PageSize) at -= PageSize;
                        else library_focus = static_cast<int>(library_tab);
                    }
                    if (actions & Down && at + PageSize < shown.size()) at += PageSize;
                    else if (actions & Down && at / PageSize < (shown.size() - 1) / PageSize) at = shown.size() - 1;
                    selected = shown[at];
                }
                dirty = true;
            }
        }
        if (actions & Swap) { options.swap_face_buttons = !options.swap_face_buttons; dirty = true; }
        if (actions & Zone) {
            options.deadzone = options.deadzone < 0.10f ? 0.12f : options.deadzone < 0.15f ? 0.18f : 0.08f;
            dirty = true;
        }
        if (device_changed || actions & (Swap | Zone)) {
            device_changed = false;
            const bool saved = SaveControllerOptions(options);
            panel.notice = Tr(saved ? T::SettingsSaved : T::ControllerSaveFailed);
            controller_notice_until = saved ? now + std::chrono::seconds{3} :
                                             std::chrono::steady_clock::time_point::max();
        }
        if (actions & Refresh && !loading && !picker && !validation.valid()) start_scan();
        if (!configuration.busy && metadata.valid() && metadata.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
            try {
                auto batch = metadata.get();
                if (batch.generation == generation) {
                    // Merge field by field: a names-only batch must not drop icons already shown.
                    rebuild_shown();
                    const size_t cursor = selected < position_of.size() && position_of[selected] != SIZE_MAX ? position_of[selected] : 0;
                    const size_t page = cursor / PageVisible * PageVisible;
                    const size_t window_first = page >= PageSize ? page - PageSize : 0;
                    for (auto& entry : batch.entries) {
                        auto found = std::find_if(scan.entries.begin(), scan.entries.end(), [&](const auto& e) {
                            return e.launch_path == entry.launch_path;
                        });
                        if (found == scan.entries.end()) continue;
                        const auto index = static_cast<size_t>(found - scan.entries.begin());
                        // An interrupted background batch is resumed from its first unread game.
                        if (batch.stopped && !batch.icons && !entry.title_loaded) {
                            names_cursor = std::min(names_cursor, index);
                            fill_cursor = std::min(fill_cursor, index);
                        }
                        found->name = std::move(entry.name);
                        found->developer = std::move(entry.developer);
                        if (entry.title_loaded) found->display_version = std::move(entry.display_version);
                        if (entry.title_loaded) {
                            found->program_id = entry.program_id;
                            found->build_id = entry.build_id;
                        }
                        found->title_loaded |= entry.title_loaded;
                        found->unreadable = entry.unreadable;
                        // The player may have paged on meanwhile: keep icons only inside the window.
                        if (batch.icons && ((position_of[index] != SIZE_MAX && position_of[index] >= window_first &&
                                             position_of[index] < page + PageVisible + PageSize) ||
                                            std::find(kept_tiles.begin(), kept_tiles.end(), index) != kept_tiles.end()))
                            found->icon = std::move(entry.icon);
                    }
                    dirty = true;
                }
            } catch (const std::exception&) { notice = Tr(T::MetadataFailed); }
        }
        // Metadata, one worker at a time and in priority order: the covers around the cursor, then
        // every title the cache already has, then the rest of the library into the cache.
        // The grid's page in what it shows (the tab and search pick the entries); covers load for it
        // and a row on each side.
        if (query != filtered_query || library_tab != filtered_tab) {
            filtered_query = query; filtered_tab = library_tab; metadata_dirty = true;
        }
        // The Switch home's tiles are not the grid's page (recent games first): their covers are
        // kept and read too, or a tile showed the fallback until Recentes read it (19/10/2026).
        std::vector<size_t> tile_keep;
        if (home && switch_home() && !loading) {
            rebuild_tiles();
            tile_keep = sw_tiles;
        }
        if (tile_keep != kept_tiles) { kept_tiles = tile_keep; metadata_dirty = true; }
        const auto is_tile = [&](size_t index) {
            return std::find(kept_tiles.begin(), kept_tiles.end(), index) != kept_tiles.end();
        };
        rebuild_shown();
        const size_t grid_at = selected < position_of.size() && position_of[selected] != SIZE_MAX ? position_of[selected] : 0;
        const size_t page = grid_at / PageVisible * PageVisible;
        // The player moved to another page: background work yields to its covers.
        if (metadata.valid() && metadata_background && (metadata_dirty || retained_page != page))
            metadata_worker.request_stop();
        if (!content_session && !validation.valid() && !missing_content && !configuration.open && !loading && !metadata.valid() && !scan.entries.empty()) {
            MetadataOptions request{title_language};
            std::vector<LibraryEntry> requested;
            if (metadata_dirty || retained_page != page) {
                metadata_dirty = false;
                retained_page = page;
                const size_t window_first = page >= PageSize ? page - PageSize : 0;
                const size_t window_end = std::min(page + PageVisible + PageSize, shown.size());
                for (size_t i = 0; i < scan.entries.size(); ++i) {
                    auto& entry = scan.entries[i];
                    const size_t at = position_of[i];
                    if ((at == SIZE_MAX || at < window_first || at >= window_end) && entry.metadata_loaded && !is_tile(i)) {
                        std::vector<unsigned char>{}.swap(entry.icon);
                        entry.metadata_loaded = false;
                    }
                }
                // The visible page first, then the next one (where the player usually goes), then the previous.
                const auto want = [&](size_t first, size_t last) {
                    for (size_t k = first; k < std::min(last, shown.size()); ++k) {
                        auto& entry = scan.entries[shown[k]];
                        if (entry.metadata_loaded) continue;
                        entry.metadata_loaded = true;
                        requested.push_back(entry);
                    }
                };
                // The Switch home's tiles first while it is on screen.
                for (const size_t index : kept_tiles) {
                    if (index >= scan.entries.size() || scan.entries[index].metadata_loaded) continue;
                    scan.entries[index].metadata_loaded = true;
                    requested.push_back(scan.entries[index]);
                }
                want(page, page + PageVisible);
                want(page + PageVisible, page + PageVisible + PageSize);
                want(window_first, page);
                request.icons = true;
            } else {
                // Titles without icons: what the grid and the cache need, never a cover's bytes.
                const auto take = [&](size_t& cursor, size_t batch) {
                    while (cursor < scan.entries.size() && requested.size() < batch) {
                        const auto& entry = scan.entries[cursor++];
                        if (!entry.title_loaded)
                            requested.push_back({entry.relative_path, entry.name, {}, {}, false, false,
                                                 entry.launch_path, entry.source_name});
                    }
                };
                if (names_cursor < scan.entries.size()) {
                    take(names_cursor, NamesBatch);
                    request.cache_only = true;
                } else {
                    take(fill_cursor, FillBatch);
                }
            }
            if (!requested.empty()) {
                if (!logging_ready) { Common::Log::Initialize(); logging_ready = true; }
                std::packaged_task<MetadataBatch(std::stop_token)> task{
                    [root, &cache, requested = std::move(requested), generation, request](std::stop_token stop) mutable {
                        winrt::init_apartment(winrt::apartment_type::multi_threaded);
                        struct Uninitialize { ~Uninitialize() { winrt::uninit_apartment(); } } guard;
                        ReadLibraryMetadata(root, requested, cache, request, stop);
                        return MetadataBatch{std::move(requested), generation, request.icons,
                                             stop.stop_requested()};
                    }};
                metadata_background = !request.icons;
                metadata = task.get_future();
                metadata_worker = std::jthread(std::move(task));
            }
        }
        actions = 0;
        if (resize) {
            canvas.reset();
            canvas = std::make_unique<LibraryCanvas>(core_window, width, height);
            resize = false;
            dirty = true;
        }
        if (dirty && game_options.open && !game_options.page && game_options.game < scan.entries.size())
            refresh_status(scan.entries[game_options.game]);
        if (dirty) {
            const GameOptionsView options_view{
                game_options, game_options.open && game_options.game < scan.entries.size() ? &scan.entries[game_options.game] : nullptr,
                cheat_session ? &*cheat_session : nullptr, cheats_page, options_notice, content_session.get(),
                mod_session.get(), &mods_page, status_update, status_dlc, status_mods,
                game_options.open && game_options.game < scan.entries.size()
                    ? static_cast<std::uint8_t>(LoadGameProfile(scan.entries[game_options.game].launch_path)) : std::uint8_t{},
                tuning_page ? &tuning : nullptr, tuning_row,
                game_options.game < scan.entries.size() &&
                    std::find(favorites.begin(), favorites.end(), scan.entries[game_options.game].launch_path) != favorites.end(),
                game_options.open && game_options.game < scan.entries.size() &&
                    LoadGameTuning(scan.entries[game_options.game].launch_path).remap.has_value()};
            const RemapView remap_view{remap_open, remap_game, remap_own, remap_row, remap_edit, remap_title, options_notice};
            if ((home || theme_previewing) && !loading) rebuild_tiles();
            const ThemesView themes_view{themes_page, theme_previewing, themes_card, themes_focus,
                                         active_card(), themes_wallpaper, themes_notice, &theme_packs};
            // The frame's theme: the candidate while previewing, else the one in use.
            const unsigned frame_card = theme_previewing ? themes_card : active_card();
            const ThemePack* frame_pack = frame_card >= ThemeCount && frame_card - ThemeCount < theme_packs.size()
                                              ? &theme_packs[frame_card - ThemeCount] : nullptr;
            const unsigned frame_theme = frame_pack ? static_cast<unsigned>(frame_pack->base) : std::min(frame_card, 2U);
            const unsigned frame_wallpaper = themes_page ? themes_wallpaper : system_page.settings.wallpaper;
            rebuild_shown();
            const SavesView saves_view{saves_page, saves_row, saves_folder, saves.folder, saves.automatic,
                                       saves.last_time, saves.last_games, saves_future.valid(),
                                       saves_job == SavesJob::Restore, saves_progress.done.load(),
                                       saves_progress.total.load(), saves_notice};
            canvas->Draw({scan, selected, loading, settings, setting_row, options, notice, panel,
                          configuration, system_page, options_view, missing_content, home, home_row,
                          false, library_tab, library_focus, &shown, &favorites, query,
                          search_editor ? &*search_editor : nullptr, false, &translated, translations_ready,
                          &online, online_page, online_row, static_cast<unsigned>(edit_target), &saves_view,
                          frame_theme, frame_wallpaper, themes_page ? &themes_view : nullptr, &sw_tiles, sw_row,
                          sw_tile, sw_button, frame_pack, exit_prompt, remap_open ? &remap_view : nullptr});
            dirty = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{16});
    }
}
} // namespace EdenXbox
