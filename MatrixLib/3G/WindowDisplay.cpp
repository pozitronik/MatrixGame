// MatrixGame - licensed under GPLv2 or any later version.
#include "WindowDisplay.hpp"
#include "3g.hpp"
#include "stupid_logger.hpp"

#include <algorithm>
#include <optional>

namespace Display {
namespace {
Settings options;
Mode mode = Mode::Windowed;
bool active = false;
bool in_change = false;
bool pending = false;
std::optional<Mode> requested;  // Explicit target; otherwise a pending change toggles.
Shortcut shortcut;
RECT windowed_rectangle{};
bool have_windowed_rectangle = false;

struct ChangeGuard {
    bool previous = in_change;
    ChangeGuard() { in_change = true; }
    ~ChangeGuard() { in_change = previous; }
};

MONITORINFO monitor() {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfo(g_D3D->GetAdapterMonitor(D3DADAPTER_DEFAULT), &info))
        ERROR_S(L"Cannot read the primary display dimensions");
    return info;
}

void sync_cursor() {
    POINT cursor{};
    if (FLAG(g_Flags, GFLAG_APPACTIVE) && GetCursorPos(&cursor) && ScreenToClient(g_Wnd, &cursor)) {
        int x = cursor.x, y = cursor.y;
        map_mouse(WM_MOUSEMOVE, x, y);
        g_FormCur->MouseMove(x, y);
    }
}
}

void initialize(Settings settings) {
    shutdown();
    active = true;
    options = settings;
    mode = settings.mode;
}

void shutdown() {
    active = false;
    pending = false;
    requested.reset();
    in_change = false;
    shortcut = {};
    have_windowed_rectangle = false;
    options = {};
    mode = Mode::Windowed;
}

bool enabled() { return active; }
bool changing() { return in_change; }
Mode current_mode() { return mode; }
Mode fullscreen_mode() { return options.fullscreen; }

void request(Mode target, Mode fullscreen) {
    if (!active || fullscreen == Mode::Windowed) return;
    options.fullscreen = fullscreen;
    requested = target;
    pending = target != mode;
}

bool supports_exclusive() {
    const UINT count = g_D3D->GetAdapterModeCount(D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8);
    for (UINT i = 0; i < count; ++i) {
        D3DDISPLAYMODE candidate{};
        if (SUCCEEDED(g_D3D->EnumAdapterModes(D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8, i, &candidate)) &&
            candidate.Width == static_cast<UINT>(options.width) && candidate.Height == static_cast<UINT>(options.height))
            return true;
    }
    return false;
}

void apply_window(Mode target) {
    ChangeGuard guard;
    if (mode == Mode::Windowed && target != Mode::Windowed) {
        if (!GetWindowRect(g_Wnd, &windowed_rectangle)) ERROR_S(L"Cannot save the game window position");
        have_windowed_rectangle = true;
    }
    const auto info = monitor();
    const LONG style = target == Mode::Windowed ? WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX : WS_POPUP;
    SetLastError(0);
    if (!SetWindowLongPtr(g_Wnd, GWL_STYLE, style | (GetWindowLongPtr(g_Wnd, GWL_STYLE) & WS_VISIBLE)) && GetLastError())
        ERROR_S(L"Cannot change the game window style");
    RECT rectangle = info.rcMonitor;
    if (target == Mode::Windowed) {
        rectangle = {0, 0, options.width, options.height};
        if (!AdjustWindowRectEx(&rectangle, style, FALSE, 0)) ERROR_S(L"Cannot size the game window");
        const LONG width = std::min(rectangle.right - rectangle.left, info.rcWork.right - info.rcWork.left);
        const LONG height = std::min(rectangle.bottom - rectangle.top, info.rcWork.bottom - info.rcWork.top);
        LONG x = have_windowed_rectangle ? windowed_rectangle.left : info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
        LONG y = have_windowed_rectangle ? windowed_rectangle.top : info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
        x = std::clamp(x, info.rcWork.left, info.rcWork.right - width);
        y = std::clamp(y, info.rcWork.top, info.rcWork.bottom - height);
        rectangle = {x, y, x + width, y + height};
    } else if (target == Mode::Exclusive) {
        rectangle.right = rectangle.left + options.width;
        rectangle.bottom = rectangle.top + options.height;
    }
    mode = target;
    INITFLAG(g_Flags, GFLAG_FULLSCREEN, mode == Mode::Exclusive);
    if (!SetWindowPos(g_Wnd, HWND_NOTOPMOST, rectangle.left, rectangle.top,
                      rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
                      SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER))
        ERROR_S(L"Cannot position the game window");
}

