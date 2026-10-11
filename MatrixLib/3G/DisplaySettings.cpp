// MatrixGame - licensed under GPLv2 or any later version.
#include "DisplaySettings.hpp"
#include "CBlockPar.hpp"
#include "utils.hpp"

#include <filesystem>
#include <algorithm>
#include <string>

namespace Display {
namespace {
bool read(const Base::CBlockPar &options, const wchar_t *key, std::wstring &value) {
    if (options.ParCount(key) != 1) return false;
    value = utils::trim(options.ParGet(key));
    utils::to_lower(value);
    return true;
}

bool dimension(const std::wstring &value, int minimum, int &result) {
    if (value.empty() || value.size() > 4) return false;
    int number = 0;
    for (wchar_t ch : value) {
        if (ch < L'0' || ch > L'9') return false;
        number = number * 10 + (ch - L'0');
    }
    if (number < minimum || number > 8192) return false;
    result = number;
    return true;
}

bool mode_value(const std::wstring &value, Mode &mode) {
    if (value == L"windowed") mode = Mode::Windowed;
    else if (value == L"borderless") mode = Mode::Borderless;
    else if (value == L"exclusive") mode = Mode::Exclusive;
    else return false;
    return true;
}
}

Selection resolve(const Base::CBlockPar &options) {
    Settings settings;
    std::wstring value;
    if (options.ParCount(L"ResolutionWidth") &&
        (!read(options, L"ResolutionWidth", value) || !dimension(value, 640, settings.width))) return {{}, false};
    if (options.ParCount(L"ResolutionHeight") &&
        (!read(options, L"ResolutionHeight", value) || !dimension(value, 480, settings.height))) return {{}, false};
    if (options.ParCount(L"WindowMode") &&
        (!read(options, L"WindowMode", value) || !mode_value(value, settings.mode))) return {{}, false};
    if (options.ParCount(L"FullscreenMode") &&
        (!read(options, L"FullscreenMode", value) || !mode_value(value, settings.fullscreen) ||
         settings.fullscreen == Mode::Windowed)) return {{}, false};
    // Starting fullscreen selects the same fullscreen mode for subsequent toggles.
    if (settings.mode != Mode::Windowed) settings.fullscreen = settings.mode;
    return {settings, true};
}

Selection load(const wchar_t *path) {
    try {
        Base::CBlockPar options;
        if (std::filesystem::exists(path)) options.LoadFromTextFile(path);
        return resolve(options);
    } catch (const Base::CException &) {
    } catch (const std::exception &) {
    }
    return {{}, false};
}

const char *name(Mode mode) {
    switch (mode) {
        case Mode::Windowed: return "Windowed";
        case Mode::Borderless: return "Borderless";
        case Mode::Exclusive: return "Exclusive";
    }
    return "Unknown";
}

Mode toggle_target(Mode current, Mode fullscreen) {
    return current == Mode::Windowed ? fullscreen : Mode::Windowed;
}

int map_coordinate(int position, int client_extent, int render_extent) {
    if (client_extent <= 1 || render_extent <= 1) return 0;
    const int result = static_cast<int>(static_cast<int64_t>(position) * (render_extent - 1) / (client_extent - 1));
    if (position < 0) return std::min(-1, result);
    if (position >= client_extent) return std::max(render_extent, result);
    return result;
}

bool Shortcut::handle(UINT message, WPARAM key, LPARAM flags, bool &request) {
    request = false;
    if (key != VK_RETURN) return false;
    if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        const bool consumed = held;
        held = false;
        return consumed;
    }
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) return false;
    const bool alt = (static_cast<uintptr_t>(flags) & (1u << 29)) != 0;
    const bool repeat = (static_cast<uintptr_t>(flags) & (1u << 30)) != 0;
    if (!held && !alt) return false;
    request = !held && !repeat;
    held = true;
    return true;
}
} // namespace Display
