// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "native_session_assets.hpp"
#include "native_window_containment.hpp"
#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFormGame.hpp"
#include "Interface/CInterface.h"
#include "CStorage.hpp"
#include "scenario_build_identity.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <float.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;
std::wstring PathToOutputFiles(const wchar_t *destination) {
    const auto path = fs::current_path() / destination;
    fs::create_directories(path);
    return path.wstring();
}

namespace {
struct Directory {
    fs::path previous = fs::current_path();
    fs::path root = previous / ("loaded-navigation-" + std::to_string(GetCurrentProcessId()));
    Directory() {
        MG_CHECK(root.is_absolute() && root.parent_path() == previous);
        MG_CHECK(!fs::exists(root));
        fs::create_directory(root);
        fs::current_path(root);
    }
    ~Directory() {
        std::error_code error;
        fs::current_path(previous, error);
        if (!error) fs::remove_all(root, error);
        if (error) std::cerr << "Cannot clean loaded-navigation fixture: " << error.message() << '\n';
    }
};

constexpr int terrain_size = 32;
constexpr int movement_size = terrain_size * 2;
float terrain_height(int x) {
    if (x >= 8 && x < 12) return -3;
    if (x >= 12 && x < 16) return -40;
    if (x >= 20 && x <= 24) return 10 + (x - 20) * 12;
    return x > 24 ? 58 : 10;
}
DWORD authored_restriction(int x, int y) {
    if (x < 2 || y < 2 || x >= movement_size - 2 || y >= movement_size - 2) return 31;
    if (x >= 24 && x < 32) return 7; // Deep strip: pneumatic, wheel and track blocked.
    if (x >= 40 && x < 48) return 6; // Steep strip: wheel and track blocked.
    return 0;
}
SCompileMove movement_record(int x, int y) {
    SCompileMove result{};
    result.m_Zone = 0;
    for (int size = 1; size <= 5; ++size) {
        DWORD blocked = 0;
        for (int dy = 0; dy < size; ++dy)
            for (int dx = 0; dx < size; ++dx) blocked |= authored_restriction(x + dx, y + dy);
        result.m_Move |= blocked << (6 * (size - 1));
    }
    if (x >= 4 && x < 6 && y >= 4 && y < 6) {
        const DWORD index = (x - 4) + 2 * (y - 4);
        result.m_Sphere = 0x01020408u + index;
        result.m_Zubchik = 0x10203040u + index;
    }
    return result;
}

void write_terrain_map() {
    tests::write_native_session_assets();
    CStorage storage;
    MG_CHECK(storage.Load(L"synthetic.map"));
    auto *points = storage.GetBuf(DATA_POINTS, DATA_POINTS_DATA, ST_BYTE)->GetFirst<SCompilePoint>(0);
    std::vector<SCompileMoveCell> cells(terrain_size * terrain_size);
    for (int y = 0; y <= terrain_size; ++y) for (int x = 0; x <= terrain_size; ++x) {
        auto &point = points[x + y * (terrain_size + 1)];
        point.z = terrain_height(x);
        const bool water = x >= 12 && x < 16;
        point.flags = water ? CELLFLAG_WATER : CELLFLAG_LAND;
        if (terrain_height(x) == terrain_height(x + 1)) point.flags |= CELLFLAG_FLAT;
        if (x == terrain_size || y == terrain_size) continue;
        const int index = x + y * terrain_size;
        point.move = index;
        for (int sub = 0; sub < 4; ++sub)
            cells[index].c[sub] = movement_record(x * 2 + (sub & 1), y * 2 + (sub >> 1));
    }
    storage.GetBuf(DATA_MOVE, DATA_MOVE_DATA, ST_BYTE)->SetArray<BYTE>(0,
        reinterpret_cast<const BYTE *>(cells.data()), int(cells.size() * sizeof(SCompileMoveCell)));
    auto *serialized_roads = storage.GetBuf(DATA_ROADS, DATA_ROADS_DATA, ST_BYTE);
    CBuf network;
    network.Add(serialized_roads->GetFirst<BYTE>(0), serialized_roads->GetArrayLength(0));
    network.Pointer(0);
    MG_CHECK(network.Get<DWORD>() == 27);
    CMatrixRoadNetwork roads(nullptr);
    roads.Load(network, 27);
    MG_CHECK(roads.m_PlaceCnt == 4);
    for (int i = 0; i < roads.m_PlaceCnt; ++i) {
        MG_CHECK(roads.m_Place[i].m_Near[0] == (i ^ 1) && roads.m_Place[i].m_Near[1] == (i ^ 2));
        roads.m_Place[i].m_NearMove[0] = 7; // Horizontal edges cross the deep-water strip.
        roads.m_Place[i].m_NearMove[1] = 0;
    }
    network.Clear();
    network.Add<DWORD>(27);
    roads.Save(network);
    serialized_roads->SetArray<BYTE>(0, network.Buff<BYTE>(), int(network.Len()));
    storage.Save(L"terrain-navigation.map");
}

void storage_profile() {
    Directory directory;
    write_terrain_map();
    CStorage storage;
    MG_CHECK(storage.Load(L"terrain-navigation.map"));
    auto *points = storage.GetBuf(DATA_POINTS, DATA_POINTS_DATA, ST_BYTE);
    auto *moves = storage.GetBuf(DATA_MOVE, DATA_MOVE_DATA, ST_BYTE);
    MG_CHECK(points->GetArrayLength(0) == 33 * 33 * sizeof(SCompilePoint));
    MG_CHECK(moves->GetArrayLength(0) == 32 * 32 * sizeof(SCompileMoveCell));
    const auto *p = points->GetFirst<SCompilePoint>(0);
    const auto *m = moves->GetFirst<SCompileMoveCell>(0);
    MG_CHECK(p[5 + 5 * 33].z == 10 && p[9 + 5 * 33].z == -3 && p[13 + 5 * 33].z == -40);
    MG_CHECK((p[9 + 5 * 33].flags & CELLFLAG_LAND) && !(p[9 + 5 * 33].flags & CELLFLAG_WATER));
    MG_CHECK((p[13 + 5 * 33].flags & CELLFLAG_WATER) && !(p[21 + 5 * 33].flags & CELLFLAG_FLAT));
    MG_CHECK(p[21 + 5 * 33].z == 22 && p[24 + 5 * 33].z == 58);
    for (int sub = 0; sub < 4; ++sub) {
        const auto &marker = m[p[2 + 2 * 33].move].c[sub];
        MG_CHECK(marker.m_Sphere == 0x01020408u + sub && marker.m_Zubchik == 0x10203040u + sub);
        MG_CHECK(marker.m_Zone == 0 && marker.m_Move == 0);
    }
    const auto &deep = m[p[13 + 5 * 33].move].c[0];
    const auto &steep = m[p[21 + 5 * 33].move].c[0];
    for (int size = 1; size <= 5; ++size) {
        MG_CHECK(((deep.m_Move >> (6 * (size - 1))) & 31) == 7);
        MG_CHECK(((steep.m_Move >> (6 * (size - 1))) & 31) == 6);
    }
    CBuf network;
    auto *serialized_roads = storage.GetBuf(DATA_ROADS, DATA_ROADS_DATA, ST_BYTE);
    network.Add(serialized_roads->GetFirst<BYTE>(0), serialized_roads->GetArrayLength(0));
    network.Pointer(0);
    MG_CHECK(network.Get<DWORD>() == 27);
    CMatrixRoadNetwork roads(nullptr);
    roads.Load(network, 27);
    MG_CHECK(roads.m_PlaceCnt == 4);
    for (int i = 0; i < 4; ++i)
        MG_CHECK(roads.m_Place[i].m_NearMove[0] == 7 && roads.m_Place[i].m_NearMove[1] == 0);
}

#if MATRIXGAME_SCENARIO_BUILD_DLL
void diagnostic(const wchar_t *title, const wchar_t *message) {
    std::wcerr << title << L": " << message << L'\n';
    TerminateProcess(GetCurrentProcess(), 98);
}
struct Window {
    ATOM type{};
    HWND handle{};
    explicit Window(HWND parent) {
        WNDCLASSW definition{};
        definition.lpfnWndProc = DefWindowProcW;
        definition.hInstance = GetModuleHandleW(nullptr);
        definition.lpszClassName = L"MatrixGame_wc";
        type = RegisterClassW(&definition);
        if (type) handle = CreateWindowExW(WS_EX_NOACTIVATE, definition.lpszClassName, L"Loaded navigation", WS_CHILD,
            0, 0, 640, 480, parent, nullptr, definition.hInstance, nullptr);
    }
    ~Window() {
        if (handle) DestroyWindow(handle);
        if (type) UnregisterClassW(L"MatrixGame_wc", GetModuleHandleW(nullptr));
    }
};
struct Device {
    IDirect3D9 *api = Direct3DCreate9(D3D_SDK_VERSION);
    IDirect3DDevice9 *device{};
    explicit Device(HWND window) {
        if (!api) throw std::runtime_error("Native Direct3D 9 unavailable");
        D3DPRESENT_PARAMETERS parameters{};
        parameters.Windowed = TRUE;
        parameters.hDeviceWindow = window;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.BackBufferWidth = 640;
        parameters.BackBufferHeight = 480;
        parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
        const auto status = api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &parameters, &device);
        if (FAILED(status)) {
            api->Release();
            api = nullptr;
            throw std::runtime_error("Native Direct3D 9 device unavailable: " + std::to_string(status));
        }
    }
    ~Device() {
        if (device) device->Release();
        if (api) api->Release();
    }
};
void __stdcall sound(wchar *) {}
dword __stdcall sound_create(wchar *, int, int) { return 0; }
void __stdcall sound_id(dword) {}
int __stdcall sound_playing(dword) { return 0; }
void __stdcall sound_set(dword, float) {}
float __stdcall sound_get(dword) { return 0; }
void __stdcall text(wchar *, wchar *, DWORD, int, int, int, int, int, int, int, Base::CRect *, SMGDRangersInterfaceText *value) {
    value->m_SizeX = value->m_SizeY = 2;
    value->m_Pitch = 8;
    value->m_Buf = new BYTE[16]{};
}
void __stdcall text_clear(SMGDRangersInterfaceText *value) { delete[] value->m_Buf; value->m_Buf = nullptr; }
void __stdcall progress(float) { SetUnhandledExceptionFilter(nullptr); std::set_terminate([] { std::abort(); }); }
void __stdcall callback() {}
float __stdcall music_get() { return 1; }
void __stdcall music_set(float) {}

