// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <string>

class CMatrixHint;

// In-game settings dialog for the standalone game. It replaces the battle menu while open,
// keeps the battle paused and returns to the menu when applied or canceled.
namespace SettingsWindow {

// True in the standalone game; the DLL host owns its settings.
bool available();
bool active();
void open();
// Applies or discards the edits and returns to the battle menu.
void close(bool apply);

// Input while the dialog is active. Each returns true when the event was consumed.
bool keyboard(uint8_t vk, bool down);
void mouse_move(int x, int y);
bool mouse_down(int x, int y);

// Battle menu integration: enables the menu's settings entry and handles its click area.
void customize_menu(std::wstring &menu_template);
bool menu_mouse_down(int x, int y);
void menu_mouse_move(int x, int y);
// Releases the menu highlight; called whenever a dialog closes.
void menu_closed();
// Releases dialog state before the session's hints and heap are destroyed.
void shutdown();

}  // namespace SettingsWindow
