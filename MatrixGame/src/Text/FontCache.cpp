// MatrixGame - licensed under GPLv2 or any later version.
#include "FontCache.hpp"

#include "utils.hpp"

#include <cstdint>
#include <format>
#include <stdexcept>

namespace Text {
namespace {
void check_notification(HRESULT result, std::wstring_view name, const char *operation) {
    if (FAILED(result)) {
        throw std::runtime_error(std::format("Font {} failed for {} (HRESULT=0x{:08X})",
                                            operation, utils::from_wstring(name), static_cast<uint32_t>(result)));
    }
}
}

FontCache::FontCache(IDirect3DDevice9 *device, Loader loader) {
    m_Fonts.emplace(L"Font.1Normal", loader(device, L"Verdana", 13));
    m_Fonts.emplace(L"Font.2Mini", loader(device, L"Verdana", 12));
    m_Fonts.emplace(L"Font.2Small", loader(device, L"Verdana", 13));
    m_Fonts.emplace(L"Font.2Normal", loader(device, L"Verdana", 14));
    m_Fonts.emplace(L"Font.2Ranger", loader(device, L"Rangers", 10));
}

Font &FontCache::Get(std::wstring_view name) {
    const auto found = m_Fonts.find(name);
    if (found == m_Fonts.end()) {
        throw std::runtime_error("Unknown font: " + utils::from_wstring(name));
    }
    return found->second;
}

void FontCache::OnLostDevice() {
    for (auto &[name, font] : m_Fonts) {
        if (font) {
            check_notification(font->OnLostDevice(), name, "OnLostDevice");
        }
    }
}

void FontCache::OnResetDevice() {
    for (auto &[name, font] : m_Fonts) {
        if (font) {
            check_notification(font->OnResetDevice(), name, "OnResetDevice");
        }
    }
}

}  // namespace Text
