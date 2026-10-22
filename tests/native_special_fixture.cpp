// MatrixGame - licensed under GPLv2 or any later version.
#include "native_session_assets.hpp"
#include "native_special_assets.hpp"
#include "native_packed_config.hpp"
#include "test_support.hpp"
#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObject.hpp"
#include "MatrixFormGame.hpp"
#include "Interface/CInterface.h"
#include "CStorage.hpp"
#include "Pack.hpp"
#include "StandaloneSettings.hpp"
#include "input.hpp"
#include "native_window_containment.hpp"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <array>
#include <shellapi.h>

namespace fs = std::filesystem;
std::wstring PathToOutputFiles(const wchar_t *destination) {
    const auto path = fs::current_path() / destination;
    fs::create_directories(path);
    return path.wstring();
}
namespace {
struct Directory {
    fs::path previous = fs::current_path(), root;
    bool closed = false;
    explicit Directory(const fs::path &path) : root(fs::absolute(path).lexically_normal()) {
        MG_CHECK(fs::is_directory(root.parent_path()));
        MG_CHECK(!fs::exists(root) && fs::create_directory(root));
        try { fs::current_path(root); }
        catch (...) { std::error_code error; fs::remove_all(root, error); throw; }
    }
    void close(bool keep = false) {
        fs::current_path(previous);
        if (!keep) fs::remove_all(root);
        closed = true;
    }
    ~Directory() {
        if (closed) return;
        std::error_code error;
        fs::current_path(previous, error);
        if (!error) fs::remove_all(root, error);
        if (error) std::cerr << "Cannot clean synthetic export: " << error.message() << '\n';
    }
};
void assets() {
    tests::write_native_session_assets();
    tests::write_native_special_scenes();
    CBlockPar data;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    data.BlockGet(L"Config")->ParSetAdd(L"Map", L"Matrix/Map/mixed-break-anim.cmap");
    data.SaveInTextFile(L"CFG/robots/data.txt");
    tests::pack_native_session_configuration();
    fs::rename(L"CFG/robots.dat", L"CFG/robots.fixture-packed");
    fs::rename(L"parked-text-config", L"CFG/robots");
    std::ofstream settings(L"CFG/standalone.txt");
    settings << "RandomGenerator=ParkMiller\nWindowMode=Windowed\nResolutionWidth=1280\nResolutionHeight=720\n"
                "StencilShadows=Off\nProjectedShadows=Off\nRobotShadows=Off\nSkybox=Off\n";
    MG_CHECK(settings.good());
    fs::create_directories(L"DATA");
    std::ofstream package(L"DATA/robots.pkg", std::ios::binary);
    for (uint32_t word : {4u, 12u, 0u, 158u})
        for (int shift = 0; shift < 32; shift += 8) package.put(char((word >> shift) & 255));
    MG_CHECK(package.good());
}
void same_tree(const CBlockPar &text, const CBlockPar &packed) {
    MG_CHECK(text.ParCount() == packed.ParCount() && text.BlockCount() == packed.BlockCount());
    for (int i = 0; i < text.ParCount(); ++i)
        MG_CHECK(text.ParGetName(i) == packed.ParGetName(i) && text.ParGet(i) == packed.ParGet(i));
    for (int i = 0; i < text.BlockCount(); ++i) {
        MG_CHECK(text.BlockGetName(i) == packed.BlockGetName(i));
        same_tree(*text.BlockGet(i), *packed.BlockGet(i));
    }
}
template<class T> void column(CStorage &storage, const wchar_t *table, const wchar_t *name,
                             EStorageType type, std::initializer_list<T> expected) {
    auto *values = storage.GetBuf(table, name, type);
    MG_CHECK(values && values->GetArraysCount() == 1 && values->GetArrayLength(0) == expected.size());
    MG_CHECK(std::equal(expected.begin(), expected.end(), values->GetFirst<T>(0)));
}
void check_map(bool terron) {
    CStorage map;
    MG_CHECK(map.Load(terron ? L"Matrix/Map/mixed-boss-break.cmap" : L"Matrix/Map/mixed-break-anim.cmap"));
    column<DWORD>(map, DATA_OBJECTS, DATA_OBJECTS_TYPE, ST_DWORD, {1, 2, 3, 4});
    column<float>(map, DATA_OBJECTS, DATA_OBJECTS_X, ST_FLOAT, terron ?
        std::initializer_list<float>{100, 320, 540, 100} : std::initializer_list<float>{100, 220, 420, 320});
    column<float>(map, DATA_OBJECTS, DATA_OBJECTS_Y, ST_FLOAT, terron ?
        std::initializer_list<float>{540, 320, 100, 320} : std::initializer_list<float>{540, 300, 300, 440});
    column<float>(map, DATA_OBJECTS, DATA_OBJECTS_Z, ST_FLOAT, {10, 10, 10, 10});
    column<float>(map, DATA_OBJECTS, DATA_OBJECTS_ANGLE_Z, ST_FLOAT, {0, 0, 0, 0});
    column<float>(map, DATA_OBJECTS, DATA_OBJECTS_SCALE, ST_FLOAT, terron ?
        std::initializer_list<float>{1, 3, 1, 1} : std::initializer_list<float>{1, 1, 1, 1});
    auto *strings = map.GetBuf(DATA_STRINGS, DATA_STRINGS_STRING, ST_WCHAR);
    MG_CHECK(strings && strings->GetArraysCount() == 5);
    MG_CHECK(strings->GetAsWStr(1).find(L"*Static*") != std::wstring::npos);
    MG_CHECK(strings->GetAsWStr(2).find(terron ? L"+Break,1,300,Terron" : L"+Break,1,300,None") != std::wstring::npos);
    MG_CHECK(strings->GetAsWStr(3).find(terron ? L"+Break,1,300,None" : L"+Anim,0:0:300:1#1:1:0:-1") != std::wstring::npos);
    MG_CHECK(strings->GetAsWStr(4).find(L"*Break,1,300,None*") != std::wstring::npos);
    column<BYTE>(map, DATA_ROBOTS, DATA_ROBOTS_SIDE, ST_BYTE, terron ?
        std::initializer_list<BYTE>{1, 1, 1} : std::initializer_list<BYTE>{1, 1});
    column<float>(map, DATA_ROBOTS, DATA_ROBOTS_X, ST_FLOAT, terron ?
        std::initializer_list<float>{420, 560, 260} : std::initializer_list<float>{320, 500});
    column<float>(map, DATA_ROBOTS, DATA_ROBOTS_Y, ST_FLOAT, terron ?
        std::initializer_list<float>{160, 560, 320} : std::initializer_list<float>{100, 500});
    column<BYTE>(map, DATA_ROBOTS, DATA_ROBOTS_SHADOW, ST_BYTE, terron ?
        std::initializer_list<BYTE>{0, 0, 0} : std::initializer_list<BYTE>{0, 0});
    for (const auto *name : {DATA_ROBOTS_SHADOWSIZE, DATA_ROBOTS_GROUP})
        column<INT32>(map, DATA_ROBOTS, name, ST_INT32, terron ?
            std::initializer_list<INT32>{0, 0, 0} : std::initializer_list<INT32>{0, 0});
    auto *units = map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, ST_WCHAR);
    MG_CHECK(units && units->GetArraysCount() == (terron ? 3u : 2u));
    const auto laser = L"|" + std::to_wstring(MRT_WEAPON) + L"," + std::to_wstring(RUK_WEAPON_LASER);
    MG_CHECK(units->GetAsWStr(0).find(laser) != std::wstring::npos);
    for (DWORD i = 1; i < units->GetArraysCount(); ++i)
        MG_CHECK(units->GetAsWStr(i).find(laser) == std::wstring::npos);
}
void check_layout() {
    for (const auto *file : {L"pixel.png", L"synthetic.map", L"CFG/robots/data.txt", L"CFG/robots/iface.txt",
         L"CFG/standalone.txt", L"CFG/robots.fixture-packed", L"DATA/robots.pkg", L"Matrix/Fixture/special.vo",
         L"Matrix/Fixture/grey.png", L"Matrix/Fixture/red.png", L"Matrix/Fixture/green.png", L"Matrix/Fixture/blue.png"})
        MG_CHECK(fs::is_regular_file(file) && fs::file_size(file) > 0);
    MG_CHECK(!fs::exists(L"CFG/robots.dat") && !fs::exists(L"parked-text-config"));
    const auto settings = StandaloneSettings::load();
    MG_CHECK(settings.invalid.empty() && settings.values.random == random::Mode::ParkMiller);
    MG_CHECK(settings.values.display.mode == Display::Mode::Windowed);
    MG_CHECK(settings.values.display.width == 1280 && settings.values.display.height == 720);
    MG_CHECK(!settings.values.stencil_shadows && !settings.values.projected_shadows && !settings.values.robot_shadows);
    MG_CHECK(settings.values.skybox == StandaloneSettings::Skybox::Off);
    CBlockPar data, interface_config, packed_data, packed_interface;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    interface_config.LoadFromTextFile(L"CFG/robots/iface.txt");
    CStorage packed;
    MG_CHECK(packed.Load(L"CFG/robots.fixture-packed"));
    packed.RestoreBlockPar(L"da", packed_data);
    packed.RestoreBlockPar(L"if", packed_interface);
    same_tree(data, packed_data); same_tree(interface_config, packed_interface);
    MG_CHECK(data.BlockGet(L"Config")->ParGet(L"Map") == L"Matrix/Map/mixed-break-anim.cmap");
    Base::CPackCollection packages(nullptr);
    packages.AddPacketFile(L"DATA/robots.pkg");
    MG_CHECK(packages.OpenPacketFiles() && packages.m_PackFiles.size() == 1);
    CStorage mesh;
    MG_CHECK(mesh.Load(L"Matrix/Fixture/special.vo"));
    auto *names = mesh.GetBuf(L"anims", L"name", ST_WCHAR);
    MG_CHECK(names);
    for (const auto *required : {L"Main", L"Pain", L"Death"}) {
        bool found = false;
        for (DWORD i = 0; i < names->GetArraysCount(); ++i) found |= names->GetAsWStr(i) == required;
        MG_CHECK(found);
    }
    check_map(false); check_map(true);
}
void export_to(const fs::path &path, void (*generate)() = assets) {
    Directory directory(path);
    generate(); check_layout();
    directory.close(true); // Retain only a complete, validated export.
}
void export_layout() {
    Directory directory("special-layout-" + std::to_string(GetCurrentProcessId()));
    const auto destination = directory.root / L"export-\u0416 \u03bb";
    export_to(destination);
    MG_CHECK(fs::current_path() == directory.root);
    fs::current_path(destination);
    check_layout();
    tests::pack_native_session_configuration();
    MG_CHECK(fs::is_regular_file(L"CFG/robots.dat") && fs::is_directory(L"parked-text-config"));
    MG_CHECK(!fs::exists(L"CFG/robots/data.txt") && !fs::exists(L"CFG/robots/iface.txt"));
    directory.close();
}
void export_ownership() {
    Directory directory("special-ownership-" + std::to_string(GetCurrentProcessId()));
    fs::create_directory(L"existing");
    { std::ofstream sentinel(L"existing/sentinel.txt"); sentinel << "retained"; }
    bool rejected = false;
    try { export_to(directory.root / "existing"); }
    catch (const std::exception &) { rejected = true; }
    MG_CHECK(rejected && fs::file_size(L"existing/sentinel.txt") == 8);
    fs::create_directory(L"existing-empty");
    rejected = false;
    try { export_to(directory.root / "existing-empty"); }
    catch (const std::exception &) { rejected = true; }
    MG_CHECK(rejected && fs::is_directory(L"existing-empty") && fs::is_empty(L"existing-empty"));
    rejected = false;
    try { export_to(directory.root / "missing-parent" / "new"); }
    catch (const std::exception &) { rejected = true; }
    MG_CHECK(rejected && !fs::exists(L"missing-parent"));
    rejected = false;
    std::string reason;
    try {
        export_to(directory.root / "incomplete", [] {
            assets(); fs::remove(L"Matrix/Fixture/special.vo");
        });
    }
    catch (const std::exception &error) { rejected = true; reason = error.what(); }
    MG_CHECK(rejected && reason.find("fs::is_regular_file(file)") != std::string::npos);
    MG_CHECK(fs::current_path() == directory.root && !fs::exists(L"incomplete"));
    MG_CHECK(fs::file_size(L"existing/sentinel.txt") == 8);
    directory.close();
}
#if MATRIXGAME_SCENARIO_BUILD_DLL
std::wstring native_diagnostics;
void diagnostic(const wchar_t *title, const wchar_t *text) {
    native_diagnostics += std::wstring(title) + L": " + text + L"\n";
}
struct NativeDiagnostic : std::runtime_error { using std::runtime_error::runtime_error; };
void reject_diagnostics() {
    if (!native_diagnostics.empty())
        throw NativeDiagnostic("Native diagnostic: " + utils::from_wstring(native_diagnostics));
}
struct NativeHost {
    HWND parent{}, child{};
    ATOM registered{};
    IDirect3D9 *api{};
    IDirect3DDevice9 *device{};
    explicit NativeHost(HWND owner) : parent(owner) {
        MG_CHECK(parent && IsWindow(parent) && !IsWindowVisible(parent));
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = DefWindowProcW;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"MatrixGame_wc";
        registered = RegisterClassW(&window_class);
        MG_CHECK(registered);
        child = CreateWindowExW(WS_EX_NOACTIVATE, L"MatrixGame_wc", L"Synthetic objectives", WS_CHILD,
            0, 0, 640, 480, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!child) {
            UnregisterClassW(L"MatrixGame_wc", GetModuleHandleW(nullptr)); registered = 0;
            throw std::runtime_error("Cannot create contained special-scene child");
        }
    }
    void close() {
        if (device) { device->Release(); device = nullptr; }
        if (api) { api->Release(); api = nullptr; }
        if (child) { MG_CHECK(DestroyWindow(child)); child = nullptr; }
        if (registered) {
            MG_CHECK(UnregisterClassW(L"MatrixGame_wc", GetModuleHandleW(nullptr))); registered = 0;
        }
    }
    ~NativeHost() {
        if (device) device->Release();
        if (api) api->Release();
        if (child) DestroyWindow(child);
        if (registered) UnregisterClassW(L"MatrixGame_wc", GetModuleHandleW(nullptr));
    }
    void create_device() {
        MG_CHECK(child && GetParent(child) == parent && !IsWindowVisible(child));
        MG_CHECK(GetWindowLongW(child, GWL_EXSTYLE) & WS_EX_NOACTIVATE);
        api = Direct3DCreate9(D3D_SDK_VERSION);
        MG_CHECK(api);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.hDeviceWindow = child; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferWidth = 640; pp.BackBufferHeight = 480; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        const auto result = api->CreateDevice(0, D3DDEVTYPE_HAL, child, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &pp, &device);
        if (result != D3D_OK)
            throw std::runtime_error("Native Direct3D 9 device unavailable: " + std::to_string(result));
    }
};
struct Observation {
    HWND window;
    int scene, ticks{}, hits{}, texts{}, text_clears{};
    bool checked{}, closing{};
    bool outcome{}, reverse{}, won{}, statistics{}, inject_diagnostic{};
    int phase{}, retired_at{};
    CMatrixMapObject *target{};
    CMatrixMapObject *second{};
    CMatrixMapObject *red{}, *green{}, *blue{};
    CMatrixRobotAI *near_robot{}, *far_robot{};
    std::string failure;
} *observation;
struct ObservationScope {
    explicit ObservationScope(Observation &current) { MG_CHECK(!observation); observation = &current; }
    ~ObservationScope() { observation = nullptr; }
};
void __stdcall sound(wchar *) {}
dword __stdcall create_sound(wchar *, int, int) { return 0; }
void __stdcall sound_id(dword) {}
int __stdcall playing(dword) { return 0; }
void __stdcall set_sound(dword, float) {}
float __stdcall get_sound(dword) { return 0; }
void __stdcall text(wchar *, wchar *, DWORD, int, int, int, int, int, int, int, Base::CRect *, SMGDRangersInterfaceText *out) {
    MG_CHECK(observation);
    out->m_SizeX = out->m_SizeY = 2; out->m_Pitch = 8; out->m_Buf = new BYTE[16]{};
    ++observation->texts;
}
void __stdcall clear_text(SMGDRangersInterfaceText *out) {
    MG_CHECK(observation && out->m_Buf && observation->text_clears < observation->texts);
    delete[] out->m_Buf; out->m_Buf = nullptr; ++observation->text_clears;
}
void __stdcall progress(float) {
    SetUnhandledExceptionFilter(nullptr); std::set_terminate([] { std::abort(); });
}
void __stdcall begin() {}
float __stdcall music() { return 1; }
void __stdcall set_music(float) {}
void press_enter() {
    g_FormCur->Keyboard(true, VK_RETURN); g_FormCur->Keyboard(false, VK_RETURN);
}
void fire(bool down) {
    static_cast<CFormMatrixGame *>(g_FormCur)->MouseKey(down ? B_DOWN : B_UP, VK_LBUTTON, 320, 240);
}
bool live_robot(CMatrixRobotAI *expected) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object == expected) return object->IsLiveRobot();
    return false;
}
void outcome_tick(Observation &o) {
    const int now = g_MatrixMap->GetTime();
    auto &side = *g_MatrixMap->GetPlayerSide();
    MG_CHECK(now < 30000);
    if (o.phase == 0 || o.phase == 2) {
        g_IFaceList->m_InFocus = UNKNOWN;
        g_MatrixMap->m_TraceStopObj = o.target;
        g_MatrixMap->m_TraceStopPos = o.target->GetGeoCenter();
        MG_CHECK(!g_MatrixMap->m_DialogModeName);
        const bool retired = o.scene == 1 ? o.target->m_BreakHitPoint <= 0 : !o.target->IsSpecial();
        if (retired) {
            fire(false);
            o.target = nullptr;
            o.retired_at = now;
            std::cout << "native retirement scene=" << o.scene << " reverse=" << o.reverse
                << " phase=" << o.phase << " time=" << now << std::endl;
            if (o.scene == 1) {
                // Leave direct control through the same key used by a player; the shooter stays in blast range.
                press_enter();
                MG_CHECK(!side.IsArcadeMode());
                o.phase = 3;
            } else if (o.phase == 0) {
                MG_CHECK(g_MatrixMap->m_BeforeWinCount == 1 && side.GetStatValue(STAT_BUILDING_KILL) == 1);
                o.phase = 1;
            } else {
                MG_CHECK(g_MatrixMap->m_BeforeWinCount == 0 && side.GetStatValue(STAT_BUILDING_KILL) == 2);
                o.phase = 3;
            }
        }
    } else if (o.phase == 1) {
        MG_CHECK(!g_MatrixMap->m_DialogModeName && g_MatrixMap->m_BeforeWinCount == 1);
        MG_CHECK(side.GetStatus() == SS_ACTIVE);
        MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_WIN | MMFLAG_SPECIAL_BROKEN)));
        MG_CHECK(o.second->IsSpecial() && o.second->m_BreakHitPoint == 300);
        if (now - o.retired_at >= 2200) {
            o.target = o.second;
            o.phase = 2;
            fire(true);
        }
    } else if (o.phase == 3) {
        MG_CHECK(now - o.retired_at < 10000);
        if (!g_MatrixMap->m_DialogModeName) return;
        MG_CHECK(wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_WIN) == 0);
        MG_CHECK(side.GetStatValue(STAT_BUILDING_KILL) == (o.scene == 1 ? 1 : 2));
        MG_CHECK(side.GetStatValue(STAT_ROBOT_KILL) == 0);
        MG_CHECK(o.red->m_BehFlag == BEHF_STATIC);
        if (o.scene == 1) {
            MG_CHECK(g_MatrixMap->m_BeforeWinCount == 1);
            MG_CHECK(o.green->IsSpecial() && o.green->m_BreakHitPoint == 300);
            MG_CHECK(o.blue->m_BehFlag == BEHF_STATIC);
            MG_CHECK(!live_robot(o.near_robot) && live_robot(o.far_robot));
            MG_CHECK(o.far_robot->GetHitPoint() == 190);
        } else {
            MG_CHECK(!o.green->IsSpecial() && g_MatrixMap->m_BeforeWinCount == 0);
            MG_CHECK(o.blue->m_BehFlag == BEHF_BREAK && o.blue->m_BreakHitPoint == 300);
        }
        o.won = true;
        press_enter();
        MG_CHECK(g_ExitState == 3 && FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG));
        o.phase = 4;
    } else if (o.phase == 4 && g_MatrixMap->m_DialogModeName) {
        MG_CHECK(wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_STATISTICS) == 0);
        o.statistics = true;
        press_enter();
        MG_CHECK(g_ExitState == 3 && FLAG(g_Flags, GFLAG_EXITLOOP));
        o.closing = true;
        o.phase = 5;
    }
}
void __stdcall tick() {
    try {
        auto &o = *observation;
        MG_CHECK(++o.ticks <= 100000 && g_MatrixMap && g_FormCur && !IsWindowVisible(o.window));
        if (!o.outcome) {
            MG_CHECK(g_MatrixMap->GetTime() < 10000);
            if (o.scene != 2 && o.ticks > 1)
                MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) && !g_MatrixMap->m_DialogModeName);
        }
        if (o.ticks == 1) {
            if (o.inject_diagnostic)
                MessageBoxW(o.window, L"intentional special-scene diagnostic", L"Fixture diagnostic", MB_OK);
            MG_CHECK(g_MatrixMap->m_DialogModeName &&
                wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_BEGIN) == 0);
            std::vector<CMatrixMapObject *> objects;
            g_MatrixMap->FindObjects(D3DXVECTOR3(320,320,10), 1000, 1, TRACE_OBJECT, nullptr,
                [](const D3DXVECTOR3 &, CMatrixMapStatic *object, uintptr_t value) {
                    reinterpret_cast<std::vector<CMatrixMapObject *> *>(value)->push_back(static_cast<CMatrixMapObject *>(object));
                    return true;
                }, reinterpret_cast<uintptr_t>(&objects));
            std::sort(objects.begin(), objects.end(), [](auto *a, auto *b) { return a->m_Type < b->m_Type; });
            MG_CHECK(objects.size() == (o.scene == 2 ? 0u : 4u));
            MG_CHECK(g_MatrixMap->m_BeforeWinCount == (o.scene == 2 ? 0 : 2));
            MG_CHECK(g_ExitState == 0);
            MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_WIN | MMFLAG_SPECIAL_BROKEN | MMFLAG_TERRON_DEAD)));
            MG_CHECK(!g_MatrixMap->GetPlayerSide()->IsArcadeMode() && !Input::isVKeyPressed(VK_LBUTTON));
            MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_BUILDING_KILL) == 0);
            if (o.scene != 2) {
                MG_CHECK(objects[0]->m_BehFlag == BEHF_STATIC && !objects[0]->IsSpecial());
                MG_CHECK(objects[1]->m_BehFlag == (o.scene == 0 ? BEHF_BREAK : BEHF_TERRON));
                MG_CHECK(objects[2]->m_BehFlag == (o.scene == 0 ? BEHF_ANIM : BEHF_BREAK));
                MG_CHECK(objects[3]->m_BehFlag == BEHF_BREAK && !objects[3]->IsSpecial());
                for (int i = 0; i < 4; ++i) {
                    MG_CHECK(objects[i]->m_Type == i + 1);
                    if (i) MG_CHECK(objects[i]->m_BreakHitPoint == 300);
                    MG_CHECK(objects[i]->IsSpecial() == (i == 1 || i == 2));
                    MG_CHECK(objects[i]->m_Graph && objects[i]->m_Graph->VO()->GetAnimByName(L"Main") >= 0);
                    MG_CHECK(objects[i]->m_Graph->VO()->GetAnimByName(L"Pain") >= 0);
                    MG_CHECK(objects[i]->m_Graph->VO()->GetAnimByName(L"Death") >= 0);
                }
                const auto &matrix = objects[1]->GetMatrix();
                MG_CHECK(matrix._41 == (o.scene == 0 ? 220 : 320) && matrix._42 == (o.scene == 0 ? 300 : 320));
                MG_CHECK(objects[1]->m_Scale == (o.scene == 0 ? 1 : 3));
            }
            int robots = 0, armed = 0;
            CMatrixRobotAI *shooter = nullptr;
            for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
                if (!object->IsLiveRobot()) continue;
                ++robots;
                if (o.scene != 2) MG_CHECK(object->GetSide() == PLAYER_SIDE);
                if (o.scene == 1 && object->AsRobot()->m_PosX == 260) o.near_robot = object->AsRobot();
                if (o.scene == 1 && object->AsRobot()->m_PosX == 560) o.far_robot = object->AsRobot();
                std::cout << "scene=" << o.scene << " robot-side=" << object->GetSide()
                    << " position=" << object->AsRobot()->m_PosX << ',' << object->AsRobot()->m_PosY
                    << " health=" << object->AsRobot()->GetHitPoint() << '\n';
                if (object->AsRobot()->GetWeapon(0).IsEffectPresent()) {
                    ++armed;
                    shooter = object->AsRobot();
                    MG_CHECK(object->GetSide() == PLAYER_SIDE && !object->AsRobot()->GetWeapon(1).IsEffectPresent());
                    MG_CHECK(object->AsRobot()->GetWeapon(0).GetWeaponType() == WEAPON_LASER);
                }
            }
            MG_CHECK(robots == (o.scene == 1 ? 3 : 2) && armed == (o.scene == 2 ? 0 : 1));
            auto *form = static_cast<CFormMatrixGame *>(g_FormCur);
            form->Keyboard(true, VK_RETURN); form->Keyboard(false, VK_RETURN);
            if (shooter) {
                o.red = objects[1]; o.green = objects[2]; o.blue = objects[3];
                o.target = o.reverse ? o.green : o.red;
                o.second = o.reverse ? o.red : o.green;
                if (o.scene == 1) MG_CHECK(o.near_robot && o.far_robot);
                auto &side = *g_MatrixMap->GetPlayerSide();
                side.CreateGroupFromCurrent(shooter); side.Select(ROBOT, nullptr);
                form->Keyboard(true, VK_RETURN); form->Keyboard(false, VK_RETURN);
                MG_CHECK(side.GetArcadedObject() == shooter);
                g_IFaceList->m_InFocus = UNKNOWN;
                form->MouseKey(B_DOWN, VK_LBUTTON, 320, 240);
            }
            o.checked = true;
        }
        if (o.outcome) { outcome_tick(o); return; }
        if (o.target) {
            // Supply resolved world aim; real manual control, weapon tracing and damage follow it.
            g_IFaceList->m_InFocus = UNKNOWN;
            g_MatrixMap->m_TraceStopObj = o.target;
            g_MatrixMap->m_TraceStopPos = o.target->GetGeoCenter();
            if (o.target->m_BreakHitPoint < 300) {
                MG_CHECK(o.target->m_BreakHitPoint > 0 && g_MatrixMap->m_BeforeWinCount == 2);
                ++o.hits;
                if (o.hits == 1) o.target = o.second;
                else {
                    o.target = nullptr;
                    static_cast<CFormMatrixGame *>(g_FormCur)->MouseKey(B_UP, VK_LBUTTON, 320, 240);
                }
            }
        }
        if (!o.closing && (o.hits == 2 || (o.scene == 2 && o.ticks == 3))) {
            o.closing = true;
            MG_CHECK(PostMessageW(o.window, WM_CLOSE, 0, 0));
        }
    } catch (const std::exception &error) { observation->failure = error.what(); throw; }
    catch (...) { observation->failure = "Unexpected native special-scene callback exception"; throw; }
}
void special_sessions(int outcome = -1, bool inject_diagnostic = false) {
    Directory directory("native-special-" + std::to_string(GetCurrentProcessId()));
    assets();
    tests::NativeWindowContainment containment(diagnostic, false);
    NativeHost native(containment.parent());
    native.create_device();
    const auto device_references = native.device->AddRef(); native.device->Release();
    const auto original_procedure = GetWindowLongPtrA(native.child, GWLP_WNDPROC);
    MG_CHECK(original_procedure);
    auto settings = SRobotsSettings::generate_default_settings();
    settings.FDirect3D = reinterpret_cast<uintptr_t>(native.api);
    settings.FD3DDevice = reinterpret_cast<uintptr_t>(native.device);
    settings.m_ResolutionX = 640; settings.m_ResolutionY = 480;
    settings.m_FSAASamples = 0; settings.m_VSync = false; settings.m_RefreshRate = 0;
    settings.m_SoftwareCursor = true; settings.m_RobotShadow = 0;
    settings.m_ShowStencilShadows = settings.m_ShowProjShadows = false;
    SMGDRangersInterface callbacks{sound, create_sound, sound_id, sound_id, playing,
        set_sound, set_sound, get_sound, get_sound, text, clear_text, progress, tick, begin, music, set_music};
    auto *entry = GetRobotInterface(); entry->m_Init(&callbacks);
    struct Guard { SMGDRobotInterface *entry; ~Guard() { entry->m_Deinit(); observation = nullptr; } } guard{entry};
    const wchar_t *maps[] = {L".\\Matrix\\Map\\mixed-break-anim.cmap", L".\\Matrix\\Map\\mixed-boss-break.cmap", L".\\synthetic.map"};
    for (bool packed : {false, true}) {
        if (packed) tests::pack_native_session_configuration();
        const std::vector<int> scenes = inject_diagnostic ? std::vector<int>{2} :
            outcome < 0 ? std::vector<int>{0,1,2} : std::vector<int>{outcome == 2 ? 1 : 0, 2};
        for (int scene : scenes) {
            Observation observed{}; observed.window = native.child; observed.scene = scene;
            ObservationScope observation_scope(observed);
            observed.outcome = outcome >= 0 && scene != 2; observed.reverse = outcome == 1;
            observed.inject_diagnostic = inject_diagnostic;
            native_diagnostics.clear();
            SRobotGameState result{};
            std::wstring map(maps[scene]);
            const int exit = entry->m_Run(GetModuleHandleW(nullptr), native.child, map.data(), &settings,
                nullptr, nullptr, nullptr, nullptr, nullptr, &result);
            SetUnhandledExceptionFilter(nullptr); std::set_terminate([] { std::abort(); });
            if (!observed.failure.empty()) throw std::runtime_error(observed.failure);
            MG_CHECK(exit == (observed.outcome ? 3 : 0) && observed.checked && observed.closing && observed.ticks >= 3);
            if (observed.outcome) {
                MG_CHECK(observed.won && observed.statistics && observed.phase == 5);
                MG_CHECK(result.m_KillBuilding == (scene == 1 ? 1 : 2));
                MG_CHECK(result.m_KillRobot == 0);
            } else MG_CHECK(observed.hits == (scene == 2 ? 0 : 2));
            if (scene == 2) MG_CHECK(result.m_KillBuilding == 0 && result.m_KillRobot == 0);
            MG_CHECK(observed.texts > 0 && observed.texts == observed.text_clears);
            MG_CHECK(!g_MatrixMap && !g_MatrixData && !g_Render && !g_IFaceList);
            MG_CHECK(!g_Cache && !g_CacheHeap && !g_MatrixHeap && !g_D3DD && !g_D3D && !g_Wnd);
            MG_CHECK(!g_FormCur && !g_FormFirst && !g_FormLast && !CMatrixMapStatic::GetFirstLogic());
            MG_CHECK(IsWindow(native.child) && !IsWindowVisible(native.child));
            MG_CHECK(GetParent(native.child) == containment.parent());
            MG_CHECK(GetWindowLongPtrA(native.child, GWLP_WNDPROC) == original_procedure);
            MG_CHECK(native.device->TestCooperativeLevel() == D3D_OK);
            const auto remaining = native.device->AddRef(); native.device->Release();
            MG_CHECK(remaining == device_references);
            MG_CHECK(native.device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0) == D3D_OK);
            // A successful Present can be occluded; this proves reuse, not visible output.
            MG_CHECK(SUCCEEDED(native.device->Present(nullptr, nullptr, nullptr, nullptr)));
            // An expected diagnostic cannot mask a failed Run, callback or cleanup assertion.
            reject_diagnostics();
            std::cout << "native-special scene=" << scene << " packed=" << packed
                << " initialized=1 weapon-targets-hit=" << observed.hits << " outcome=" << outcome
                << " frames=" << observed.ticks << " exit=" << exit << '\n';
        }
    }
    native.close();
}
void special_scene_initialization() { special_sessions(); }
void break_anim_outcome() { special_sessions(0); }
void anim_break_outcome() { special_sessions(1); }
void terron_outcome() { special_sessions(2); }
void diagnostic_failure() {
    bool failed = false;
    try { special_sessions(-1, true); }
    catch (const NativeDiagnostic &error) {
        MG_CHECK(native_diagnostics == L"Fixture diagnostic: intentional special-scene diagnostic\n");
        MG_CHECK(std::string(error.what()) == "Native diagnostic: Fixture diagnostic: intentional special-scene diagnostic\n");
        failed = true;
    }
    MG_CHECK(failed);
}
#endif
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit(); SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result;
    if (argc >= 2 && std::strcmp(argv[1], "--export") == 0) {
        try {
            MG_CHECK(argc == 3);
            int count = 0;
            auto **wide = CommandLineToArgvW(GetCommandLineW(), &count);
            MG_CHECK(wide);
            struct Arguments { wchar_t **value; ~Arguments() { LocalFree(value); } } arguments{wide};
            MG_CHECK(count == 3 && std::wstring(wide[1]) == L"--export");
            const auto destination = fs::absolute(fs::path(wide[2])).lexically_normal();
            export_to(destination);
            const auto encoded = destination.u8string();
            std::cout << std::string(encoded.begin(), encoded.end()) << '\n';
            result = 0;
        }
        catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
    } else {
        const tests::Case cases[] = {
            {"game.session.special_export_layout", export_layout},
            {"game.session.special_export_ownership", export_ownership},
#if MATRIXGAME_SCENARIO_BUILD_DLL
            {"game.session.native_special_scenes", special_scene_initialization},
            {"game.session.native_break_anim", break_anim_outcome},
            {"game.session.native_anim_break", anim_break_outcome},
            {"game.session.native_terron", terron_outcome},
            {"game.session.native_special_diagnostic", diagnostic_failure},
#endif
        };
        result = tests::run(argc, argv, cases);
    }
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
