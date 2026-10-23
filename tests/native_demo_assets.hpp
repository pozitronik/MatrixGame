// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <filesystem>
namespace tests {
// Declared laser duel; only the intended winning side is armed.
void write_native_demo_outcome(const std::filesystem::path &path, bool player_wins);
// Stationary laser attacker with a short first and durable second fight.
void write_native_demo_fights(const std::filesystem::path &path);
// Add a single owned/enemy factory and the A8 synthetic reinforcement transport.
// Generate only before initialization, inside an existing native fixture folder.
void write_native_demo_reinforcement(const std::filesystem::path &path, bool eligible);
// Three unarmed actors with stationary chassis for component fight registration.
void write_native_demo_camera(const std::filesystem::path &path);
}
