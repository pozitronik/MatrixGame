// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "StandaloneSettings.hpp"

struct SRobotsSettings;

// Connects standalone options to the engine. Restart-only options are read once at startup;
// the remaining options are applied immediately when the settings window confirms a change.
namespace StandaloneSettings {

// Copies startup-only graphics options before the engine reads its host-style settings.
void apply_startup(const Values &values, SRobotsSettings &settings);

// Records the configured camera speed and applies immediate options. Call after g_Config is loaded.
void initialize_runtime(const Values &values);

// Applies options that take effect during a battle and records them as the saved values.
void apply_runtime(const Values &values);

// Options most recently loaded or applied; the settings window edits a copy.
const Values &current();

// True when the saved value differs from the one the running game started with.
bool restart_pending(const Values &values);

}  // namespace StandaloneSettings
