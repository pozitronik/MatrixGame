// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "ZeroXTrace.hpp"

namespace tests {
// Selects the zero-X trace mode for one scenario and restores the process default afterwards,
// also when a check fails.
struct ZeroXMode {
    explicit ZeroXMode(bool original) { ZeroXTrace::set_original(original); }
    ~ZeroXMode() { ZeroXTrace::set_original(ZeroXTrace::original_by_default()); }
    ZeroXMode(const ZeroXMode &) = delete;
    ZeroXMode &operator=(const ZeroXMode &) = delete;
};
} // namespace tests
