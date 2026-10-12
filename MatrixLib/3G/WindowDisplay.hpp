// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include "DisplaySettings.hpp"

namespace Display {
void initialize(Settings settings);
void shutdown();
bool enabled();
bool changing();
Mode current_mode();
bool supports_exclusive();
void apply_window(Mode mode);
void configure_parameters(D3DPRESENT_PARAMETERS &parameters);
HRESULT reset_device(const D3DPRESENT_PARAMETERS &parameters);
Mode fullscreen_mode();
// Queues a switch to target (applied by apply_pending) and sets the Alt+Enter fullscreen mode.
void request(Mode target, Mode fullscreen);
bool handle_shortcut(UINT message, WPARAM key, LPARAM flags);
void focus_lost();
void confine_cursor();
bool apply_pending(bool resources_ready, void (*release)(), void (*restore)());
void map_mouse(UINT message, int &x, int &y);
} // namespace Display
