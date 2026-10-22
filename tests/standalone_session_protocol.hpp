// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <windows.h>
#include <cstdint>

namespace tests::native_session {
inline constexpr UINT snapshot_message = WM_APP + 0x385;
inline constexpr UINT diagnostic_snapshot_message = snapshot_message + 1;
inline constexpr wchar_t environment[] = L"MATRIXGAME_NATIVE_TEST";
inline constexpr LONG version = 4;
inline constexpr int capacity = 16;
struct Snapshot {
    LONG request{};
    wchar_t form[40]{}, dialog[80]{}, map[260]{};
    int time{}, paused{}, selector{}, initialized{}, direct_start{}, parent_handshake{}, exit_state{};
    int robots{};
    int friendly_robots{}, enemy_robots{}, friendly_weapons{}, enemy_weapons{}, pending_result{}, win{};
    int status_scan_timestamp{}, result_countdown{};
    float friendly_health{}, enemy_health{};
    float friendly_x{}, friendly_y{}, destination_x{}, destination_y{}, trace_x{}, trace_y{};
    int selected{}, friendly_move{}, move_preorder{}, interface_focus{};
    int trace_land{}, relaunch_requested{};
    float order_x{}, order_y{};
    int client_width{}, client_height{}, render_width{}, render_height{}, cursor_x{}, cursor_y{};
    int hints{}, settings{}, saved_camera{}, saved_skybox{}, saved_random{}, runtime_skybox{}, runtime_random{}, restart_pending{};
    float runtime_camera{};
    DWORD flags{};
    DWORD map_flags{};
    HRESULT device_status{E_PENDING};
    int draw_fps{}, backbuffer_width{}, backbuffer_height{}, device_windowed{};
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
