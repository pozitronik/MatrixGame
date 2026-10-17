// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <filesystem>

namespace tests {
// Materialize synthetic resources and tracked text configuration in the current
// directory. The caller owns that directory and must provide an isolated scope.
// Also writes synthetic.map. No engine session may be active during generation.
void write_native_session_assets();

// Write the same two-robot flat map under another name. Its referenced images and
// models are provided by write_native_session_assets().
void write_native_session_map(const std::filesystem::path &path);
}
