// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <span>
class CVectorObject;
namespace tests {
struct MeshAttachment {
    unsigned id;
    const wchar_t *name;
    float x, y, z;
};
// Eagerly loads synthetic box geometry into the current cache, then removes its
// temporary storage file. Optional cache identity supports production LoadObject.
CVectorObject *load_battle_mesh(std::span<const MeshAttachment> attachments,
                               const wchar_t *cache_name = nullptr, bool combat_animations = false);
// A single material surface for production LoadObject/skin lookup. The texture
// must already be present in the cache; this does not read external assets.
CVectorObject *load_battle_material_mesh(std::span<const MeshAttachment> attachments,
    const wchar_t *cache_name, const wchar_t *texture_name, bool combat_animations = false);
}
