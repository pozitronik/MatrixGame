// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "MapCatalog.hpp"
#include "SettingsModel.hpp"

#include <string>
#include <vector>

// Device-independent state, text and geometry of the in-game Map window: a paged list of the installed
// maps beside a preview of the selected one.
namespace MapModel {

using SettingsModel::Language;

// Name under which the window registers the selected map's preview for its hint template.
inline constexpr const wchar_t *preview_bitmap = L"MapPreview";

// Window geometry in pixels, relative to the window's top-left corner.
struct Layout {
    static constexpr int width = 520;
    static constexpr int title_y = 30;
    static constexpr int first_row_y = 58;
    static constexpr int row_height = 20;
    static constexpr int visible_rows = 14;
    static constexpr int list_x = 40;
    static constexpr int list_width = 230;
    static constexpr int pager_y = first_row_y + visible_rows * row_height + 4;
    static constexpr int previous_x = list_x;  // "<" steps back one page.
    static constexpr int next_x = list_x + list_width - 20;  // ">" steps forward one page.
    static constexpr int pager_width = 20;
    static constexpr int preview_x = 290;
    static constexpr int preview_y = first_row_y;
    static constexpr int preview_width = 200;
    static constexpr int preview_height = 200;
    static constexpr int caption_y = preview_y + preview_height + 8;
    static constexpr int legend_y = pager_y + row_height + 6;
    static constexpr int content_height = legend_y + 2 * row_height;
};

struct Hit {
    int index = -1;  // Map under the point, or -1.
    int page = 0;    // -1 or +1 when a pager arrow was hit.
};

enum class Error { None, Unreadable, Save };

class Window {
public:
    // Selects the current map when it is listed, otherwise the first map.
    Window(std::vector<MapCatalog::Map> maps, const std::wstring &current, Language language);

    const std::vector<MapCatalog::Map> &maps() const { return m_Maps; }
    int selected() const { return m_Selected; }  // -1 when the list is empty.
    const MapCatalog::Map *selected_map() const;
    int first_visible() const { return m_Selected < 0 ? 0 : m_Selected / Layout::visible_rows * Layout::visible_rows; }
    bool is_current(int index) const { return index == m_Current; }

    // Each returns true when the selection changed; the list does not wrap.
    bool select(int index);
    bool move(int delta);
    bool page(int delta);

    void set_error(Error error) { m_Error = error; }
    Error error() const { return m_Error; }

    std::wstring title() const;
    std::wstring legend() const;
    std::wstring pager() const;  // "page/pages"
    std::wstring caption() const;  // Selected map name, marked when it is a loose file.
    std::wstring missing_preview() const;
    std::wstring empty_text() const;
    std::wstring error_text() const;

private:
    std::vector<MapCatalog::Map> m_Maps;
    int m_Selected = -1;
    int m_Current = -1;
    Language m_Language;
    Error m_Error = Error::None;

    const wchar_t *text(const wchar_t *english, const wchar_t *russian) const;
};

int page_count(const Window &window);
Hit hit(const Window &window, int x, int y);

// Size of a width x height image scaled to fit the preview box with its aspect ratio kept; zero for
// an empty image.
SettingsModel::Resolution preview_size(int width, int height);

// CMatrixHint template: the frame and button holes of the settings window, the visible page of names,
// the pager, and the preview bitmap centred in its box, or a placeholder when preview has no size.
std::wstring hint_template(const Window &window, SettingsModel::Resolution preview);

}  // namespace MapModel
