// MatrixGame - licensed under GPLv2 or any later version.
#include "native_session_assets.hpp"
#include "test_support.hpp"
#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFormGame.hpp"
#include "input.hpp"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <ctime>

namespace fs = std::filesystem;

// Contain generated cache/screenshot paths in the synthetic fixture directory.
// Session initialization, rendering and teardown use their production code.
std::wstring PathToOutputFiles(const wchar_t *destination) {
    const auto path = fs::current_path() / destination;
    fs::create_directories(path);
    return path.wstring();
}

namespace {
struct Window {
    HWND parent = CreateWindowExW(0, L"STATIC", L"Synthetic DLL host", WS_POPUP,
        0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND child = CreateWindowExW(0, L"STATIC", L"Synthetic battle", WS_CHILD,
        0, 0, 640, 480, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() { if (child) DestroyWindow(child); if (parent) DestroyWindow(parent); }
};
struct NativeDevice {
    IDirect3D9 *api = Direct3DCreate9(D3D_SDK_VERSION);
    IDirect3DDevice9 *device{};
    explicit NativeDevice(HWND window) {
        MG_CHECK(api);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.hDeviceWindow = window; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferWidth = 640; pp.BackBufferHeight = 480; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        const auto result = api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &pp, &device);
        if (result != D3D_OK) {
            api->Release(); api = nullptr;
            throw std::runtime_error("Native Direct3D 9 device unavailable: " + std::to_string(result));
        }
    }
    ~NativeDevice() { if (device) device->Release(); if (api) api->Release(); }
};
struct Fixture {
    fs::path previous = fs::current_path();
    fs::path root = previous / ("native-session-" + std::to_string(GetCurrentProcessId()));
    Fixture() { MG_CHECK(!fs::exists(root)); fs::create_directories(root); fs::current_path(root); }
    ~Fixture() { fs::current_path(previous); std::error_code error; fs::remove_all(root, error); }
};
struct HostObservation {
    HWND window{};
    int progress{}, begins{}, ticks{}, texts{}, text_clears{};
    int phase{};
    float music{1};
    CMatrixRobotAI *robot{};
    float initial_x{};
    bool moved{}, map_observed{}, leave_key_held{};
    std::time_t entry_time{}, first_progress_time{};
    std::string failure;
} *host;
void __stdcall sound(wchar *) {}
dword __stdcall sound_create(wchar *, int, int) { return 0; }
void __stdcall sound_id(dword) {}
int __stdcall sound_playing(dword) { return 0; }
void __stdcall sound_set(dword, float) {}
float __stdcall sound_get(dword) { return 0; }
void __stdcall host_text(wchar *, wchar *, DWORD, int, int, int, int, int, int, int, Base::CRect *, SMGDRangersInterfaceText *text) {
    ++host->texts;
    text->m_SizeX = text->m_SizeY = 2; text->m_Pitch = 8;
    text->m_Buf = new BYTE[16]{};
}
void __stdcall host_text_clear(SMGDRangersInterfaceText *text) {
    ++host->text_clears; delete[] text->m_Buf; text->m_Buf = nullptr;
}
void __stdcall progress(float) {
    // BaseInit installs interactive crash reporters. This test host reports a
    // process failure to its runner instead of opening a diagnostic window.
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    if (host->progress == 0) host->first_progress_time = std::time(nullptr);
    ++host->progress;
    if (g_MatrixMap && g_MatrixMap->m_Size.x == 32) host->map_observed = true;
}
void __stdcall begin() { ++host->begins; }
float __stdcall music_get() { return host->music; }
void __stdcall music_set(float value) { host->music = value; }
void __stdcall tick() {
    ++host->ticks;
    try {
        MG_CHECK(host->ticks < 5000);
        MG_CHECK(g_FormCur && g_MatrixMap && !IsWindowVisible(host->window));
        if (host->phase == 0) {
            MG_CHECK(random::mode() == random::Mode::ParkMiller);
            MG_CHECK(g_MatrixMap->m_DialogModeName && wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_BEGIN) == 0);
            auto *form = static_cast<CFormMatrixGame *>(g_FormCur);
            form->Keyboard(true, VK_RETURN); form->Keyboard(false, VK_RETURN);
            MG_CHECK(!g_MatrixMap->m_DialogModeName && !Input::isVKeyPressed(VK_RETURN));
            int robots = 0;
            for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
                if (object->IsLiveRobot()) {
                    ++robots;
                    if (object->GetSide() == PLAYER_SIDE) host->robot = object->AsRobot();
                }
            }
            MG_CHECK(robots == 2 && host->robot);
            MG_CHECK(host->robot->m_PosX == 100 && host->robot->m_PosY == 100);
            auto *side = g_MatrixMap->GetPlayerSide();
            MG_CHECK(side->GetStatValue(STAT_ROBOT_KILL) == 0);
            side->CreateGroupFromCurrent(host->robot);
            side->m_CurrSel = ROBOT_SELECTED;
            host->initial_x = host->robot->m_PosX;
            g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
            g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(300, 100, 10);
            side->OnRButtonDown(CPoint(0, 0));
            MG_CHECK(side->m_PlayerGroup[host->robot->GetGroupLogic()].Order() == mpo_MoveTo);
            ++host->phase;
        } else if (host->phase == 1 && host->robot->m_PosX > host->initial_x + 5) {
            host->moved = true;
            if (host->leave_key_held) {
                static_cast<CFormMatrixGame *>(g_FormCur)->Keyboard(true, VK_F12);
                MG_CHECK(Input::isVKeyPressed(VK_F12));
            }
            MG_CHECK(PostMessageW(host->window, WM_CLOSE, 0, 0));
            ++host->phase;
        }
    } catch (const std::exception &error) { host->failure = error.what(); throw; }
}
void native_run_lifetimes_impl(bool leave_key_held) {
    Fixture fixture;
    tests::write_native_session_assets();
    Window window;
    MG_CHECK(window.parent && window.child && !IsWindowVisible(window.child));
    const auto original_procedure = GetWindowLongPtrA(window.child, GWLP_WNDPROC);
    NativeDevice native(window.child);
    const auto references = native.device->AddRef(); native.device->Release();
    auto settings = SRobotsSettings::generate_default_settings();
    settings.FDirect3D = reinterpret_cast<uintptr_t>(native.api);
    settings.FD3DDevice = reinterpret_cast<uintptr_t>(native.device);
    settings.m_ResolutionX = 640; settings.m_ResolutionY = 480;
    settings.m_FSAASamples = 0; settings.m_VSync = false; settings.m_RefreshRate = 0;
    settings.m_SoftwareCursor = true; settings.m_RobotShadow = 0;
    settings.m_ShowStencilShadows = settings.m_ShowProjShadows = false;
    SMGDRangersInterface callbacks{sound, sound_create, sound_id, sound_id, sound_playing,
        sound_set, sound_set, sound_get, sound_get, host_text, host_text_clear, progress, tick, begin, music_get, music_set};
    auto *entry = GetRobotInterface();
    entry->m_Init(&callbacks);
    struct HostGuard {
        SMGDRobotInterface *entry;
        ~HostGuard() { entry->m_Deinit(); host = nullptr; }
    } guard{entry};
    for (int lifetime = 0; lifetime < 3; ++lifetime) {
        const bool failing = lifetime == 1;
        HostObservation observed;
        observed.window = window.child;
        observed.leave_key_held = leave_key_held;
        host = &observed;
        if (failing) fs::rename(L"CFG/robots/iface.txt", L"CFG/robots/iface.saved");
        SRobotGameState result{41,42,43,44,45,46};
        const auto unchanged = result;
        wchar_t map[] = L".\\synthetic.map";
        observed.entry_time = std::time(nullptr);
        const int exit = entry->m_Run(GetModuleHandleW(nullptr), window.child, map, &settings,
            nullptr, nullptr, nullptr, nullptr, nullptr, &result);
        SetUnhandledExceptionFilter(nullptr);
        std::set_terminate([] { std::abort(); });
        if (failing) fs::rename(L"CFG/robots/iface.saved", L"CFG/robots/iface.txt");
        std::cout << "fixture=synthetic-session-v1 lifetime=" << lifetime << " result=" << exit
            << " seed_interval=[" << observed.entry_time << ',' << observed.first_progress_time
            << "] ticks=" << observed.ticks << " moved=" << observed.moved << '\n';
        MG_CHECK(observed.failure.empty());
        MG_CHECK(exit == (failing ? 100 : 0));
        MG_CHECK(observed.progress > 0 && observed.map_observed);
        MG_CHECK(observed.begins == (failing ? 0 : 1));
        if (failing) {
            MG_CHECK(std::memcmp(&result, &unchanged, sizeof(result)) == 0);
            std::ifstream errors("matrixgame-dll-errors.log", std::ios::binary);
            const std::string diagnostic((std::istreambuf_iterator<char>(errors)), {});
            MG_CHECK(diagnostic.find("Error open file: cfg\\robots\\iface.txt") != std::string::npos);
        }
        else {
            MG_CHECK(observed.ticks >= 2 && observed.moved && observed.phase == 2);
            MG_CHECK(result.m_Time >= 0 && result.m_BuildRobot == 0 && result.m_KillRobot == 0 &&
                result.m_BuildTurret == 0 && result.m_KillTurret == 0 && result.m_KillBuilding == 0);
            MG_CHECK(observed.texts > 0 && observed.texts == observed.text_clears);
        }
        MG_CHECK(!g_MatrixHeap && !g_MatrixData && !g_MatrixMap && !g_Render && !g_IFaceList);
        MG_CHECK(!g_Cache && !g_CacheHeap && !g_D3DD && !g_D3D && !g_Wnd);
        MG_CHECK(!g_FormCur && !g_FormFirst && !g_FormLast && !CMatrixMapStatic::GetFirstLogic());
        MG_CHECK(IsWindow(window.child) && !IsWindowVisible(window.child));
        MG_CHECK(GetWindowLongPtrA(window.child, GWLP_WNDPROC) == original_procedure);
        MG_CHECK(native.device->TestCooperativeLevel() == D3D_OK);
        const auto remaining = native.device->AddRef(); native.device->Release();
        MG_CHECK(remaining == references);
        MG_CHECK(native.device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0) == D3D_OK);
        MG_CHECK(native.device->Present(nullptr, nullptr, nullptr, nullptr) == D3D_OK);
        if (leave_key_held) MG_CHECK(!Input::isVKeyPressed(VK_F12));
#ifdef MEM_SPY_ENABLE
        MG_CHECK(Base::SMemHeader::first_mem_block == nullptr);
#endif
    }
}
void native_run_lifetimes() { native_run_lifetimes_impl(false); }
void native_held_input_probe() { native_run_lifetimes_impl(true); }
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const tests::Case cases[] = {
        {"game.session.native_run_lifetimes", native_run_lifetimes},
        {"probe.session.held_input", native_held_input_probe}};
    const int result = tests::run(argc, argv, cases);
    Base::CMain::BaseDeInit();
    return result;
}
