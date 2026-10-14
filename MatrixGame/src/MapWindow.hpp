// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "StandaloneSettings.hpp"

#include <cstdint>

// In-game Map window of the standalone game. It lists the installed maps with previews; choosing one
// saves it as the remembered map and starts the game again on it.
namespace MapWindow {

// Where the window returns when it is canceled.
enum class Origin {
    Settings,   // The Settings window's Map row; Cancel returns to Settings.
    BattleEnd,  // After the statistics of a finished battle; Cancel exits the game as before.
};

// True in the standalone game; the DLL host owns map selection.
bool available();
bool active();

// Opens the window over the battle. values are saved together with the chosen map.
void open(Origin origin, const StandaloneSettings::Values &values);
// Opens the window after a finished battle with the current settings. Returns false when unavailable.
bool open_after_battle();
// Plays the selected map or returns to the window's origin.
void close(bool play);

// Input while the window is active. Each returns true when the event was consumed.
bool keyboard(uint8_t vk, bool down);
bool mouse_down(int x, int y);
void mouse_wheel(int steps);

// Rebuilds an open window at the centre of the current screen after a resolution change.
void relayout();

// Releases the preview and window state before the session's hints and heap are destroyed.
void shutdown();

}  // namespace MapWindow
