// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <filesystem>
namespace tests {
// Two nearby ordinary robots. Only the selected winning side has a laser.
void write_native_outcome_map(const std::filesystem::path &path, bool player_wins);
// Keep catalog-readable storage but omit a mandatory size property checked by Init.
void remove_native_map_width(const std::filesystem::path &path);
}
