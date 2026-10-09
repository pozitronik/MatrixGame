// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "Helper.hpp"
#include "Form.hpp"
#include "GraphicsReferences.hpp"
#include "MatrixFormGame.hpp"
#include "SessionCleanup.hpp"

#include <memory>
#include <type_traits>
#include <vector>

namespace {
struct Services {
    Services() {
        CacheInit();
        g_Config.SetDefaults();
    }

    ~Services() {
        CGame::Deinit();
#ifdef _DEBUG
        CHelper::ClearAll();
#endif
        CacheDeinit();
    }
};

void populate_cursors(CMatrixConfig &config) {
    constexpr int count = 2;
    auto *pairs = static_cast<SStringPair *>(HAlloc(sizeof(SStringPair) * count, g_CacheHeap));
    int constructed = 0;
    try {
        for (; constructed < count; ++constructed) {
            std::construct_at(pairs + constructed, std::wstring(80, L'k'), std::wstring(160, L'v'));
        }
    }
    catch (...) {
        std::destroy_n(pairs, constructed);
        HFree(pairs, g_CacheHeap);
        throw;
    }
    config.m_Cursors = pairs;
    config.m_CursorsCnt = count;
}

void configuration_cleanup() {
    Services services;
    CMatrixConfig config;
    config.SetDefaults();
    for (int cycle = 0; cycle < 3; ++cycle) {
        populate_cursors(config);
        config.Clear();
        MG_CHECK(config.m_Cursors == nullptr);
        MG_CHECK(config.m_CursorsCnt == 0);
        config.Clear();
        MG_CHECK(config.m_Cursors == nullptr);
        MG_CHECK(config.m_CursorsCnt == 0);
    }
}

void empty_configuration_cleanup() {
    Services services;
    CMatrixConfig config;
    config.m_CursorsCnt = 17;
    config.SetDefaults();
    MG_CHECK(config.m_Cursors == nullptr);
    MG_CHECK(config.m_CursorsCnt == 0);
    config.Clear();
    config.Clear();
    MG_CHECK(config.m_CursorsCnt == 0);
}

struct RecordingForm : CForm {
    std::vector<int> &events;
    int id;

    RecordingForm(std::vector<int> &record, int identifier, bool fail = false) : events(record), id(identifier) {
        if (fail) {
            throw std::runtime_error("Synthetic form construction failure");
        }
    }

    ~RecordingForm() {
        if (g_FormCur == this) {
            FormChange(nullptr);
        }
    }

