// MatrixGame - licensed under GPLv2 or any later version.
#include "MatrixDebugInfo.hpp"

#include <stdexcept>

// The command fixture records diagnostics without constructing a DirectX font.
CMatrixDebugInfo::CMatrixDebugInfo() : m_Font(nullptr), m_Pos(0, 0) {}
CMatrixDebugInfo::~CMatrixDebugInfo() = default;

void CMatrixDebugInfo::T(const wchar_t *key, const wchar_t *value, int ttl, int bttl, bool) {
    m_Items.push_back({key ? key : L"", value ? value : L"", ttl, bttl});
}

void CMatrixDebugInfo::Takt(int) {}
void CMatrixDebugInfo::SetStartPos(const CPoint &position) { m_Pos = position; }

void CMatrixDebugInfo::Draw() { throw std::logic_error("Unexpected diagnostic rendering in command fixture"); }
void CMatrixDebugInfo::OnLostDevice() { throw std::logic_error("Unexpected device loss in command fixture"); }
void CMatrixDebugInfo::OnResetDevice() { throw std::logic_error("Unexpected device reset in command fixture"); }
