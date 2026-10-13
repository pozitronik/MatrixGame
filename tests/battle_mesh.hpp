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
// Height changes only synthetic initial geometry; 20 preserves existing fixtures.
CVectorObject *load_battle_mesh(std::span<const MeshAttachment> attachments,
                               const wchar_t *cache_name = nullptr, bool combat_animations = false, float height = 20.0f);
}
