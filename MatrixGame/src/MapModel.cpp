// MatrixGame - licensed under GPLv2 or any later version.
#include "MapModel.hpp"

#include <algorithm>
#include <cstdint>

namespace MapModel {
namespace {

constexpr uint32_t text_color = 0xFF000000;
constexpr uint32_t current_color = 0xFF3F6C8C;
constexpr uint32_t selected_color = 0xFF0B5FA5;
constexpr uint32_t arrow_color = 0xFF7D8890;
constexpr uint32_t legend_color = 0xFF5E676E;
constexpr uint32_t error_color = 0xFFB01818;

using SettingsModel::text_element;

}  // namespace

Window::Window(std::vector<MapCatalog::Map> maps, const std::wstring &current, Language language)
  : m_Maps(std::move(maps)), m_Language(language) {
    m_Current = MapCatalog::find(m_Maps, current);
    m_Selected = m_Current >= 0 ? m_Current : (m_Maps.empty() ? -1 : 0);
}

const MapCatalog::Map *Window::selected_map() const { return m_Selected < 0 ? nullptr : &m_Maps[m_Selected]; }

bool Window::select(int index) {
    if (index < 0 || index >= static_cast<int>(m_Maps.size()) || index == m_Selected) return false;
    m_Selected = index;
    m_Error = Error::None;
    return true;
}

bool Window::move(int delta) {
    if (m_Maps.empty()) return false;
    return select(std::clamp(m_Selected + delta, 0, static_cast<int>(m_Maps.size()) - 1));
}

bool Window::page(int delta) {
    if (m_Maps.empty()) return false;
    const int target = first_visible() + delta * Layout::visible_rows;
    if (target < 0) return select(0);
    if (target >= static_cast<int>(m_Maps.size())) return select(static_cast<int>(m_Maps.size()) - 1);
    return select(target);
}

const wchar_t *Window::text(const wchar_t *english, const wchar_t *russian) const {
    return m_Language == Language::Russian ? russian : english;
}

std::wstring Window::title() const { return text(L"Map", L"\u041a\u0430\u0440\u0442\u0430"); }

std::wstring Window::legend() const {
    return text(L"Enter: play the map. Esc: back", L"Enter: \u0438\u0433\u0440\u0430\u0442\u044c \u043d\u0430 \u043a\u0430\u0440\u0442\u0435. Esc: \u043d\u0430\u0437\u0430\u0434");
}

std::wstring Window::pager() const {
    if (m_Maps.empty()) return {};
    return std::to_wstring(first_visible() / Layout::visible_rows + 1) + L"/" + std::to_wstring(page_count(*this));
}

std::wstring Window::caption() const {
    const auto *map = selected_map();
    if (!map) return {};
    return map->loose ? map->name + text(L" (loose file)", L" (\u043e\u0442\u0434\u0435\u043b\u044c\u043d\u044b\u0439 \u0444\u0430\u0439\u043b)") : map->name;
}

std::wstring Window::missing_preview() const { return text(L"No preview", L"\u041d\u0435\u0442 \u0438\u0437\u043e\u0431\u0440\u0430\u0436\u0435\u043d\u0438\u044f"); }

std::wstring Window::empty_text() const { return text(L"No maps found", L"\u041a\u0430\u0440\u0442\u044b \u043d\u0435 \u043d\u0430\u0439\u0434\u0435\u043d\u044b"); }

std::wstring Window::error_text() const {
    switch (m_Error) {
        case Error::Unreadable:
            return text(L"This map cannot be loaded; choose another",
                        L"\u042d\u0442\u0443 \u043a\u0430\u0440\u0442\u0443 \u043d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c, \u0432\u044b\u0431\u0435\u0440\u0438\u0442\u0435 \u0434\u0440\u0443\u0433\u0443\u044e");
        case Error::Save:
            return text(L"The choice could not be saved; see test.log",
                        L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u0441\u043e\u0445\u0440\u0430\u043d\u0438\u0442\u044c \u0432\u044b\u0431\u043e\u0440, \u0441\u043c. test.log");
        case Error::None: break;
    }
    return {};
}

int page_count(const Window &window) {
    const int count = static_cast<int>(window.maps().size());
    return std::max(1, (count + Layout::visible_rows - 1) / Layout::visible_rows);
}

Hit hit(const Window &window, int x, int y) {
    if (y >= Layout::pager_y && y < Layout::pager_y + Layout::row_height) {
        if (x >= Layout::previous_x && x < Layout::previous_x + Layout::pager_width) return {-1, -1};
        if (x >= Layout::next_x && x < Layout::next_x + Layout::pager_width) return {-1, 1};
        return {};
    }
    if (x < Layout::list_x || x >= Layout::list_x + Layout::list_width || y < Layout::first_row_y) return {};
    const int row = (y - Layout::first_row_y) / Layout::row_height;
    const int index = window.first_visible() + row;
    if (row >= Layout::visible_rows || index >= static_cast<int>(window.maps().size())) return {};
    return {index, 0};
}

SettingsModel::Resolution preview_size(int width, int height) {
    if (width <= 0 || height <= 0) return {0, 0};
    // Scale by the more limiting side; smaller images are enlarged to the box as well.
    if (static_cast<long long>(width) * Layout::preview_height >= static_cast<long long>(height) * Layout::preview_width)
        return {Layout::preview_width, std::max(1, static_cast<int>(static_cast<long long>(height) * Layout::preview_width / width))};
    return {std::max(1, static_cast<int>(static_cast<long long>(width) * Layout::preview_height / height)), Layout::preview_height};
}

std::wstring hint_template(const Window &window, SettingsModel::Resolution preview) {
    // The same frame, blank sizing block and transparent button holes as the settings window.
    std::wstring result = L"2|_BORDER:0:0:0:28|_FONT:Font.2Normal|_WIDTH:" + std::to_wstring(Layout::width) +
                          L"|_HEIGHT:" + std::to_wstring(Layout::content_height) +
                          L"|_TEXTP:L|_TEXT: |_MOD:COPY|_BITMAP:bhole2:CL:6|_MOD:COPY|_BITMAP:bhole2:CR:6|_TEXTP:B";
    result += text_element(window.title(), L"Font.2Ranger", text_color, 0, Layout::title_y, Layout::width, 1);
    const auto &maps = window.maps();
    if (maps.empty()) {
        result += text_element(window.empty_text(), L"Font.2Normal", text_color, Layout::list_x, Layout::first_row_y,
                               Layout::list_width, 0);
    }
    const int first = window.first_visible();
    for (int row = 0; row < Layout::visible_rows && first + row < static_cast<int>(maps.size()); ++row) {
        const int index = first + row;
        const uint32_t color = index == window.selected() ? selected_color
                               : window.is_current(index) ? current_color
                                                          : text_color;
        result += text_element(maps[index].name, L"Font.2Normal", color, Layout::list_x,
                               Layout::first_row_y + row * Layout::row_height, Layout::list_width, 0);
    }
    if (page_count(window) > 1) {
        result += text_element(L"<", L"Font.2Normal", arrow_color, Layout::previous_x, Layout::pager_y,
                               Layout::pager_width, 0);
        result += text_element(window.pager(), L"Font.2Small", legend_color, Layout::previous_x + Layout::pager_width,
                               Layout::pager_y, Layout::list_width - 2 * Layout::pager_width, 1);
        result += text_element(L">", L"Font.2Normal", arrow_color, Layout::next_x, Layout::pager_y,
                               Layout::pager_width, 0);
    }
    if (window.selected_map()) {
        if (preview.width > 0) {
            result += L"|_POS:" + std::to_wstring(Layout::preview_x + (Layout::preview_width - preview.width) / 2) +
                      L":" + std::to_wstring(Layout::preview_y + (Layout::preview_height - preview.height) / 2) +
                      L"|_BITMAP:" + preview_bitmap + L":B";
        }
        else {
            result += text_element(window.missing_preview(), L"Font.2Small", legend_color, Layout::preview_x,
                                   Layout::preview_y + Layout::preview_height / 2 - Layout::row_height / 2,
                                   Layout::preview_width, 1);
        }
        result += text_element(window.caption(), L"Font.2Normal", selected_color, Layout::preview_x,
                               Layout::caption_y, Layout::preview_width, 1);
    }
    result += text_element(window.legend(), L"Font.2Small", legend_color, Layout::list_x, Layout::legend_y,
                           Layout::preview_x + Layout::preview_width - Layout::list_x, 0);
    if (window.error() != Error::None) {
        result += text_element(window.error_text(), L"Font.2Small", error_color, Layout::list_x,
                               Layout::legend_y + Layout::row_height,
                               Layout::preview_x + Layout::preview_width - Layout::list_x, 0);
    }
    return result;
}

}  // namespace MapModel
