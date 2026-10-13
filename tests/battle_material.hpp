// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "Texture.hpp"
#include "test_support.hpp"
#include <array>
#include <cstring>

namespace tests {
// Skin preparation selects fixed-function callbacks from the advertised caps.
// Their draw calls are deliberately outside the no-render profile.
struct BattleMaterialProfile {
    DWORD previous_texture_stages = g_D3DDCaps.MaxSimultaneousTextures;
    BattleMaterialProfile() { g_D3DDCaps.MaxSimultaneousTextures = 2; }
    ~BattleMaterialProfile() { g_D3DDCaps.MaxSimultaneousTextures = previous_texture_stages; }
    BattleMaterialProfile(const BattleMaterialProfile &) = delete;
    BattleMaterialProfile &operator=(const BattleMaterialProfile &) = delete;
};

// Opaque 2x2 ARGB data uploaded through the production managed-texture wrapper.
// CacheDeinit owns its lifetime; the map destroys skins before cache teardown.
inline CTextureManaged *install_battle_texture(const wchar_t *name) {
    MG_CHECK(name && *name);
    MG_CHECK(g_Cache->Find(CacheClass::TextureManaged, name) == nullptr);
    auto *texture = static_cast<CTextureManaged *>(g_Cache->Get(CacheClass::TextureManaged, name));
    D3DLOCKED_RECT locked{};
    MG_CHECK(texture->CreateLock(D3DFMT_A8R8G8B8, 2, 2, 1, locked) == D3D_OK);
    const std::array<DWORD, 4> pixels{0xff204060, 0xff608020, 0xffa04080, 0xffe0c0a0};
    for (int y = 0; y < 2; ++y)
        std::memcpy(static_cast<BYTE *>(locked.pBits) + y * locked.Pitch,
                    pixels.data() + y * 2, 2 * sizeof(DWORD));
    texture->UnlockRect();
    texture->Preload();
    return texture;
}
}
