// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <windows.h>
#include <d3d9.h>
#include <cstdint>

namespace Base { class CBlockPar; }

namespace Display {

enum class Mode { Windowed, Borderless, Exclusive };
struct Settings {
    int width = 1024;
    int height = 768;
    Mode mode = Mode::Windowed;
    Mode fullscreen = Mode::Borderless;
};
struct Selection {
    Settings settings;
    bool valid = true;
};
Selection resolve(const Base::CBlockPar &options);
Selection load(const wchar_t *path = L"CFG\\standalone.txt");
const char *name(Mode mode);
Mode toggle_target(Mode current, Mode fullscreen);
int map_coordinate(int position, int client_extent, int render_extent);

struct Shortcut {
    bool held = false;
    // Consume the complete Enter press, even if Alt is released before Enter.
    bool handle(UINT message, WPARAM key, LPARAM flags, bool &request);
};

template<class Reset>
HRESULT reset_copy(const D3DPRESENT_PARAMETERS &parameters, Reset reset) {
    auto attempt = parameters;
    return reset(&attempt); // DirectX mutates its input, including dimensions on failure.
}

// Resources must stay released while a lost device waits for ordinary recovery.
template<class Release, class Change, class Rollback, class Restore>
HRESULT transition(bool ready, Release release, Change change, Rollback rollback, Restore restore) {
    if (ready) release();
    HRESULT result = change();
    if (result != D3D_OK && result != D3DERR_DEVICELOST) result = rollback(result);
    if (result == D3D_OK) restore();
    return result;
}

} // namespace Display
