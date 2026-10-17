// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <span>
#include <filesystem>
class CVectorObject;
namespace tests {
struct MeshAttachment {
    unsigned id;
    const wchar_t *name;
    float x, y, z;
};
// Persist the same synthetic geometry for tests that let CGame::Init own loading.
void write_battle_mesh(const std::filesystem::path &path, std::span<const MeshAttachment> attachments,
    bool pneumatic = false, const wchar_t *texture = nullptr);
// Eagerly loads synthetic box geometry into the current cache, then removes its
// temporary storage file. Optional cache identity supports production LoadObject.
// Height changes only synthetic initial geometry; 20 preserves existing fixtures.
CVectorObject *load_battle_mesh(std::span<const MeshAttachment> attachments,
                               const wchar_t *cache_name = nullptr, bool combat_animations = false, float height = 20.0f);
// A single material surface for production LoadObject/skin lookup. The texture
// must already be present in the cache; this does not read external assets.
CVectorObject *load_battle_material_mesh(std::span<const MeshAttachment> attachments,
    const wchar_t *cache_name, const wchar_t *texture_name, bool combat_animations = false,
    std::span<const wchar_t *const> extra_animations = {});
}
