// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <filesystem>

namespace tests {
// Materialize synthetic resources and tracked text configuration in the current
// directory. The caller owns that directory and must provide an isolated scope.
// Also writes synthetic.map. No engine session may be active during generation.
void write_native_session_assets();

// Write the same two-robot flat map under another name. Optional populated
// profiles require the matching building/cannon asset helpers below.
void write_native_session_map(const std::filesystem::path &path, bool with_buildings = false, bool with_cannons = false);

// Add synthetic base/factory models and long resource intervals for reload tests.
// Call after write_native_session_assets(), before loading a with_buildings map.
void write_native_session_building_assets();
// Add cannon model chains for the occupied/empty-slot profile.
void write_native_session_cannon_assets();
}
