// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <windows.h>
#include <stdexcept>

namespace tests {
// Queries are supplied so the policy can be checked without reading or moving
// the desktop pointer or creating a visible window.
template<class CursorQuery, class PanelQuery>
void require_pointer_outside_panel(bool visible, CursorQuery cursor_query, PanelQuery panel_query) {
    if (!visible) return;
    POINT cursor{};
    RECT panel{};
    if (!cursor_query(cursor)) throw std::runtime_error("Cannot read physical pointer for visible native test");
    if (!panel_query(panel) || panel.right <= panel.left || panel.bottom <= panel.top)
        throw std::runtime_error("Cannot read visible native test panel rectangle");
    if (cursor.x >= panel.left && cursor.x < panel.right && cursor.y >= panel.top && cursor.y < panel.bottom)
        throw std::runtime_error("Physical pointer entered visible native test panel; run is invalid");
}
}
