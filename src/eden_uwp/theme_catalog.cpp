// SPDX-FileCopyrightText: Copyright 2026 NS HUEHUE contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eden_uwp/theme_catalog.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.FileProperties.h>

#include "common/logging.h"
#include "eden_uwp/localization.h"

namespace EdenXbox {
namespace {
using nlohmann::json;
namespace fs = std::filesystem;
using namespace winrt::Windows::Storage;

constexpr std::size_t MaxPacks = 64;
constexpr std::uint64_t MaxManifestBytes = 64 * 1024;
constexpr std::uint64_t MaxImageBytes = 32ULL << 20;

fs::path LocalThemes() {
    try {
        return fs::path{ApplicationData::Current().LocalFolder().Path().c_str()} / L"themes";
    } catch (...) { return {}; }
}
fs::path BundledThemes() {
    try {
        return fs::path{winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str()} /
               L"Assets" / L"themes";
    } catch (...) { return {}; }
}
bool ValidId(std::wstring_view id) {
    if (id.empty() || id.size() > 64) return false;
    return std::all_of(id.begin(), id.end(), [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-';
    });
}
/// A file name inside the pack: no folders, no "..", an image extension.
bool ValidImageName(std::wstring_view name) {
    if (name.empty() || name.size() > 128 || name.find_first_of(L"\\/:") != std::wstring_view::npos ||
        name.find(L"..") != std::wstring_view::npos)
        return false;
    std::wstring lower{name};
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return lower.ends_with(L".png") || lower.ends_with(L".jpg") || lower.ends_with(L".jpeg");
}
std::optional<unsigned> ParseColor(const json& value) {
    if (!value.is_string()) return std::nullopt;
    const auto text = value.get<std::string>();
    if (text.size() != 7 || text[0] != '#') return std::nullopt;
    unsigned color = 0;
    for (std::size_t i = 1; i < 7; ++i) {
        const char c = text[i];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return std::nullopt;
        color = color << 4 | static_cast<unsigned>(digit);
    }
    return color;
}
bool Light(unsigned color) {
    const unsigned r = color >> 16 & 0xff, g = color >> 8 & 0xff, b = color & 0xff;
    return r * 299 + g * 587 + b * 114 > 150000;
}
std::wstring Widen(const std::string& utf8) { return winrt::to_hstring(utf8).c_str(); }

/// The pack described by `text` (theme.json) in `folder`; nullopt when it is not valid.
std::optional<ThemePack> ParsePack(const std::string& text, const fs::path& folder, bool check_files) {
    try {
        const auto manifest = json::parse(text);
        if (manifest.value("schemaVersion", 0) != 1) return std::nullopt;
        ThemePack pack;
        pack.id = Widen(manifest.value("id", std::string{}));
        pack.name = Widen(manifest.value("name", std::string{}));
        pack.version = Widen(manifest.value("version", std::string{"1"}));
        if (!ValidId(pack.id) || pack.name.empty() || pack.name.size() > 48) return std::nullopt;
        // The built-in ids belong to the built-in themes.
        if (ParseTheme(pack.id)) return std::nullopt;
        const auto base = manifest.value("base", std::string{"switch-dark"});
        if (base == "switch-dark") pack.base = ThemeId::SwitchDark;
        else if (base == "switch-light") pack.base = ThemeId::SwitchLight;
        else return std::nullopt;
        const auto background = Widen(manifest.value("background", std::string{}));
        const auto thumbnail = Widen(manifest.value("thumbnail", std::string{}));
        if (!ValidImageName(background) || (!thumbnail.empty() && !ValidImageName(thumbnail))) return std::nullopt;
        pack.background = folder / background;
        pack.thumbnail = thumbnail.empty() ? pack.background : folder / thumbnail;
        if (check_files) {
            std::error_code error;
            for (const auto& image : {pack.background, pack.thumbnail}) {
                const auto size = fs::file_size(image, error);
                if (error || size == 0 || size > MaxImageBytes) return std::nullopt;
            }
        }
        ThemeColors colors = BaseThemeColors(pack.base);
        if (const auto found = manifest.find("colors"); found != manifest.end() && found->is_object()) {
            const auto& c = *found;
            const auto take = [&](const char* key) { return c.contains(key) ? ParseColor(c[key]) : std::nullopt; };
            if (const auto v = take("background")) colors.backdrop = *v;
            if (const auto v = take("foreground")) colors.text = *v;
            if (const auto v = take("surface")) colors.panel = colors.row = colors.secondary = colors.menu_button = *v;
            if (const auto v = take("muted")) colors.text_secondary = colors.text_hint = colors.muted = *v;
            if (const auto v = take("border")) colors.line = colors.divider = colors.track = *v;
            if (const auto v = take("focus")) colors.focus = *v;
            if (const auto v = take("selection")) colors.row_active = colors.accent_deep = colors.nav_selected = colors.toast = *v;
            if (const auto v = take("accent")) {
                colors.accent = colors.magenta = *v;
                colors.on_accent = Light(*v) ? 0x101214 : 0xffffff;
            }
            if (const auto v = take("warning")) colors.warning = *v;
            if (const auto v = take("error")) colors.error = *v;
        }
        pack.colors = colors;
        return pack;
    } catch (const std::exception& e) {
        LOG_WARNING(Frontend, "themes: invalid manifest in {}: {}", winrt::to_string(folder.wstring()), e.what());
        return std::nullopt;
    }
}
std::optional<std::string> ReadSmall(const fs::path& file) {
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error || size > MaxManifestBytes) return std::nullopt;
    std::ifstream in{file, std::ios::binary};
    std::ostringstream text;
    text << in.rdbuf();
    return in ? std::optional{text.str()} : std::nullopt;
}
void Scan(const fs::path& root, bool bundled, std::vector<ThemePack>& out) {
    std::error_code error;
    if (root.empty() || !fs::is_directory(root, error)) return;
    std::vector<ThemePack> found;
    for (fs::directory_iterator it{root, error}, end; !error && it != end && found.size() < MaxPacks; it.increment(error)) {
        if (!it->is_directory(error)) continue;
        const auto text = ReadSmall(it->path() / L"theme.json");
        if (!text) continue;
        if (auto pack = ParsePack(*text, it->path(), true)) {
            pack->bundled = bundled;
            if (std::none_of(out.begin(), out.end(), [&](const ThemePack& p) { return p.id == pack->id; }) &&
                std::none_of(found.begin(), found.end(), [&](const ThemePack& p) { return p.id == pack->id; }))
                found.push_back(std::move(*pack));
        }
    }
    std::sort(found.begin(), found.end(), [](const ThemePack& a, const ThemePack& b) { return a.name < b.name; });
    out.insert(out.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
}
std::string ReadText(const StorageFile& file) {
    if (file.GetBasicPropertiesAsync().get().Size() > MaxManifestBytes) return {};
    return winrt::to_string(FileIO::ReadTextAsync(file).get());
}
/// Copies one pack folder (validated) into LocalState/themes/<id>.
bool ImportOne(const StorageFolder& folder, std::stop_token stop) {
    const auto item = folder.TryGetItemAsync(L"theme.json").get();
    if (!item) return false;
    const auto manifest_file = item.try_as<StorageFile>();
    if (!manifest_file) return false;
    const auto text = ReadText(manifest_file);
    const auto local = LocalThemes();
    if (text.empty() || local.empty()) return false;
    auto pack = ParsePack(text, local, false);
    if (!pack) return false;
    // The images it names must be there, of a sane size.
    std::vector<StorageFile> images;
    for (const auto& image : {pack->background.filename(), pack->thumbnail.filename()}) {
        const auto found = folder.TryGetItemAsync(winrt::hstring{image.wstring()}).get();
        const auto file = found ? found.try_as<StorageFile>() : nullptr;
        if (!file) return false;
        const auto size = file.GetBasicPropertiesAsync().get().Size();
        if (size == 0 || size > MaxImageBytes) return false;
        if (std::none_of(images.begin(), images.end(), [&](const StorageFile& f) { return f.Name() == file.Name(); }))
            images.push_back(file);
    }
    if (stop.stop_requested()) return false;
    std::error_code error;
    fs::create_directories(local, error);
    // Copied beside the installed pack first, then put in its place.
    const auto target = local / pack->id;
    const auto staging = local / (pack->id + L".import");
    fs::remove_all(staging, error);
    fs::create_directories(staging, error);
    if (error) return false;
    auto staging_folder = StorageFolder::GetFolderFromPathAsync(staging.wstring()).get();
    manifest_file.CopyAsync(staging_folder, L"theme.json", NameCollisionOption::ReplaceExisting).get();
    for (const auto& file : images) file.CopyAsync(staging_folder, file.Name(), NameCollisionOption::ReplaceExisting).get();
    const auto copied = ReadSmall(staging / L"theme.json");
    if (!copied || !ParsePack(*copied, staging, true)) {
        fs::remove_all(staging, error);
        return false;
    }
    fs::remove_all(target, error);
    fs::rename(staging, target, error);
    if (error) {
        fs::remove_all(staging, error);
        return false;
    }
    LOG_INFO(Frontend, "themes: imported {}", winrt::to_string(pack->id));
    return true;
}
} // namespace

std::vector<ThemePack> LoadThemePacks() {
    std::vector<ThemePack> packs;
    Scan(BundledThemes(), true, packs);
    Scan(LocalThemes(), false, packs);
    return packs;
}

ThemeImportResult ImportThemePacks(const StorageFolder& source, std::stop_token stop) {
    ThemeImportResult result;
    try {
        if (source.TryGetItemAsync(L"theme.json").get()) {
            (ImportOne(source, stop) ? result.imported : result.skipped)++;
            return result;
        }
        unsigned seen = 0;
        for (const auto& folder : source.GetFoldersAsync().get()) {
            if (stop.stop_requested() || ++seen > MaxPacks) break;
            if (!folder.TryGetItemAsync(L"theme.json").get()) continue;
            (ImportOne(folder, stop) ? result.imported : result.skipped)++;
        }
    } catch (const winrt::hresult_error& e) {
        LOG_ERROR(Frontend, "themes: import failed: {}", winrt::to_string(e.message()));
        result.error = I18n::ToUtf8(I18n::Tr(I18n::Text::NsThemesImportFailed));
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "themes: import failed: {}", e.what());
        result.error = I18n::ToUtf8(I18n::Tr(I18n::Text::NsThemesImportFailed));
    }
    return result;
}

} // namespace EdenXbox
