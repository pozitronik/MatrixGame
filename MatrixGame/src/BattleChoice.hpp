// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

namespace BattleChoice {

enum class Bonus { None, AirSupport, ReinforcedArmor };

// Apply only the two supported bonus fields. Friendly fire belongs to the existing battle contract.
template<class Difficulty>
void apply(Bonus bonus, Difficulty &difficulty) {
    difficulty.k_damage_enemy_to_player = bonus == Bonus::ReinforcedArmor ? 0.5f : 1.0f;
    difficulty.k_time_before_maintenance = bonus == Bonus::AirSupport ? 0.33f : 1.0f;
}

}  // namespace BattleChoice
