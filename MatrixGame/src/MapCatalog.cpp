// MatrixGame - licensed under GPLv2 or any later version.
#include "MapCatalog.hpp"
#include "Common.hpp"

#include "CFile.hpp"
#include "CStorage.hpp"

#include <algorithm>
#include <filesystem>
#include <map>

namespace MapCatalog {
namespace {

std::wstring folded(std::wstring_view text) {
    std::wstring result(text);
    for (auto &ch : result) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return result;
}

// Characters that survive the settings file, where '=', braces and "//" are syntax, and hint markup,
// where '|' separates elements and brackets mark replacements.
bool allowed(wchar_t ch) {
    if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) return true;
    return std::wstring_view(L" _-.()+!#&,'~\\/").find(ch) != std::wstring_view::npos;
}

bool printable(std::wstring_view text) { return std::ranges::all_of(text, allowed); }

// Splits a file name into its folded stem and folded extension, without the dot.
std::pair<std::wstring, std::wstring> split(const std::wstring &file) {
    const size_t dot = file.rfind(L'.');
    if (dot == std::wstring::npos) return {folded(file), {}};
    return {folded(file.substr(0, dot)), folded(file.substr(dot + 1))};
}

std::wstring in_folder(const wchar_t *directory, const std::wstring &file) {
    return std::wstring(directory) + L"\\" + file;
}

std::vector<std::wstring> loose_files(const wchar_t *directory) {
    std::vector<std::wstring> files;
    std::error_code error;
    for (std::filesystem::directory_iterator it(std::filesystem::path(directory), error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_regular_file(error)) files.push_back(it->path().filename().wstring());
    }
    return files;
}

}  // namespace

bool valid_path(std::wstring_view path) {
    if (path.empty() || !printable(path) || path.front() == L' ' || path.back() == L' ') return false;
    if (path.size() <= 5 || folded(path.substr(path.size() - 5)) != L".cmap") return false;
    size_t begin = 0;
    while (begin <= path.size()) {
        size_t end = path.find_first_of(L"\\/", begin);
        if (end == std::wstring_view::npos) end = path.size();
        const auto part = path.substr(begin, end - begin);
        if (part.empty() || part == L"." || part == L"..") return false;
        begin = end + 1;
    }
    return true;
}

std::vector<Map> merge(const std::vector<std::wstring> &packed, const std::vector<std::wstring> &loose,
                       const std::vector<std::wstring> &external, const std::vector<std::wstring> &data) {
    struct Found {
        Map map;
        bool present = false;
        std::wstring preview[2];  // jpg, png
    };
    std::map<std::wstring, Found> found;
    struct Source {
        const std::vector<std::wstring> &files;
        const wchar_t *directory;
        bool loose;
    };
    // Visit lower priorities first so higher-priority loose files replace them.
    for (const auto &source : {Source{packed, folder, false}, Source{loose, folder, true},
                               Source{data, data_folder, true}, Source{external, external_folder, true}}) {
        for (const auto &file : source.files) {
            if (!printable(file)) continue;
            const auto [stem, extension] = split(file);
            if (stem.empty()) continue;
            auto &entry = found[stem];
            if (extension == L"cmap") {
                if (entry.present && !source.loose) continue;  // The first packaged map wins, as the loader opens it.
                entry.map.name = file.substr(0, file.size() - 5);
                entry.map.path = in_folder(source.directory, file);
                entry.map.loose = source.loose;
                entry.present = true;
            }
            else if (extension == L"jpg" || extension == L"png") {
                auto &preview = entry.preview[extension == L"jpg" ? 0 : 1];
                if (preview.empty() || source.loose) preview = in_folder(source.directory, file);
            }
        }
    }
    std::vector<Map> maps;
    for (auto &[stem, entry] : found) {
        if (!entry.present || !valid_path(entry.map.path)) continue;
        entry.map.preview = !entry.preview[0].empty() ? entry.preview[0] : entry.preview[1];
        maps.push_back(std::move(entry.map));
    }
    // std::map orders by the folded stem, which is the display order.
    return maps;
}

std::vector<Map> scan() {
    return merge(Base::CFile::PackedFileNames(folder), loose_files(folder), loose_files(external_folder),
                 loose_files(data_folder));
}

int find(const std::vector<Map> &maps, std::wstring_view path) {
    const auto key = folded(path);
    for (size_t i = 0; i < maps.size(); ++i) {
        if (folded(maps[i].path) == key) return static_cast<int>(i);
    }
    return -1;
}

bool readable(const std::wstring &path) {
    if (!valid_path(path)) return false;
    try {
        Base::CStorage storage;
        if (!storage.Load(path.c_str())) return false;
        return storage.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, Base::ST_WCHAR) != nullptr &&
               storage.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, Base::ST_WCHAR) != nullptr;
    }
    catch (...) {
        return false;
    }
}

}  // namespace MapCatalog