std::vector<std::string> loaded_run(random::Mode mode, int step) {
    Directory directory;
    write_terrain_map();
    tests::NativeWindowContainment containment(diagnostic, false);
    Window window(containment.parent());
    MG_CHECK(window.type && window.handle && !IsWindowVisible(window.handle));
    Device device(window.handle);
    auto settings = SRobotsSettings::generate_default_settings();
    settings.FDirect3D = reinterpret_cast<uintptr_t>(device.api);
    settings.FD3DDevice = reinterpret_cast<uintptr_t>(device.device);
    settings.m_ResolutionX = 640; settings.m_ResolutionY = 480;
    settings.m_FSAASamples = 0; settings.m_VSync = false; settings.m_RefreshRate = 0;
    settings.m_ShowStencilShadows = settings.m_ShowProjShadows = false;
    settings.m_RobotShadow = 0; settings.m_SoftwareCursor = true;
    SMGDRangersInterface callbacks{sound, sound_create, sound_id, sound_id, sound_playing,
        sound_set, sound_set, sound_get, sound_get, text, text_clear, progress, callback, callback, music_get, music_set};
    auto *entry = GetRobotInterface();
    entry->m_Init(&callbacks);
    struct Cleanup {
        SMGDRobotInterface *entry;
        bool active = true;
        void close() noexcept {
            if (!active) return;
            CGame::SafeFree(false);
            entry->m_Deinit();
            active = false;
        }
        ~Cleanup() { close(); }
    } cleanup{entry};
    CGame::Init(GetModuleHandleW(nullptr), window.handle, L".\\terrain-navigation.map", 81, &settings);
    // DLL initialization always selects ParkMiller. The controlled simulation
    // starts after successful loading and explicitly declares its own stream.
    random::seed(81, mode);
    std::cout << "fixture=loaded-terrain-navigation-v1 revision=" << MATRIXGAME_SCENARIO_REVISION
        << " dirty=" << MATRIXGAME_SCENARIO_DIRTY << " initialization_rng=ParkMiller seed=81 rng="
        << (mode == random::Mode::ParkMiller ? "ParkMiller" : "LegacyCRT") << " step_ms=" << step
        << " configuration=" << MATRIXGAME_SCENARIO_CONFIGURATION << " cheats=" << MATRIXGAME_SCENARIO_CHEATS
        << " precision_control=" << (_controlfp(0, 0) & _MCW_PC)
        << " render=native-initialization-only-hidden-child audio=host-create-zero";
#ifdef _MSC_VER
    std::cout << " compiler=MSVC version=" << _MSC_FULL_VER;
#else
    std::cout << " compiler=MinGW version=" << __VERSION__;
#endif
    std::cout << std::endl;
    MG_CHECK(g_MatrixMap && g_MatrixMap->m_Size.x == 32 && g_MatrixMap->m_SizeMove.x == 64);
    for (int i = 0; i < 4; ++i)
        MG_CHECK(g_MatrixMap->m_RN.m_Place[i].m_NearMove[0] == 7 && g_MatrixMap->m_RN.m_Place[i].m_NearMove[1] == 0);
    for (int sub = 0; sub < 4; ++sub) {
        const auto *cell = g_MatrixMap->MoveGet(4 + (sub & 1), 4 + (sub >> 1));
        MG_CHECK(cell->m_Sphere == 0x01020408u + sub && cell->m_Zubchik == 0x10203040u + sub);
    }
    MG_CHECK(g_MatrixMap->GetZ(100, 100) == 10);
    MG_CHECK(g_MatrixMap->GetZ(190, 100) == -3);
    MG_CHECK(g_MatrixMap->PointGet(13, 5)->z == -40 && g_MatrixMap->UnitGet(13, 5)->IsWater());
    MG_CHECK(g_MatrixMap->GetZ(270, 100) == -1000); // Water-only cells have no walkable land plane.
    MG_CHECK(std::abs(g_MatrixMap->GetZ(430, 100) - 28) < .0001f);
    D3DXVECTOR3 normal;
    g_MatrixMap->GetNormal(&normal, 430, 100);
    MG_CHECK(normal.x < -.5f && normal.x > -.52f && normal.y == 0 && normal.z > .85f && normal.z < .86f);
    for (int chassis = 0; chassis < 5; ++chassis) {
        MG_CHECK(g_MatrixMap->IsAbsenceWall(chassis, 4, 8, 8));
        for (int size = 1; size <= 5; ++size) {
            MG_CHECK(g_MatrixMap->IsAbsenceWall(chassis, size, 26, 8) == (chassis >= 3));
            MG_CHECK(g_MatrixMap->IsAbsenceWall(chassis, size, 42, 8) == (chassis == 0 || chassis >= 3));
        }
        MG_CHECK(g_MatrixMap->IsAbsenceWall(chassis, 1, 23, 8));
        MG_CHECK(g_MatrixMap->IsAbsenceWall(chassis, 4, 21, 8) == (chassis >= 3));
        MG_CHECK(!g_MatrixMap->IsAbsenceWall(chassis, 4, 8, 1));
        const auto path = [&](int from, int to, int expected) {
            std::array<CPoint, MatrixPathMoveMax> points;
            int zone = 0;
            const auto draws = random::draw_count();
            const int count = g_MatrixMap->FindLocalPath(chassis, 4, from, 8, &zone, 1, to, 8, points.data(),
                0, nullptr, nullptr, nullptr, nullptr, false);
            std::cout << "chassis=" << chassis + 1 << " route=" << from << ":" << to << " points=" << count << '\n';
            MG_CHECK(count == expected && random::draw_count() == draws);
            if (count) for (int i = 0; i < count; ++i) MG_CHECK(points[i] == CPoint(from + i, 8));
        };
        path(8, 18, 11);
        path(8, 36, chassis >= 3 ? 29 : 0);
        path(36, 52, chassis == 0 || chassis >= 3 ? 17 : 0);
        path(8, 18, 11); // A failed search must not poison a later valid search.
    }
    std::cout << "Loaded terrain and four compiled movement subcells through CGame::Init\n";
    CMatrixRobotAI *robot = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        MG_CHECK(object->IsLiveRobot());
        auto *candidate = object->AsRobot();
        candidate->SetMaxSpeed(object->GetSide() == PLAYER_SIDE ? .6f : 0);
        candidate->m_GroupSpeed = candidate->GetMaxSpeed();
        if (object->GetSide() == PLAYER_SIDE) { MG_CHECK(!robot); robot = candidate; }
    }
    MG_CHECK(robot && robot->m_Unit[0].u1.s1.m_Kind == RUK_CHASSIS_WHEEL);
    auto robot_core = std::unique_ptr<SObjectCore, void (*)(SObjectCore *)>(robot->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    auto form = std::make_unique<CFormMatrixGame>();
    FormChange(form.get());
    struct FormReset { ~FormReset() { FormChange(nullptr); } } form_reset;
    if (g_MatrixMap->IsPaused()) {
        form->Keyboard(true, VK_RETURN);
        form->Keyboard(false, VK_RETURN);
    }
    MG_CHECK(!g_MatrixMap->IsPaused());
    auto &side = *g_MatrixMap->GetPlayerSide();
    side.CreateGroupFromCurrent(robot);
    side.Select(ROBOT, nullptr);
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
    g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(270, 100, 0);
    side.OnRButtonDown(CPoint(0, 0));
    const int group = robot->GetGroupLogic();
    MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP && side.m_PlayerGroup[group].Order() == mpo_MoveTo);
    std::cout << "Blocked target order=" << side.m_PlayerGroup[group].m_To.x << ',' << side.m_PlayerGroup[group].m_To.y
        << " place=" << robot->GetEnv()->m_Place << " add=" << robot->GetEnv()->m_PlaceAdd.x << ','
        << robot->GetEnv()->m_PlaceAdd.y << std::endl;
    MG_CHECK(side.m_PlayerGroup[group].m_To == CPoint(25, 8));
    MG_CHECK(robot->GetEnv()->m_Place == -1 && robot->GetEnv()->m_PlaceAdd == CPoint(20, 3));
    MG_CHECK(g_MatrixMap->IsAbsenceWall(RUK_CHASSIS_WHEEL - 1, 4, 20, 3));
    std::vector<std::string> trace;
    const int started = g_MatrixMap->GetTime();
    int arrival = 0, wet_frames = 0;
    bool moved = false;
    for (int frame = 0; g_MatrixMap->GetTime() - started < 10000; ++frame) {
        MG_CHECK(frame < 1001);
        MG_CHECK(!g_MatrixMap->IsPaused());
        g_MatrixMap->Takt(step);
        g_MatrixMap->m_Camera.BeforeDraw();
        MG_CHECK(robot_core->m_Object == robot && robot->IsLiveRobot());
        MG_CHECK(std::isfinite(robot->m_PosX) && std::isfinite(robot->m_PosY));
        MG_CHECK(robot->m_PosX < 240 && robot->GetGroupLogic() == group);
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_MoveTo && side.m_PlayerGroup[group].m_To == CPoint(25, 8));
        moved |= robot->m_PosX > 120;
        wet_frames += robot->m_PosX >= 170 && robot->m_PosX < 230;
        if (!arrival && robot->m_PosX == 220 && robot->m_PosY == 50 && robot->PLIsInPlace())
            arrival = g_MatrixMap->GetTime() - started;
        if (g_MatrixMap->GetTime() - started >= 8000) {
            MG_CHECK(robot->m_PosX == 220 && robot->m_PosY == 50 && robot->PLIsInPlace());
            MG_CHECK(robot->m_Speed == 0 && robot->GetOrdersInPool() == 0);
        }
        std::ostringstream state;
        state << frame << ':' << g_MatrixMap->GetTime() - started << ':' << random::draw_count()
            << ':' << std::bit_cast<uint32_t>(robot->m_PosX) << ',' << std::bit_cast<uint32_t>(robot->m_PosY)
            << ',' << std::bit_cast<uint32_t>(robot->GetGeoCenter().z) << ',' << std::bit_cast<uint32_t>(robot->m_Speed)
            << ':' << robot->PLIsInPlace() << ':' << robot->GetOrdersInPool();
        trace.push_back(state.str());
    }
    std::cout << "Blocked target final=" << robot->m_PosX << ',' << robot->m_PosY << " arrived=" << robot->PLIsInPlace()
        << " order=" << int(side.m_PlayerGroup[robot->GetGroupLogic()].Order()) << std::endl;
    MG_CHECK(!IsWindowVisible(window.handle) && GetParent(window.handle) == containment.parent());
    MG_CHECK(moved && wet_frames > 0 && arrival > 0 && arrival < 10000);
    MG_CHECK(robot->m_PosX == 220 && robot->m_PosY == 50 && robot->PLIsInPlace());
    MG_CHECK(g_MatrixMap->GetZ(robot->m_PosX, robot->m_PosY) == -3);
    std::cout << "arrival_ms=" << arrival << " wet_frames=" << wet_frames << " draws=" << random::draw_count() << '\n';
    FormChange(nullptr);
    form.reset();
    robot_core.reset();
    cleanup.close();
    MG_CHECK(!g_MatrixMap && !g_MatrixHeap && !g_MatrixData && !g_IFaceList && !g_RangersInterface);
    return trace;
}
template<random::Mode Mode, int Step> void loaded_masks() {
    const auto first = loaded_run(Mode, Step);
    MG_CHECK(first == loaded_run(Mode, Step));
}
#endif
template<void (*Run)()> void engine_contract() {
    try { Run(); }
    catch (const Base::CException &error) { throw std::runtime_error(utils::from_wstring(error.Info())); }
}
constexpr tests::Case cases[]{
    {"game.navigation.loaded_map_assets", engine_contract<storage_profile>},
#if MATRIXGAME_SCENARIO_BUILD_DLL
    {"game.navigation.native_loaded_masks.park_miller10", engine_contract<loaded_masks<random::Mode::ParkMiller, 10>>},
    {"game.navigation.native_loaded_masks.park_miller16", engine_contract<loaded_masks<random::Mode::ParkMiller, 16>>},
    {"game.navigation.native_loaded_masks.legacy10", engine_contract<loaded_masks<random::Mode::LegacyCRT, 10>>},
    {"game.navigation.native_loaded_masks.legacy16", engine_contract<loaded_masks<random::Mode::LegacyCRT, 16>>},
#endif
};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) return EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
