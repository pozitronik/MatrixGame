// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "MapModel.hpp"
#include "stupid_logger.hpp"

logger_type lgr{std::cerr};

namespace {

using namespace MapModel;
using MapCatalog::Map;

std::vector<Map> maps(int count) {
    std::vector<std::wstring> names;
    for (int i = 0; i < count; ++i) {
        wchar_t name[16];
        swprintf(name, 16, L"map%02d.cmap", i);
        names.push_back(name);
    }
    names.push_back(L"map03.jpg");
    return MapCatalog::merge(names, {L"map05.cmap"});
}

std::wstring path(int index) {
    wchar_t name[32];
    swprintf(name, 32, L"Matrix\\Map\\map%02d.cmap", index);
    return name;
}

size_t count(const std::wstring &text, const std::wstring &part) {
    size_t result = 0;
    for (size_t at = text.find(part); at != std::wstring::npos; at = text.find(part, at + part.size())) ++result;
    return result;
}

void selection() {
    Window current(maps(5), L"matrix\\map\\MAP03.cmap", Language::English);
    MG_CHECK(current.selected() == 3 && current.is_current(3) && current.selected_map()->path == path(3));
    Window fallback(maps(5), L"Matrix\\Map\\absent.cmap", Language::English);
    MG_CHECK(fallback.selected() == 0 && !fallback.is_current(0));
    MG_CHECK(!fallback.move(-1) && fallback.move(2) && fallback.selected() == 2);
    // Five packaged maps and the loose map05.
    MG_CHECK(fallback.maps().size() == 6 && fallback.move(100) && fallback.selected() == 5 && !fallback.move(1));
    MG_CHECK(fallback.select(1) && !fallback.select(1) && !fallback.select(6) && !fallback.select(-1));

    Window empty({}, L"Matrix\\Map\\absent.cmap", Language::English);
    MG_CHECK(empty.selected() == -1 && empty.selected_map() == nullptr && empty.first_visible() == 0);
    MG_CHECK(!empty.move(1) && !empty.page(1) && !empty.select(0) && empty.pager().empty() && page_count(empty) == 1);

    // Changing the selection clears an error about the previous map.
    fallback.set_error(Error::Unreadable);
    MG_CHECK(fallback.move(1) && fallback.error() == Error::None);
}

void paging() {
    Window window(maps(30), path(20), Language::English);
    MG_CHECK(page_count(window) == 3 && window.first_visible() == 14 && window.pager() == L"2/3");
    MG_CHECK(window.page(1) && window.selected() == 28 && window.first_visible() == 28 && window.pager() == L"3/3");
    MG_CHECK(window.page(1) && window.selected() == 29 && !window.page(1));
    MG_CHECK(window.page(-1) && window.selected() == 14 && window.page(-1) && window.selected() == 0);
    MG_CHECK(!window.page(-1));
    MG_CHECK(window.move(13) && window.first_visible() == 0 && window.move(1) && window.first_visible() == 14);
    MG_CHECK(page_count(Window(maps(14), L"", Language::English)) == 1);
    MG_CHECK(page_count(Window(maps(15), L"", Language::English)) == 2);
}

void geometry() {
    Window window(maps(30), path(16), Language::English);  // Second page: maps 14-27.
    const int x = Layout::list_x + 5;
    MG_CHECK(hit(window, x, Layout::first_row_y).index == 14);
    MG_CHECK(hit(window, x, Layout::first_row_y + 3 * Layout::row_height + 19).index == 17);
    MG_CHECK(hit(window, x, Layout::first_row_y - 1).index == -1);
    MG_CHECK(hit(window, Layout::list_x - 1, Layout::first_row_y).index == -1);
    MG_CHECK(hit(window, Layout::list_x + Layout::list_width, Layout::first_row_y).index == -1);
    MG_CHECK(hit(window, Layout::previous_x, Layout::pager_y).page == -1);
    MG_CHECK(hit(window, Layout::next_x + Layout::pager_width - 1, Layout::pager_y + 1).page == 1);
    MG_CHECK(hit(window, Layout::list_x + Layout::list_width / 2, Layout::pager_y).page == 0);
    MG_CHECK(hit(window, Layout::preview_x, Layout::preview_y).index == -1);
    window.page(1);  // The last page has two maps; rows below them hit nothing.
    MG_CHECK(hit(window, x, Layout::first_row_y + Layout::row_height).index == 29);
    MG_CHECK(hit(window, x, Layout::first_row_y + 2 * Layout::row_height).index == -1);
    MG_CHECK(Layout::content_height + 21 + 28 <= 480);  // Fits the smallest supported resolution.
    MG_CHECK(Layout::pager_y + Layout::row_height <= Layout::legend_y);
    MG_CHECK(Layout::caption_y + Layout::row_height <= Layout::legend_y);

    MG_CHECK(preview_size(400, 400) == SettingsModel::Resolution({200, 200}));
    MG_CHECK(preview_size(512, 256) == SettingsModel::Resolution({200, 100}));
    MG_CHECK(preview_size(100, 300) == SettingsModel::Resolution({66, 200}));
    MG_CHECK(preview_size(50, 50) == SettingsModel::Resolution({200, 200}));
    MG_CHECK(preview_size(1000, 1) == SettingsModel::Resolution({200, 1}));
    MG_CHECK(preview_size(0, 10) == SettingsModel::Resolution({0, 0}));
}

void text() {
    Window english(maps(6), path(5), Language::English);
    MG_CHECK(english.title() == L"Map" && english.caption() == L"map05 (loose file)");
    english.move(-1);
    MG_CHECK(english.caption() == L"map04" && english.missing_preview() == L"No preview");
    MG_CHECK(english.error_text().empty());
    english.set_error(Error::Unreadable);
    MG_CHECK(english.error_text() == L"This map cannot be loaded; choose another");
    english.set_error(Error::Save);
    MG_CHECK(english.error_text().ends_with(L"test.log"));

    Window russian(maps(6), path(5), Language::Russian);
    MG_CHECK(SettingsModel::detect_language(russian.title()) == Language::Russian);
    MG_CHECK(SettingsModel::detect_language(russian.legend()) == Language::Russian);
    MG_CHECK(SettingsModel::detect_language(russian.caption()) == Language::Russian);
    MG_CHECK(SettingsModel::detect_language(russian.missing_preview()) == Language::Russian);
    MG_CHECK(SettingsModel::detect_language(Window({}, L"", Language::Russian).empty_text()) == Language::Russian);
    for (const auto error : {Error::Unreadable, Error::Save}) {
        russian.set_error(error);
        MG_CHECK(SettingsModel::detect_language(russian.error_text()) == Language::Russian);
    }
}

void hint_text() {
    Window window(maps(30), path(3), Language::English);
    const auto hint = hint_template(window, {0, 0});
    MG_CHECK(hint.starts_with(L"2|_BORDER:0:0:0:28|"));
    MG_CHECK(count(hint, L"_MOD:COPY") == 2);  // OK and Cancel hint buttons.
    // Only the visible page is listed; the selected map is highlighted.
    MG_CHECK(hint.find(L"|_TEXT:map13") != std::wstring::npos && hint.find(L"|_TEXT:map14") == std::wstring::npos);
    MG_CHECK(hint.find(L"_COLOR:FF0B5FA5|_WIDTH:230|_HEIGHT:19|_ALIGN:0:0|_POS:40:118|_TEXT:map03") !=
             std::wstring::npos);
    MG_CHECK(hint.find(L"|_TEXT:1/3") != std::wstring::npos && hint.find(L"|_TEXT:No preview") != std::wstring::npos);
    MG_CHECK(hint.find(MapModel::preview_bitmap) == std::wstring::npos);
    // Every text element is positioned; the blank block is the only flowing text.
    MG_CHECK(count(hint, L"|_TEXT:") == count(hint, L"|_POS:") + 1);
    MG_CHECK(hint.find(L"cannot be loaded") == std::wstring::npos);

    window.set_error(Error::Unreadable);
    const auto preview = hint_template(window, {200, 100});
    MG_CHECK(preview.find(L"|_POS:290:108|_BITMAP:MapPreview") != std::wstring::npos);
    MG_CHECK(preview.find(L"No preview") == std::wstring::npos && preview.find(L"cannot be loaded") != std::wstring::npos);

    // A single page has no pager, and an empty list shows a notice instead of names.
    const auto single = hint_template(Window(maps(3), L"", Language::English), {0, 0});
    MG_CHECK(single.find(L"|_TEXT:<") == std::wstring::npos && single.find(L"|_TEXT:1/1") == std::wstring::npos);
    const auto empty = hint_template(Window({}, L"", Language::English), {0, 0});
    MG_CHECK(empty.find(L"|_TEXT:No maps found") != std::wstring::npos && empty.find(L"No preview") == std::wstring::npos);
}

constexpr tests::Case cases[] = {
        {"game.maps.window_selection", selection}, {"game.maps.window_paging", paging},
        {"game.maps.window_geometry", geometry},   {"game.maps.window_text", text},
        {"game.maps.window_hint", hint_text},
};

}  // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
