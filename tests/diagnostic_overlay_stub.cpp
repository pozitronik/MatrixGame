// MatrixGame - licensed under GPLv2 or any later version.
#include "MatrixDebugInfo.hpp"
#include "diagnostic_capture.hpp"

#include <cstdlib>
#include <stdexcept>

namespace { tests::DiagnosticCapture *capture; }
tests::DiagnosticCapture::DiagnosticCapture() {
    if (capture) throw std::logic_error("Nested diagnostic capture");
    capture = this;
}
tests::DiagnosticCapture::~DiagnosticCapture() {
    if (capture != this) std::abort();
    capture = nullptr;
}

// Engine fixtures record diagnostics without constructing a DirectX font.
CMatrixDebugInfo::CMatrixDebugInfo() : m_Font(nullptr), m_Pos(0, 0) {}
CMatrixDebugInfo::~CMatrixDebugInfo() = default;

void CMatrixDebugInfo::T(const wchar_t *key, const wchar_t *value, int ttl, int bttl, bool append) {
    m_Items.push_back({key ? key : L"", value ? value : L"", ttl, bttl});
    if (capture) capture->events.push_back({m_Items.back(), append});
}

void CMatrixDebugInfo::Takt(int) {}
void CMatrixDebugInfo::SetStartPos(const CPoint &position) { m_Pos = position; }

void CMatrixDebugInfo::Draw() { throw std::logic_error("Unexpected diagnostic rendering in engine fixture"); }
void CMatrixDebugInfo::OnLostDevice() { throw std::logic_error("Unexpected device loss in engine fixture"); }
void CMatrixDebugInfo::OnResetDevice() { throw std::logic_error("Unexpected device reset in engine fixture"); }
