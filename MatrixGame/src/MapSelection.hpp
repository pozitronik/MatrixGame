// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "StandaloneSettings.hpp"

#include <string>

// Starting the standalone game on a chosen map. A battle runs once per process, so a new choice is
// saved as the remembered map and the game is started again.
namespace MapSelection {

// Returns the map to start instead of Config/Map: the remembered map when it can be read, otherwise
// an empty string. An unreadable remembered map is reported, forgotten in the settings file and in
// values, so the configured map starts.
std::wstring startup_map(StandaloneSettings::Values &values);

// True after startup_map selected the remembered map for this process.
bool remembered_map_started();

// Forgets the remembered map in the settings file after it failed during battle initialization.
// Returns true when the file was updated.
bool forget_remembered_map();

// Saves values with map as the remembered map and asks the game to start again once this process
// has shut down. Returns false and fills error when the settings file cannot be written.
bool choose(StandaloneSettings::Values values, const std::wstring &map, std::string &error);

bool relaunch_requested();

// Starts a new instance of the executable without arguments. Call after the session is cleaned up.
bool relaunch();

}  // namespace MapSelection
