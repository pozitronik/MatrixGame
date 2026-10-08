// MatrixGame - licensed under GPLv2 or any later version.
#include "Text/Font.hpp"

// Deterministic metrics isolate production tokenization and layout from GDI/DirectX.
namespace Text {
Font::Font(LPD3DXFONT font) : m_font(font) {}
Font::~Font() = default;
size_t Font::CalcTextWidth(std::wstring_view text) const { return text.size() * 6; }
size_t Font::GetSpaceWidth() const { return 3; }
size_t Font::GetHeight() const { return 10; }
}
