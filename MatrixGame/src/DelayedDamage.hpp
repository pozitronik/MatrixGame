// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "MatrixMapStatic.hpp"

// Attribution of ablaze and shorted damage pulses. A flamethrower or electric hit stores its
// attacking side, and each later pulse damages the target on behalf of the stored side.
namespace DelayedDamage {

// The original game clears the stored side when a pulse lands, so later pulses lose kill credit and
// friendly-fire scaling. The corrected behaviour, the default, keeps it for every pulse.
void set_original(bool original);
bool original();

// Side stored after a hit that does not start a status: pulses keep it unless the original
// behaviour is selected; every other weapon clears it.
int kept_side(EWeapon weapon, int stored_side);

}  // namespace DelayedDamage
