// MatrixGame - licensed under GPLv2 or any later version.
#include "ResourceFiles.hpp"

namespace Startup {

bool ResourceFileExists(const std::filesystem::path &path) {
    return std::filesystem::is_regular_file(path);
}

} // namespace Startup
