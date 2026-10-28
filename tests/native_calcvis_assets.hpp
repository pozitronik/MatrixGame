// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <filesystem>

class CMatrixMap;
namespace Base {
class CBlockPar;
}
namespace tests {
// Test-only all-land input. Existing source maps and production code are unchanged.
void write_native_calcvis_map(const std::filesystem::path &path, bool ridge);
void configure_native_calcvis();
void validate_native_calcvis_configuration(Base::CBlockPar &data);
void validate_native_calcvis(const std::filesystem::path &path, bool ridge);
// Check actual loaded state before the legacy algorithm can read any temporary cell.
void check_calcvis_runtime(const CMatrixMap &map);
}
