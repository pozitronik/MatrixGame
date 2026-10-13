// MatrixGame - licensed under GPLv2 or any later version.
#include "MatrixMap.hpp"
#include <cstdlib>

// Presentation-only surrogate for the no-render profile. Keep requested cursor
// identity/visibility and position, without loading icons or changing the OS
// cursor counter. Construction and Clear still save/restore the Win32 cursor
// handle. Picking, selection and command dispatch remain production code.
void CMatrixCursor::Select(const std::wstring &name) { m_CurCursor = name; }
void CMatrixCursor::SetVisible(bool value) { INITFLAG(m_CursorFlags, CURSOR_VISIBLE, value); }
void CMatrixCursor::Takt(int ms) { m_Time += ms; }
void CMatrixCursor::Clear() {
    SetCursor(m_OldCursor);
}
void CMatrixCursor::Draw() const { std::abort(); }
