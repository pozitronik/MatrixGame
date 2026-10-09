// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "DeviceRecovery.hpp"
#include "Text/Font.hpp"

#include <array>
#include <iomanip>
#include <memory>

namespace tests {
namespace {
template<class T> struct Release {
    void operator()(T *object) const { if (object) object->Release(); }
};

struct HiddenWindow {
    HWND handle = CreateWindowExW(0, L"STATIC", L"MatrixGame font reset check", WS_POPUP,
                                 0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~HiddenWindow() { if (handle) DestroyWindow(handle); }
};

struct FontLossGuard {
    bool armed = true;
    ~FontLossGuard() {
        if (!armed) return;
        try { Text::OnLostDevice(); }
        catch (...) {}
    }
};

void check_result(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        throw std::runtime_error(std::format("{} failed (HRESULT=0x{:08X})", operation,
                                            static_cast<uint32_t>(result)));
    }
}
}

// Explicit local check: requires a native DirectX device; never registered with CTest.
void native_font_reset() {
    HiddenWindow window;
    MG_CHECK(window.handle != nullptr);
    std::unique_ptr<IDirect3D9, Release<IDirect3D9>> d3d{Direct3DCreate9(D3D_SDK_VERSION)};
    MG_CHECK(d3d != nullptr);
    D3DADAPTER_IDENTIFIER9 adapter{};
    check_result(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &adapter), "GetAdapterIdentifier");
    std::cout << "Adapter: " << adapter.Description << ", driver: " << adapter.Driver
              << ", version: " << HIWORD(adapter.DriverVersion.HighPart) << '.'
              << LOWORD(adapter.DriverVersion.HighPart) << '.' << HIWORD(adapter.DriverVersion.LowPart)
              << '.' << LOWORD(adapter.DriverVersion.LowPart) << '\n';

    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window.handle;
    parameters.BackBufferWidth = parameters.BackBufferHeight = 128;
    parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9 *raw = nullptr;
    check_result(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window.handle,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &raw), "CreateDevice");
    std::unique_ptr<IDirect3DDevice9, Release<IDirect3DDevice9>> device{raw};
    FontLossGuard fontCleanup;
    constexpr std::array names{L"Font.1Normal", L"Font.2Mini", L"Font.2Small", L"Font.2Normal", L"Font.2Ranger"};
    auto draw = [&] {
        check_result(device->BeginScene(), "BeginScene");
        for (auto name : names) {
            auto &font = Text::GetFont(device.get(), name);
            MG_CHECK(static_cast<bool>(font));
            RECT rectangle{0, 0, 128, 128};
            MG_CHECK(font->DrawTextW(nullptr, L"Reset contract", -1, &rectangle, DT_NOCLIP, 0xffffffff) > 0);
        }
        check_result(device->EndScene(), "EndScene");
    };
    draw();
    for (int cycle = 1; cycle <= 3; ++cycle) {
        // Request a reset on an operational windowed device, without changing desktop modes.
        MG_CHECK(!Rendering::RecoverDevice(D3DERR_DEVICENOTRESET, true,
            [] { Text::OnLostDevice(); },
            [&] { auto resetParameters = parameters; return device->Reset(&resetParameters); },
            [] { Text::OnResetDevice(); },
            [](std::chrono::milliseconds) { throw std::runtime_error("Native reset did not complete"); }));
        draw();
        std::cout << "Reset and draw cycle " << cycle << " passed\n";
    }
    Text::OnLostDevice();
    fontCleanup.armed = false;
}

}  // namespace tests
