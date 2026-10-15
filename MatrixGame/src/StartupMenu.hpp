// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "StandaloneSettings.hpp"
#include <string_view>

namespace StartupMenu {

void set_direct_start(bool enabled);
bool direct_start();

// Runs on the existing device, before any battlefield or battle interface is constructed.
// Returns false for Exit/window close. Start updates values after successful validation and saving.
bool run(StandaloneSettings::Values &values);

}  // namespace StartupMenu
