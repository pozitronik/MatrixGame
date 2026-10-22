// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <windows.h>
#include <cstdint>

namespace tests::native_session {
inline constexpr UINT snapshot_message = WM_APP + 0x385;
inline constexpr wchar_t environment[] = L"MATRIXGAME_NATIVE_TEST";
inline constexpr LONG version = 2;
inline constexpr int capacity = 16;
struct Snapshot {
    LONG request{};
    wchar_t form[40]{}, dialog[80]{}, map[260]{};
    int time{}, paused{}, selector{}, initialized{}, direct_start{}, parent_handshake{}, exit_state{};
    int robots{};
    int friendly_robots{}, enemy_robots{}, friendly_weapons{}, enemy_weapons{}, pending_result{}, win{};
    int status_scan_timestamp{}, result_countdown{};
    float friendly_health{}, enemy_health{};
    int hints{}, settings{}, saved_camera{}, saved_skybox{}, saved_random{}, runtime_skybox{}, runtime_random{}, restart_pending{};
    float runtime_camera{};
    DWORD flags{};
};
struct Process {
    volatile LONG id{}, window{}, diagnostics{}, snapshot_generation{}, request{};
    wchar_t diagnostic_title[80]{}, diagnostic_text[2048]{};
    Snapshot snapshot;
};
struct Shared {
    LONG protocol = version;
    Process processes[capacity];
};
}
