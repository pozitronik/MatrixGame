// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <string>
#include <string_view>
#include <vector>

// Battle maps in registered packages and the supported loose folders beside the EXE.
namespace MapCatalog {

inline constexpr const wchar_t *folder = L"Matrix\\Map";
inline constexpr const wchar_t *external_folder = L"maps";
inline constexpr const wchar_t *data_folder = L"DATA\\maps";

struct Map {
    std::wstring name;     // File name without the extension, shown to the player.
    std::wstring path;     // Actual relative path, also stored in the remembered selection.
    std::wstring preview;  // Same-stem image from a supported folder, or empty.
    bool loose = false;    // A loose file, which the loader prefers to a packaged one.
    bool operator==(const Map &) const = default;
};

// File names compare case-insensitively. Priority: maps, DATA/maps, Matrix/Map, packages.
// JPEG precedes PNG; within each format the same source priority applies. Paths retain their
// source folder. Only maps with valid remembered paths are listed, sorted by name.
std::vector<Map> merge(const std::vector<std::wstring> &packed, const std::vector<std::wstring> &loose,
                       const std::vector<std::wstring> &external = {}, const std::vector<std::wstring> &data = {});

// Lists registered packages and all supported loose folders, without scanning subfolders.
std::vector<Map> scan();

// Index of the map with this path, compared case-insensitively, or -1.
int find(const std::vector<Map> &maps, std::wstring_view path);

// A relative path ending in .cmap without empty, current or parent components, made of ASCII letters,
// digits, spaces and the punctuation " _-.()+!#&,'~". The settings file stores ASCII values and gives
// '=', braces and "//" meaning, hint markup does so for '|' and brackets, and loose files are opened
// through narrow file names.
bool valid_path(std::wstring_view path);

// Whether the file loads as a map store with the properties table that map preparation reads first.
bool readable(const std::wstring &path);

}  // namespace MapCatalog