void configure_parameters(D3DPRESENT_PARAMETERS &parameters) {
    parameters.Windowed = mode != Mode::Exclusive;
    parameters.BackBufferWidth = options.width;
    parameters.BackBufferHeight = options.height;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = g_Wnd;
}

HRESULT reset_device(const D3DPRESENT_PARAMETERS &parameters) {
    ChangeGuard guard;
    const HRESULT result = reset_copy(parameters, [](D3DPRESENT_PARAMETERS *attempt) { return g_D3DD->Reset(attempt); });
    // Also runs after a deferred reset succeeds through ordinary device recovery.
    if (result == D3D_OK && active) {
        apply_window(mode);
        confine_cursor();
    }
    return result;
}

bool handle_shortcut(UINT message, WPARAM key, LPARAM flags) {
    if (!active) return false;
    bool request = false;
    const bool consumed = shortcut.handle(message, key, flags, request);
    if (request && !in_change) {
        pending = !pending;
        requested.reset();
    }
    return consumed;
}

void focus_lost() { shortcut = {}; }

void confine_cursor() {
    if (!active || !FLAG(g_Flags, GFLAG_APPACTIVE) || mode == Mode::Windowed) {
        ClipCursor(nullptr);
        return;
    }
    RECT rect{};
    if (GetClientRect(g_Wnd, &rect)) {
        MapWindowPoints(g_Wnd, nullptr, reinterpret_cast<POINT *>(&rect), 2);
        ClipCursor(&rect);
    }
}

bool apply_pending(bool resources_ready, void (*release)(), void (*restore)()) {
    if (!active || !pending) return false;
    const HRESULT status = g_D3DD->TestCooperativeLevel();
    if (status != D3D_OK) return false; // Ordinary recovery runs before a queued change.
    pending = false;
    const Mode target = requested.value_or(toggle_target(mode, options.fullscreen));
    requested.reset();
    if (target == Mode::Exclusive && !supports_exclusive()) {
        lgr.error("Exclusive display mode {}x{} is unavailable; keeping the current mode")(options.width, options.height);
        return false;
    }
    const Mode previous_mode = mode;
    const auto previous_parameters = g_D3Dpp;
    ChangeGuard guard;
    g_FormCur->SystemEvent(SYSEV_INPUT_RESET);
    ClipCursor(nullptr);
    if (previous_mode != Mode::Exclusive && target != Mode::Exclusive) {
        // Present scales the unchanged back buffer to the new client area.
        // A windowed/borderless switch needs no resource release or device reset.
        apply_window(target);
        confine_cursor();
        sync_cursor();
        lgr.notice("Display mode: {}; rendering {}x{}")(name(mode), options.width, options.height);
        return true;
    }
    const HRESULT result = transition(resources_ready, release,
        [&] {
            apply_window(target);
            configure_parameters(g_D3Dpp);
            return reset_device(g_D3Dpp);
        },
        [&](HRESULT failure) {
            lgr.error("Display switch failed (HRESULT {}); restoring {}")(static_cast<unsigned long>(failure), name(previous_mode));
            apply_window(previous_mode);
            g_D3Dpp = previous_parameters;
            return reset_device(g_D3Dpp);
        }, restore);
    if (result != D3D_OK && result != D3DERR_DEVICELOST) throw CExceptionD3D(__FILE__, __LINE__, result);
    confine_cursor();
    if (result == D3D_OK) sync_cursor();
    lgr.notice("Display mode: {}; rendering {}x{}")(name(mode), options.width, options.height);
    return true;
}

void map_mouse(UINT message, int &x, int &y) {
    if (!active) return;
    POINT position{x, y};
    if (message == WM_MOUSEWHEEL) ScreenToClient(g_Wnd, &position);
    RECT rect{};
    if (!GetClientRect(g_Wnd, &rect)) return;
    x = map_coordinate(position.x, rect.right, options.width);
    y = map_coordinate(position.y, rect.bottom, options.height);
}
} // namespace Display
