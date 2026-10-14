// MatrixGame - licensed under GPLv2 or any later version.
#include "DelayedDamage.hpp"
#include "Effects/MatrixEffectWeapon.hpp"

namespace DelayedDamage {
namespace {
bool original_attribution = false;
}

void set_original(bool original) {
    original_attribution = original;
}

bool original() {
    return original_attribution;
}

int kept_side(EWeapon weapon, int stored_side) {
    if (!original_attribution && (weapon == WEAPON_ABLAZE || weapon == WEAPON_SHORTED))
        return stored_side;
    return 0;
}

}  // namespace DelayedDamage
