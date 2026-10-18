// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <d3d9.h>
#include <vector>

class CForm;
namespace tests {
// Owns a hidden Win32 window. Only this executable's cursor imports are adapted;
// window style/size and production Display state transitions remain real.
class SettingsDisplayHost {
    struct Import { void **slot; void *previous; };
    std::vector<Import> imports_;
    HWND window_{};
    HWND previous_window_{};
    CForm *previous_form_{};
    D3DPRESENT_PARAMETERS previous_parameters_{};
    void cleanup() noexcept;
public:
    unsigned cursor_releases{};
    unsigned cursor_queries{};
    explicit SettingsDisplayHost(CForm &form);
    ~SettingsDisplayHost();
    SettingsDisplayHost(const SettingsDisplayHost &) = delete;
    SettingsDisplayHost &operator=(const SettingsDisplayHost &) = delete;
    HWND window() const { return window_; }
};
}
