// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFormGame.hpp"
#include "input.hpp"
#include "CStorage.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <vector>
#include <cstring>
#include <functional>
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
void table(CStorage &storage, const wchar_t *name,
    std::initializer_list<std::pair<const wchar_t *, EStorageType>> columns, bool arrays = true) {
    CStorageRecord record(name);
    for (const auto &[column, type] : columns) record.AddItem(CStorageRecordItem(column, type));
    storage.AddRecord(record);
    if (arrays) for (const auto &[column, type] : columns)
        if (type != ST_WCHAR) storage.GetBuf(name, column, type)->AddArray();
}
void write_map() {
    CStorage map;
    table(map, DATA_PROPERTIES, {{DATA_PROPERTIES_NAME, ST_WCHAR}, {DATA_PROPERTIES_VALUE, ST_WCHAR}});
    for (const auto *name : {DATA_SIZEX, DATA_SIZEY}) {
        map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, ST_WCHAR)->AddWStr(name);
        map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, ST_WCHAR)->AddWStr(L"32");
    }
    map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, ST_WCHAR)->AddWStr(DATA_TEXUNIONDIM);
    map.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, ST_WCHAR)->AddWStr(L"1");
    table(map, DATA_POINTS, {{DATA_POINTS_DATA, ST_BYTE}});
    table(map, DATA_MOVE, {{DATA_MOVE_DATA, ST_BYTE}});
    std::vector<SCompilePoint> points(33 * 33);
    for (auto &point : points) { point.z = 10; point.flags = CELLFLAG_LAND | CELLFLAG_FLAT; point.r = point.g = point.b = 128; }
    SCompileMoveCell cell{};
    for (auto &move : cell.c) move.m_Zone = 0;
    map.GetBuf(DATA_POINTS, DATA_POINTS_DATA, ST_BYTE)->AddToArray<BYTE>(0,
        reinterpret_cast<const BYTE *>(points.data()), points.size() * sizeof(SCompilePoint));
    map.GetBuf(DATA_MOVE, DATA_MOVE_DATA, ST_BYTE)->AddToArray<BYTE>(0,
        reinterpret_cast<const BYTE *>(&cell), sizeof(cell));
    table(map, DATA_STRINGS, {{DATA_STRINGS_STRING, ST_WCHAR}});
    map.GetBuf(DATA_STRINGS, DATA_STRINGS_STRING, ST_WCHAR)->AddWStr(L"pixel.png");
    table(map, DATA_TEXUINIONS, {{DATA_TEXUINIONS_DATA, ST_BYTE}});
    table(map, DATA_BOTTOM, {{DATA_BOTTOM_DATA, ST_INT32}});
    table(map, DATA_BITMAPS, {{DATA_BITMAPS_BITMAP, ST_BYTE}}, false);
    const int tile = 0;
    map.GetBuf(DATA_TEXUINIONS, DATA_TEXUINIONS_DATA, ST_BYTE)->AddToArray<BYTE>(0,
        reinterpret_cast<const BYTE *>(&tile), sizeof(tile));
    map.GetBuf(DATA_BOTTOM, DATA_BOTTOM_DATA, ST_INT32)->AddToArray<int>(0, &tile, 1);
    table(map, DATA_GROUPS, {{DATA_GROUPS_DATA, ST_BYTE}}, false);
    for (int y = 0; y < 32; y += MAP_GROUP_SIZE) for (int x = 0; x < 32; x += MAP_GROUP_SIZE) {
        std::vector<BYTE> bytes;
        const auto append = [&]<class T>(const T &value) {
            const auto *start = reinterpret_cast<const BYTE *>(&value);
            bytes.insert(bytes.end(), start, start + sizeof(value));
        };
        append(static_cast<WORD>(x)); append(static_cast<WORD>(y));
        append(DWORD{1}); append(DWORD{0}); append(DWORD{12});
        for (WORD index : {0, 1, 2, 1, 3, 2}) append(index);
        append(DWORD{4 * sizeof(SCompileBottomVert)});
        for (const auto &point : {CPoint(x,y), CPoint(x+MAP_GROUP_SIZE,y),
             CPoint(x,y+MAP_GROUP_SIZE), CPoint(x+MAP_GROUP_SIZE,y+MAP_GROUP_SIZE)})
            append(SCompileBottomVert{static_cast<uint16_t>(point.x),static_cast<uint16_t>(point.y),0,0});
        auto *groups = map.GetBuf(DATA_GROUPS, DATA_GROUPS_DATA, ST_BYTE);
        const auto index = groups->AddArray();
        groups->AddToArray<BYTE>(index, bytes.data(), bytes.size());
    }
    table(map, DATA_SURFACES, {{DATA_SURFACES_DATA, ST_BYTE}}, false);
    table(map, DATA_SURFACES_M, {{DATA_SURFACES_DATA_M, ST_BYTE}}, false);
    table(map, DATA_OBJECTS, {{DATA_OBJECTS_X, ST_FLOAT}, {DATA_OBJECTS_Y, ST_FLOAT},
        {DATA_OBJECTS_ANGLE_Z, ST_FLOAT}, {DATA_OBJECTS_SCALE, ST_FLOAT}, {DATA_OBJECTS_TYPE, ST_DWORD}});
    table(map, DATA_BUILDINGS, {{DATA_BUILDINGS_X, ST_FLOAT}, {DATA_BUILDINGS_Y, ST_FLOAT},
        {DATA_BUILDINGS_SIDE, ST_BYTE}, {DATA_BUILDINGS_KIND, ST_BYTE}, {DATA_BUILDINGS_SHADOW, ST_BYTE},
        {DATA_BUILDINGS_SHADOWSIZE, ST_INT32}, {DATA_BUILDINGS_ANGLE, ST_BYTE}});
    table(map, DATA_CANNONS, {{DATA_CANNONS_X, ST_FLOAT}, {DATA_CANNONS_Y, ST_FLOAT},
        {DATA_CANNONS_SIDE, ST_BYTE}, {DATA_CANNONS_KIND, ST_BYTE}, {DATA_CANNONS_SHADOW, ST_BYTE},
        {DATA_CANNONS_SHADOWSIZE, ST_INT32}, {DATA_CANNONS_ANGLE, ST_FLOAT}});
    table(map, DATA_ROBOTS, {{DATA_ROBOTS_X, ST_FLOAT}, {DATA_ROBOTS_Y, ST_FLOAT},
        {DATA_ROBOTS_SIDE, ST_BYTE}, {DATA_ROBOTS_SHADOW, ST_BYTE}, {DATA_ROBOTS_SHADOWSIZE, ST_INT32},
        {DATA_ROBOTS_GROUP, ST_INT32}, {DATA_ROBOTS_UNITS, ST_WCHAR}});
    const float coordinates[] = {100, 500};
    for (const auto *column : {DATA_ROBOTS_X, DATA_ROBOTS_Y})
        map.GetBuf(DATA_ROBOTS, column, ST_FLOAT)->AddToArray<float>(0, coordinates, 2);
    const BYTE sides[] = {1, 2}, shadows[] = {0, 0};
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_SIDE, ST_BYTE)->AddToArray<BYTE>(0, sides, 2);
    map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_SHADOW, ST_BYTE)->AddToArray<BYTE>(0, shadows, 2);
    const int zeroes[] = {0, 0};
    for (const auto *column : {DATA_ROBOTS_SHADOWSIZE, DATA_ROBOTS_GROUP})
        map.GetBuf(DATA_ROBOTS, column, ST_INT32)->AddToArray<int>(0, zeroes, 2);
    const std::wstring modules = std::to_wstring(MRT_CHASSIS) + L"," + std::to_wstring(RUK_CHASSIS_WHEEL) +
        L",0|" + std::to_wstring(MRT_ARMOR) + L"," + std::to_wstring(RUK_ARMOR_MONOSTACK);
    for (int i = 0; i < 2; ++i) map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, ST_WCHAR)->AddWStr(modules.c_str());

    CMatrixRoadNetwork road(nullptr);
    road.SetZoneCntMax(1); road.m_ZoneCnt = 1;
    road.m_Zone[0].m_Rect = CRect(0, 0, 64, 64);
    road.m_Zone[0].m_Center = CPoint(32, 32);
    road.m_Zone[0].m_Size = 64 * 64;
    road.m_Zone[0].m_Region = 0;
    road.m_Region = static_cast<SMatrixRegion *>(HAllocClear(sizeof(SMatrixRegion), nullptr));
    road.m_RegionCnt = 1;
    road.m_Region[0].m_Center = CPoint(32, 32);
    road.m_Region[0].m_RadiusPlace = 64;
    road.SetPlaceCnt(4);
    for (int i = 0; i < 4; ++i) {
        auto &place = road.m_Place[i];
        place.m_Pos = CPoint((i & 1) ? 48 : 8, (i & 2) ? 48 : 8);
        place.m_Region = 0; place.m_Empty = false; place.m_NearCnt = 2;
        place.m_Near[0] = i ^ 1; place.m_Near[1] = i ^ 2;
        road.m_Zone[0].m_Place[i] = road.m_Region[0].m_Place[i] = road.m_Region[0].m_PlaceAll[i] = i;
    }
    road.m_Zone[0].m_PlaceCnt = 4;
    road.m_Region[0].m_PlaceCnt = road.m_Region[0].m_PlaceAllCnt = 4;
    CBuf network;
    network.Add<DWORD>(27);
    road.Save(network);
    table(map, DATA_ROADS, {{DATA_ROADS_DATA, ST_BYTE}});
    map.GetBuf(DATA_ROADS, DATA_ROADS_DATA, ST_BYTE)->AddToArray<BYTE>(0, network.Buff<BYTE>(), network.Len());
    map.Save(L"synthetic.map");
}
void write_assets() {
    fs::create_directories(L"CFG/robots");
    const fs::path source(MATRIXGAME_SOURCE_ROOT);
    fs::copy_file(source / L"MatrixGame/CFG/robots/data.txt", L"CFG/robots/data.txt");
    fs::copy_file(source / L"MatrixGame/CFG/robots/iface.txt", L"CFG/robots/iface.txt");
    CBlockPar data;
    data.LoadFromTextFile(L"CFG/robots/data.txt");
    data.BlockGet(L"Config")->ParSetAdd(L"SoftwareCursor", L"1");
    data.SaveInTextFile(L"CFG/robots/data.txt");
    CBitmap bitmap;
    bitmap.CreateRGBA(1024, 1024);
    std::memset(bitmap.Data(), 128, bitmap.Pitch() * bitmap.SizeY());
    bitmap.SaveInPNG(L"pixel.png");
    // Configured paths name synthetic replacement images; no original assets are read.
    std::set<std::string> paths;
    const std::regex pattern(R"(Matrix[\\/][A-Za-z0-9_./\\-]+)", std::regex::icase);
    const std::function<void(CBlockPar &)> collect = [&](CBlockPar &block) {
        for (int i = 0; i < block.ParCount(); ++i) {
            const std::string text = utils::from_wstring(block.ParGet(i));
            for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
                paths.insert(it->str());
        }
        for (int i = 0; i < block.BlockCount(); ++i) collect(*block.BlockGet(i));
    };
    collect(data);
    CBlockPar interface_config;
    interface_config.LoadFromTextFile(L"CFG/robots/iface.txt");
    collect(interface_config);
    const std::wstring extra = L"W," + std::to_wstring(RUK_WEAPON_BOMB);
    const tests::MeshAttachment attachments[] = {{1,L"root",0,0,20}, {2,L"left",-8,0,0}, {3,L"right",8,0,0},
        {20,L"W,1",-8,0,20}, {21,L"W,1",8,0,20}, {22,L"W,1",-8,8,20}, {23,L"W,1",8,8,20},
        {24,extra.c_str(),0,0,20}};
    for (const auto *texture : {TEXTURE_PATH_REFLECTION, TEXTURE_PATH_SPOT, TEXTURE_PATH_OBJSEL,
        TEXTURE_PATH_HIT, TEXTURE_PATH_POINTLIGHT, TEXTURE_PATH_VORONKA, TEXTURE_PATH_TURRET_RADIUS,
        TEXTURE_PATH_SOLE_TRACK, TEXTURE_PATH_SOLE_WHEEL, TEXTURE_PATH_SOLE_PNEUMATIC, TEXTURE_PATH_PB,
        TEXTURE_PATH_MOVETO, TEXTURE_PATH_DUST, TEXTURE_PATH_BIGBOOM, TEXTURE_PATH_GUN_FIRE,
        TEXTURE_PATH_GUN_BULLETS1, TEXTURE_PATH_GUN_BULLETS2, TEXTURE_PATH_SPLASH,
        TEXTURE_PATH_KEELWATER, TEXTURE_PATH_WAVE, TEXTURE_PATH_ZAHVAT})
        paths.insert(utils::from_wstring(texture));
    for (const auto &path : paths) {
        fs::path target(path);
        if (_wcsicmp(target.extension().c_str(), L".vo") == 0) {
            fs::create_directories(target.parent_path());
            tests::write_battle_mesh(target, attachments);
        }
        if (target.has_extension()) continue;
        target += L".png";
        fs::create_directories(target.parent_path());
        if (!fs::exists(target)) fs::copy_file(L"pixel.png", target);
    }
    fs::create_directories(L"Matrix/Robot");
    for (const auto &[prefix, count] : {std::pair{OBJECT_PATH_ROBOT_HEAD, ROBOT_HEAD_CNT},
        {OBJECT_PATH_ROBOT_CHASSIS, ROBOT_CHASSIS_CNT}, {OBJECT_PATH_ROBOT_WEAPON, ROBOT_WEAPON_CNT},
        {OBJECT_PATH_ROBOT_ARMOR, ROBOT_ARMOR_CNT}}) {
        for (int i = 1; i <= count; ++i) {
            tests::write_battle_mesh(std::wstring(prefix) + std::to_wstring(i) + L".vo", attachments,
                wcscmp(prefix, OBJECT_PATH_ROBOT_CHASSIS) == 0 && i == RUK_CHASSIS_PNEUMATIC, L"pixel");
            const fs::path texture = std::wstring(prefix) + std::to_wstring(i) + L".png";
            if (!fs::exists(texture)) fs::copy_file(L"pixel.png", texture);
        }
    }
    write_map();
}
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
    write_assets();
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
