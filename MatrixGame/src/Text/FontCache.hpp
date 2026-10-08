// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "Font.hpp"

#include <map>

namespace Text {

// Owns the fixed UI font set for one device; the loader also supports asset-free tests.
class FontCache {
public:
    using Loader = LPD3DXFONT (*)(IDirect3DDevice9 *, std::wstring_view, size_t);

    FontCache(IDirect3DDevice9 *device, Loader loader);
    FontCache(const FontCache &) = delete;
    FontCache &operator=(const FontCache &) = delete;

    Font &Get(std::wstring_view name);
    void OnLostDevice();
    void OnResetDevice();

private:
    std::map<std::wstring_view, Font> m_Fonts;
};

}  // namespace Text
