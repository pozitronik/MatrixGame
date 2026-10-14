// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <windows.h>
#include <string>
#include <string_view>

namespace BattleLoading {

// Independent of the renderer, game heap and map lifetime. A worker owns the
// loading window so it continues painting while the main thread loads or exits.
void show(HWND game_window, bool russian);
void hide() noexcept;

// Private relaunch handshake. The old process keeps its loading screen until
// the new process presents a battle frame, reports a failure, or terminates.
std::wstring event_name();
bool valid_event_name(std::wstring_view name);
void connect(std::wstring_view name);
bool has_parent();
void presented(HRESULT result);
void failed() noexcept;
bool wait(HANDLE ready, HANDLE process);

} // namespace BattleLoading
