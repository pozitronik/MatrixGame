// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "MatrixInstantDraw.hpp"
#include "Text/Font.hpp"
#include "WindowDisplay.hpp"
#include "SessionCleanup.hpp"

#include <memory>

// Access only the existing initialization reset; no alternate reset implementation.
struct GameStartupTestAccess {
    static void apply(SRobotsSettings &settings) { CGame::ApplyVideoParams(settings); }
};

namespace tests {
namespace {
struct ReleaseCom {
    template<class T> void operator()(T *value) const { if (value) value->Release(); }
};
struct HiddenWindow {
    HWND value = CreateWindowExW(0, L"STATIC", L"Startup reset test", WS_POPUP,
                                 0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~HiddenWindow() { if (value) DestroyWindow(value); }
};
struct SessionGuard {
    ~SessionGuard() {
        CFormMatrixGame *form = nullptr;
        bool timer = false;
        Session::cleanup_standalone(form, timer);
    }
};
}

// Explicit local native check, without game assets or a visible window. Not a CI test.
void native_startup_reset() {
    HiddenWindow window;
    MG_CHECK(window.value != nullptr);
    std::unique_ptr<IDirect3D9, ReleaseCom> api(Direct3DCreate9(D3D_SDK_VERSION));
    MG_CHECK(api != nullptr);
    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.hDeviceWindow = window.value;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.BackBufferWidth = 640;
    parameters.BackBufferHeight = 480;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    // Match the initial standalone device, before battle depth/stencil setup.
    parameters.EnableAutoDepthStencil = FALSE;
    IDirect3DDevice9 *created = nullptr;
    MG_CHECK(api->CreateDevice(0, D3DDEVTYPE_HAL, window.value, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                              &parameters, &created) == D3D_OK);
    std::unique_ptr<IDirect3DDevice9, ReleaseCom> device(created);
    SessionGuard cleanup;
    CBlockPar config;
    L3GInitAsDLL(GetModuleHandleW(nullptr), config, L"Reset test", L"Reset test", window.value,
                reinterpret_cast<uintptr_t>(api.get()), reinterpret_cast<uintptr_t>(device.get()));
    CacheInit();
    CForm::StaticInit();
    CInstDraw::StaticInit();
    g_Config.SetDefaults();
    g_MatrixHeap = HNew(nullptr) CHeap;
    g_MatrixData = HNew(g_MatrixHeap) CBlockPar;
    g_MatrixMap = HNew(g_MatrixHeap) CMatrixMapLogic;
    Display::initialize({640, 480, Display::Mode::Windowed, Display::Mode::Borderless});
    g_D3Dpp = parameters;
    g_ScreenX = 640;
    g_ScreenY = 480;

    auto draw = [&] {
        S3D_Default();
        CInstDraw::DrawFrameBegin();
        MG_CHECK(device->BeginScene() == D3D_OK);
        // Exercise the same default-pool buffers as a rendered menu hint.
        SVert_V4_UV vertices[4]{};
        vertices[0].p = {0, 16, 0, 1};
        vertices[1].p = {0, 0, 0, 1};
        vertices[2].p = {16, 16, 0, 1};
        vertices[3].p = {16, 0, 0, 1};
        CInstDraw::BeginDraw(IDFVF_V4_UV);
        CInstDraw::AddVerts(vertices, nullptr);
        CInstDraw::ActualDraw();
        auto &font = Text::GetFont(device.get(), L"Font.2Normal");
        RECT rectangle{0, 0, 640, 480};
        MG_CHECK(font->DrawTextW(nullptr, L"Start battle", -1, &rectangle, DT_NOCLIP, 0xffffffff) > 0);
        MG_CHECK(device->EndScene() == D3D_OK);
    };
    auto settings = SRobotsSettings::generate_default_settings();
    settings.m_ResolutionX = 640;
    settings.m_ResolutionY = 480;
    settings.m_FSAASamples = 0;
    draw();
    for (int cycle = 0; cycle < 3; ++cycle) {
        try { GameStartupTestAccess::apply(settings); }
        catch (const Base::CException &error) { throw std::runtime_error(utils::from_wstring(error.Info())); }
        MG_CHECK(device->TestCooperativeLevel() == D3D_OK);
        draw();
    }
    MG_CHECK(!IsWindowVisible(window.value));
    Text::OnLostDevice();
}
} // namespace tests
