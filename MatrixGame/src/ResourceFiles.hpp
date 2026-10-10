// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <filesystem>

namespace Startup {

// Standalone packages are real files, independent of the archive collection.
bool ResourceFileExists(const std::filesystem::path &path);

} // namespace Startup