    void Enter() override { events.push_back(id * 10 + 1); }
    void Leave() override { events.push_back(id * 10 + 2); }
    void Draw() override { throw std::logic_error("Unexpected rendering in lifecycle fixture"); }
    void Takt(int) override {}
    void MouseMove(int, int) override {}
    void MouseKey(ButtonStatus, int, int, int) override {}
    void Keyboard(bool, uint8_t) override {}
    void SystemEvent(ESysEvent) override {}
};

struct FormSession {
    FormSession() { CForm::StaticInit(); }
    ~FormSession() { FormChange(nullptr); }
};

void form_transitions() {
    FormSession session;
    std::vector<int> events;
    {
        RecordingForm first(events, 1);
        RecordingForm second(events, 2);
        MG_CHECK(g_FormFirst == &first);
        MG_CHECK(g_FormLast == &second);
        FormChange(&first);
        FormChange(&second);
        FormChange(nullptr);
        MG_CHECK(g_FormCur == nullptr);
        MG_CHECK((events == std::vector<int>{11, 12, 21, 22}));
    }
    MG_CHECK(g_FormFirst == nullptr);
    MG_CHECK(g_FormLast == nullptr);
}

void form_construction_failure() {
    FormSession session;
    std::vector<int> events;
    RecordingForm survivor(events, 1);
    bool caught = false;
    try {
        RecordingForm failed(events, 2, true);
    }
    catch (const std::runtime_error &) {
        caught = true;
    }
    MG_CHECK(caught);
    MG_CHECK(g_FormFirst == &survivor);
    MG_CHECK(g_FormLast == &survivor);
    MG_CHECK(g_FormCur == nullptr);
    MG_CHECK(events.empty());
}

void repeated_form_sessions() {
    FormSession session;
    std::vector<int> events;
    for (int cycle = 0; cycle < 3; ++cycle) {
        {
            RecordingForm form(events, cycle + 1);
            FormChange(&form);
            MG_CHECK(g_FormCur == &form);
            FormChange(nullptr);
        }
        MG_CHECK(g_FormCur == nullptr);
        MG_CHECK(g_FormFirst == nullptr);
        MG_CHECK(g_FormLast == nullptr);
    }
    MG_CHECK((events == std::vector<int>{11, 12, 21, 22, 31, 32}));
}

void check_empty_game() {
    MG_CHECK(g_MatrixHeap == nullptr);
    MG_CHECK(g_MatrixData == nullptr);
    MG_CHECK(g_MatrixMap == nullptr);
    MG_CHECK(g_Render == nullptr);
    MG_CHECK(g_IFaceList == nullptr);
    MG_CHECK(g_ConfigHistory == nullptr);
    MG_CHECK(g_Config.m_Cursors == nullptr);
    MG_CHECK(g_Config.m_CursorsCnt == 0);
    MG_CHECK(g_D3DD == nullptr);
}

void partial_startup_cleanup() {
    for (int stage = 0; stage < 5; ++stage) {
        Services services;
        if (stage >= 1) {
            g_MatrixHeap = HNew(nullptr) CHeap;
        }
        if (stage >= 2) {
            g_MatrixData = HNew(g_MatrixHeap) CBlockPar;
            g_MatrixData->ParAdd(L"Synthetic", L"Partial startup");
        }
        if (stage >= 3) {
            populate_cursors(g_Config);
        }
        if (stage >= 4) {
            g_MatrixMap = HNew(g_MatrixHeap) CMatrixMapLogic;
        }
        CGame::Deinit();
        check_empty_game();
        CGame::Deinit();
        check_empty_game();
        CacheDeinit();
        CacheDeinit();
        MG_CHECK(g_Cache == nullptr);
        MG_CHECK(g_CacheHeap == nullptr);
    }
}

void safe_free_without_cache() {
    CacheDeinit();
    g_MatrixHeap = HNew(nullptr) CHeap;
    g_MatrixData = HNew(g_MatrixHeap) CBlockPar;
    g_MatrixData->ParAdd(L"Synthetic", L"Failure before cache creation");
    CGame::SafeFree();
    check_empty_game();
    MG_CHECK(g_Cache == nullptr && g_CacheHeap == nullptr);
    CGame::SafeFree();
    check_empty_game();
}

struct FakeReference {
    std::vector<int> &events;
    int id;
    void Release() { events.push_back(id); }
};

void graphics_reference_ownership() {
    std::vector<int> events;
    FakeReference api{events, 1}, device{events, 2}, host{events, 3};
    Graphics::Reference<FakeReference> owned_interface, owned_device;
    auto *visible_interface = &api;
    auto *visible_device = &device;
    owned_interface.adopt(visible_interface);
    owned_device.adopt(visible_device);
    owned_device.release(visible_device);
    owned_interface.release(visible_interface);
    MG_CHECK(visible_interface == nullptr && visible_device == nullptr);
    MG_CHECK((events == std::vector<int>{2, 1}));
    owned_device.release(visible_device);
    owned_interface.release(visible_interface);
    MG_CHECK(events.size() == 2);
    visible_device = &host;
    owned_device.release(visible_device);
    MG_CHECK(visible_device == nullptr && events.size() == 2);
    visible_interface = &api;
    owned_interface.adopt(visible_interface);
    owned_interface.release(visible_interface);
    MG_CHECK((events == std::vector<int>{2, 1, 1}));
}

void cleanup_continues_after_failure() {
    std::vector<int> events;
    const bool result = Session::cleanup(
        [&] { events.push_back(1); throw std::runtime_error("Synthetic cleanup failure"); },
        [&] { events.push_back(2); },
        [&] { events.push_back(3); throw 17; },
        [&] { events.push_back(4); });
    MG_CHECK(!result);
    MG_CHECK((events == std::vector<int>{1, 2, 3, 4}));
    MG_CHECK(Session::cleanup([&] { events.push_back(5); }));
}

void standalone_partial_cleanup() {
    for (int stage = 0; stage < 5; ++stage) {
        Base::CMain::BaseInit();
        SetUnhandledExceptionFilter(nullptr);
        CForm::StaticInit();
        CacheInit();
        g_Config.SetDefaults();
        CFormMatrixGame *form = nullptr;
        bool timer_active = false;
        if (stage >= 1) g_MatrixHeap = HNew(nullptr) CHeap;
        if (stage >= 2) g_MatrixData = HNew(g_MatrixHeap) CBlockPar;
        if (stage >= 3) populate_cursors(g_Config);
        if (stage >= 4) {
            form = HNew(nullptr) CFormMatrixGame;
            // Initialization can fail after registration but before Enter completes.
            g_FormCur = form;
        }
        SETFLAG(g_Flags, GFLAG_GAMMA);
        MG_CHECK(Session::cleanup_standalone(form, timer_active));
        MG_CHECK(!FLAG(g_Flags, GFLAG_GAMMA));
        MG_CHECK(form == nullptr && !timer_active);
        MG_CHECK(g_FormFirst == nullptr && g_FormLast == nullptr && g_FormCur == nullptr);
        MG_CHECK(g_Cache == nullptr && g_CacheHeap == nullptr && g_Wnd == nullptr);
        check_empty_game();
        MG_CHECK(Session::cleanup_standalone(form, timer_active));
    }
}

void graphics_partial_initialization() {
    CBlockPar config;
    config.ParAdd(L"FullScreen", L"0");
    config.ParAdd(L"Resolution", L"Invalid synthetic resolution");
    bool caught = false;
    try {
        L3GInitAsEXE(GetModuleHandle(nullptr), config, L"MatrixLifecycleTest", L"Lifecycle test");
    }
    catch (const CException &) { caught = true; }
    MG_CHECK(caught);
    L3GDeinit();
    L3GDeinit();
    MG_CHECK(g_D3D == nullptr && g_D3DD == nullptr && g_Wnd == nullptr);
}

struct ReleaseCom {
    template<class T> void operator()(T *pointer) const { if (pointer) pointer->Release(); }
};

struct GraphicsSession {
    ~GraphicsSession() { L3GDeinit(); }
};

void native_graphics_lifetimes() {
    CBlockPar config;
    config.ParAdd(L"FullScreen", L"0");
    config.ParAdd(L"Resolution", L"320,240");
    for (int cycle = 0; cycle < 3; ++cycle) {
        GraphicsSession session;
        L3GInitAsEXE(GetModuleHandle(nullptr), config, L"MatrixLifecycleTest", L"Lifecycle test");
        MG_CHECK(g_D3D != nullptr && g_D3DD != nullptr && IsWindow(g_Wnd));
        const HWND window = g_Wnd;
        // Keep an extra reference alive so an omitted engine Release is observable.
        g_D3D->AddRef();
        std::unique_ptr<IDirect3D9, ReleaseCom> api(g_D3D);
        g_D3DD->AddRef();
        std::unique_ptr<IDirect3DDevice9, ReleaseCom> device(g_D3DD);
        L3GDeinit();
        MG_CHECK(g_D3D == nullptr && g_D3DD == nullptr && g_Wnd == nullptr);
        MG_CHECK(!IsWindow(window));
        L3GDeinit();
        MG_CHECK(device.release()->Release() == 0);
        MG_CHECK(api.release()->Release() == 0);
    }

    // The DLL layer may use these resources but must leave the host references intact.
    const HWND host_window = CreateWindowEx(0, "STATIC", "Lifecycle host", WS_OVERLAPPED,
        0, 0, 320, 240, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    MG_CHECK(host_window != nullptr);
    const auto destroy_window = [](HWND window) { DestroyWindow(window); };
    std::unique_ptr<std::remove_pointer_t<HWND>, decltype(destroy_window)> window(host_window, destroy_window);
    std::unique_ptr<IDirect3D9, ReleaseCom> api(Direct3DCreate9(D3D_SDK_VERSION));
    MG_CHECK(api != nullptr);
    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = host_window;
    IDirect3DDevice9 *created = nullptr;
    const HRESULT result = api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, host_window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &parameters, &created);
    std::unique_ptr<IDirect3DDevice9, ReleaseCom> device(created);
    MG_CHECK(result == D3D_OK && device != nullptr);
    const ULONG api_references = api->AddRef();
    api->Release();
    const ULONG device_references = device->AddRef();
    device->Release();
    const LONG_PTR original_procedure = GetWindowLongPtr(host_window, GWL_WNDPROC);
    {
        GraphicsSession session;
        L3GInitAsDLL(GetModuleHandle(nullptr), config, L"MatrixLifecycleTest", L"Lifecycle host",
            host_window, reinterpret_cast<uintptr_t>(api.get()), reinterpret_cast<uintptr_t>(device.get()));
        L3GDeinit();
        MG_CHECK(IsWindow(host_window));
        MG_CHECK(GetWindowLongPtr(host_window, GWL_WNDPROC) == original_procedure);
        MG_CHECK(g_D3D == nullptr && g_D3DD == nullptr && g_Wnd == nullptr);
        L3GDeinit();
        MG_CHECK(api->AddRef() == api_references);
        api->Release();
        MG_CHECK(device->AddRef() == device_references);
        device->Release();
    }
    MG_CHECK(device.release()->Release() == 0);
    MG_CHECK(api.release()->Release() == 0);
}

constexpr tests::Case cases[] = {
    {"game.lifecycle.configuration_cleanup", configuration_cleanup},
    {"game.lifecycle.empty_configuration", empty_configuration_cleanup},
    {"game.lifecycle.form_transitions", form_transitions},
    {"game.lifecycle.form_construction_failure", form_construction_failure},
    {"game.lifecycle.repeated_forms", repeated_form_sessions},
    {"game.lifecycle.partial_startup", partial_startup_cleanup},
    {"game.lifecycle.safe_free_without_cache", safe_free_without_cache},
    {"game.lifecycle.graphics_references", graphics_reference_ownership},
    {"game.lifecycle.cleanup_failure", cleanup_continues_after_failure},
    {"game.lifecycle.standalone_partial_cleanup", standalone_partial_cleanup},
    {"game.lifecycle.graphics_partial_init", graphics_partial_initialization},
    {"manual.lifecycle.native_graphics", native_graphics_lifetimes},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Lifecycle test leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
