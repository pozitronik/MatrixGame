// MatrixGame - licensed under GPLv2 or any later version.
#include "native_session_assets.hpp"
#include "native_demo_assets.hpp"
#include "native_window_containment.hpp"
#include "native_packed_config.hpp"
#include "test_support.hpp"
#include "MatrixGame.h"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixObjectCannon.hpp"
#include "Interface/CInterface.h"
#include "MatrixFormGame.hpp"
#include "input.hpp"
#include "CStorage.hpp"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <array>
#include <utility>
#include <cmath>
#include <string_view>

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
    HWND parent{}, child{};
    ATOM registered{};
    explicit Window(HWND owner) : parent(owner) {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"MatrixGame_wc";
        registered = RegisterClassW(&type);
        if (registered) child = CreateWindowExW(WS_EX_NOACTIVATE, type.lpszClassName, L"Synthetic battle", WS_CHILD,
            0, 0, 640, 480, parent, nullptr, type.hInstance, nullptr);
    }
    ~Window() {
        if (child) DestroyWindow(child);
        if (registered) UnregisterClassW(L"MatrixGame_wc", GetModuleHandleW(nullptr));
    }
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
    ~Fixture() {
        fs::current_path(previous);
        std::error_code error;
        fs::remove_all(root, error);
        if (error) std::cerr << "Cannot remove synthetic session directory " << root << ": " << error.message() << '\n';
    }
};
enum class DemoProfile { None, Escape, Win, Loss, Reinforcement, NoReinforcement, CameraFights };
struct DemoCoreRelease { void operator()(SObjectCore *core) const { core->Release(); } };
struct HostObservation {
    HWND window{};
    int progress{}, begins{}, ticks{}, texts{}, text_clears{};
    int phase{};
    float music{1};
    CMatrixRobotAI *robot{};
    float initial_x{};
    bool moved{}, map_observed{}, leave_key_held{}, building_restart{}, cannon_restart{};
    int restarts{};
    bool type_toggles{}, toggles_observed{};
    DemoProfile demo{DemoProfile::None};
    bool demo_exited{}, pending_outcome{};
    int demo_start_time{};
    int request_time{};
    uint64_t initial_draws{}, observed_draws{};
    std::array<std::unique_ptr<SObjectCore, DemoCoreRelease>, 3> camera_cores;
    int camera_phase{}, camera_phase_time{};
    DWORD initial_cheat_flags{};
    std::time_t entry_time{}, first_progress_time{};
    std::string failure;
} *host;
void native_diagnostic(const wchar_t *title, const wchar_t *message) {
    std::wcerr << title << L": " << message << L'\n';
    if (!host) TerminateProcess(GetCurrentProcess(), 98);
    if (host->failure.empty()) host->failure = "Unexpected native diagnostic dialog";
}
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
    if (host->progress == 0) {
        host->first_progress_time = std::time(nullptr);
        host->initial_cheat_flags = g_Flags & (GFLAG_4SPEED | GFLAG_KEEPALIVE);
    }
    ++host->progress;
    if (g_MatrixMap && g_MatrixMap->m_Size.x == 32) host->map_observed = true;
}
void __stdcall begin() { ++host->begins; }
float __stdcall music_get() { return host->music; }
void __stdcall music_set(float value) { host->music = value; }
std::pair<CMatrixBuilding *, CMatrixBuilding *> buildings() {
    CMatrixBuilding *base = nullptr, *factory = nullptr;
    int robots = 0;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (object->IsLiveRobot()) { ++robots; continue; }
        if (object->IsCannon()) { MG_CHECK(host->cannon_restart); continue; }
        MG_CHECK(object->IsLiveBuilding());
        auto *building = object->AsBuilding();
        if (building->IsBase()) {
            MG_CHECK(!base && building->GetSide() == PLAYER_SIDE);
            MG_CHECK(building->m_Pos.x == 180 && building->m_Pos.y == 480);
            base = building;
        } else {
            MG_CHECK(!factory && building->m_Kind == BUILDING_TITAN && building->GetSide() == 2);
            MG_CHECK(building->m_Pos.x == 400 && building->m_Pos.y == 100);
            factory = building;
        }
    }
    MG_CHECK(base && factory && robots == 2);
    int footprint = 0;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        auto *owner = g_MatrixMap->UnitGet(x, y)->m_Base;
        if (owner) { MG_CHECK(owner == base); ++footprint; }
    }
    MG_CHECK(footprint == 9);
    return {base, factory};
}
CMatrixCannon *stored_cannon(CMatrixBuilding &base) {
    CMatrixCannon *cannon = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsCannon()) { MG_CHECK(!cannon); cannon = object->AsCannon(); }
    MG_CHECK(cannon && cannon->IsLiveActiveCannon());
    MG_CHECK(cannon->m_ParentBuilding == &base && cannon->GetSide() == PLAYER_SIDE && cannon->m_Num == 1);
    MG_CHECK(cannon->m_Pos.x == 80 && cannon->m_Pos.y == 480 && cannon->m_Angle == 0.5f);
    MG_CHECK(cannon->m_Place == g_MatrixMap->m_RN.FindInPL(CPoint(8, 48)));
    MG_CHECK(cannon->m_Place >= 0 && cannon->m_CurrState == CANNON_IDLE);
    MG_CHECK(cannon->GetHitPoint() == cannon->GetMaxHitPoint());
    MG_CHECK(base.m_TurretsHave == 1 && base.m_TurretsPlacesCnt == 2 && base.m_TurretsMax == 2);
    MG_CHECK(base.m_TurretsPlaces[0].m_Coord == CPoint(8,48) && base.m_TurretsPlaces[0].m_CannonType == 1);
    MG_CHECK(base.m_TurretsPlaces[1].m_Coord == CPoint(8,40) && base.m_TurretsPlaces[1].m_CannonType == -1);
    MG_CHECK(base.m_BS.GetItemsCnt() == 0);
    return cannon;
}
CMatrixCannon *queue_cannon(CMatrixBuilding &base) {
    auto *side = g_MatrixMap->GetPlayerSide();
    side->Select(BUILDING, &base);
    for (int resource = 0; resource < MAX_RESOURCES; ++resource)
        side->AddResourceAmount(static_cast<ERes>(resource), 1000);
    const D3DXVECTOR3 target(80, 400, 10);
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
    g_MatrixMap->m_TraceStopPos = target;
    g_IFaceList->BeginBuildTurret(1);
    MG_CHECK(side->m_CannonForBuild.m_Cannon && side->m_CannonForBuild.m_ParentBuilding == &base);
    g_MatrixMap->m_MouseDir = target - g_MatrixMap->m_Camera.GetFrustumCenter();
    D3DXVec3Normalize(&g_MatrixMap->m_MouseDir, &g_MatrixMap->m_MouseDir);
    side->LogicTakt(16);
    MG_CHECK(side->m_CannonForBuild.m_CanBuildFlag);
    side->OnLButtonDown(CPoint(0,0));
    MG_CHECK(base.m_BS.GetItemsCnt() == 1 && base.m_BS.GetTopItem()->IsCannon());
    auto *cannon = base.m_BS.GetTopItem()->AsCannon();
    MG_CHECK(cannon->m_CurrState == CANNON_UNDER_CONSTRUCTION && cannon->m_ParentBuilding == &base);
    base.m_BS.TickTimer(g_Config.m_Timings[UNIT_TURRET] / 4);
    MG_CHECK(cannon->GetHitPoint() > 0 && cannon->GetHitPoint() < cannon->GetMaxHitPoint());
    return cannon;
}
void restart_buildings(CFormMatrixGame &form) {
    auto *map = g_MatrixMap;
    auto *terrain = map->m_Unit;
    const auto press = [&](int key) { form.Keyboard(true, key); form.Keyboard(false, key); };
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto [base, factory] = buildings();
        std::unique_ptr<SObjectCore, void(*)(SObjectCore *)> old_base(base->GetCore(DEBUG_CALL_INFO),
            [](auto *core) { core->Release(); });
        std::unique_ptr<SObjectCore, void(*)(SObjectCore *)> old_factory(factory->GetCore(DEBUG_CALL_INFO),
            [](auto *core) { core->Release(); });
        std::unique_ptr<SObjectCore, void(*)(SObjectCore *)> old_cannon(nullptr, [](auto *core) { core->Release(); });
        std::unique_ptr<SObjectCore, void(*)(SObjectCore *)> old_queued(nullptr, [](auto *core) { core->Release(); });
        if (host->cannon_restart) {
            auto *cannon = stored_cannon(*base);
            old_cannon.reset(cannon->GetCore(DEBUG_CALL_INFO));
            auto *queued = queue_cannon(*base);
            old_queued.reset(queued->GetCore(DEBUG_CALL_INFO));
            cannon->SetSide(2);
            cannon->SetHitPoint(cannon->GetMaxHitPoint() / 2);
        }
        base->Open();
        factory->SetNeutral();
        for (int side = 1; side <= 2; ++side) for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
            auto *owner = map->GetSideById(side);
            const auto kind = static_cast<ERes>(resource);
            const int saved = 100 * side + 1 + resource;
            const int changed = saved + 10 * side + resource + 1;
            owner->AddResourceAmount(kind, changed - owner->GetResourcesAmount(kind));
            MG_CHECK(owner->GetResourcesAmount(kind) == changed && changed != saved);
        }
        MG_CHECK(base->State() == BASE_OPENING && factory->GetSide() == 0);
        press(VK_ESCAPE); press('R');
        MG_CHECK(map->m_DialogModeHints.size() == 2);
        press(VK_RETURN);
        MG_CHECK(g_MatrixMap == map && map->m_Unit == terrain);
        MG_CHECK(old_base->m_Object == nullptr && old_factory->m_Object == nullptr);
        if (host->cannon_restart) MG_CHECK(old_cannon->m_Object == nullptr && old_queued->m_Object == nullptr);
        auto [fresh_base, fresh_factory] = buildings();
        if (host->cannon_restart) stored_cannon(*fresh_base);
        MG_CHECK(fresh_base->State() == BASE_CLOSED && fresh_factory->GetSide() == 2);
        MG_CHECK(map->IsPaused() && map->m_DialogModeName &&
                 wcscmp(map->m_DialogModeName, TEMPLATE_DIALOG_BEGIN) == 0);
        for (int side = 1; side <= 2; ++side) for (int resource = 0; resource < MAX_RESOURCES; ++resource)
            MG_CHECK(map->GetSideById(side)->GetResourcesAmount(static_cast<ERes>(resource)) == 100 * side + 1 + resource);
        ++host->restarts;
        press(VK_RETURN);
        MG_CHECK(!map->IsPaused() && !map->m_DialogModeName);
    }
}
void camera_fights_tick() {
    const int now = g_MatrixMap->GetTime();
    MG_CHECK(now - host->demo_start_time < 12000);
    const auto actor = [](int index) {
        MG_CHECK(index >= 0 && index < static_cast<int>(host->camera_cores.size()));
        const auto &core = host->camera_cores[index];
        MG_CHECK(core && core->m_Object && core->m_Object->IsLiveRobot());
        return core->m_Object->AsRobot();
    };
    const auto damage = [&] (int target) {
        auto *victim = actor(target);
        const auto before = random::draw_count();
        const float health = victim->GetHitPoint();
        MG_CHECK(!victim->Damage(WEAPON_LASER, victim->GetGeoCenter(), D3DXVECTOR3(1,0,0), PLAYER_SIDE, actor(0)));
        MG_CHECK(victim->GetHitPoint() < health && random::draw_count() > before);
        host->observed_draws += random::draw_count() - before;
    };
    const auto near_pair = [&](int target, float distance) {
        const auto midpoint = (actor(0)->GetGeoCenter() + actor(target)->GetGeoCenter()) * 0.5f;
        const auto delta = g_MatrixMap->m_Camera.GetLinkPoint() - midpoint;
        return D3DXVec3Length(&delta) < distance;
    };
    if (host->camera_phase == 0) {
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            MG_CHECK(object->IsLiveRobot());
            const int index = object->GetSide() == PLAYER_SIDE ? 0 : object->AsRobot()->m_PosX < 400 ? 1 : 2;
            MG_CHECK(!host->camera_cores[index]);
            host->camera_cores[index].reset(object->GetCore(DEBUG_CALL_INFO));
        }
        for (const auto &core : host->camera_cores) MG_CHECK(core && core->m_Ref == 2);
        MG_CHECK(g_Config.m_ItemChars[CHASSIS2_MOVE_SPEED] == 0);
        host->demo_start_time = now;
        host->camera_phase_time = now;
        host->phase = 1;
        ++host->camera_phase;
    } else if (host->camera_phase == 1) {
        // Component damage registers the pair through production Damage. The
        // native loop, camera selection/interpolation and ownership stay real.
        damage(1);
        MG_CHECK(host->camera_cores[0]->m_Ref == 3 && host->camera_cores[1]->m_Ref == 3);
        host->camera_phase_time = now;
        ++host->camera_phase;
    } else if (host->camera_phase == 2 && now - host->camera_phase_time >= 2000) {
        MG_CHECK(near_pair(1, 32));
        damage(2);
        MG_CHECK(host->camera_cores[0]->m_Ref == 4 && host->camera_cores[2]->m_Ref == 3);
        host->camera_phase_time = now;
        ++host->camera_phase;
    } else if (host->camera_phase == 3 && now - host->camera_phase_time >= 1200) {
        MG_CHECK(host->camera_cores[0]->m_Ref == 3 && host->camera_cores[1]->m_Ref == 3);
        MG_CHECK(host->camera_cores[2]->m_Ref == 2 && near_pair(1, 10));
        damage(2);
        g_MatrixMap->StaticDelete(actor(1));
        MG_CHECK(host->camera_cores[1]->m_Object == nullptr && host->camera_cores[1]->m_Ref == 2);
        host->camera_phase_time = now;
        ++host->camera_phase;
    } else if (host->camera_phase == 4 && now - host->camera_phase_time >= 3000 && near_pair(2, 2)) {
        MG_CHECK(host->camera_cores[1]->m_Ref == 1);
        MG_CHECK(host->camera_cores[0]->m_Ref == 3 && host->camera_cores[2]->m_Ref == 3);
        MG_CHECK(near_pair(2, 2));
        // Host observation references must not outlive the game's owning heap.
        // Camera-owned references remain for production teardown to release.
        for (auto &core : host->camera_cores) core.reset();
        auto *form = static_cast<CFormMatrixGame *>(g_FormCur);
        form->Keyboard(true, VK_ESCAPE); form->Keyboard(false, VK_ESCAPE);
        MG_CHECK(g_ExitState == 1 && FLAG(g_Flags, GFLAG_EXITLOOP) && !Input::isVKeyPressed(VK_ESCAPE));
        host->demo_exited = true;
        host->phase = 2;
        ++host->camera_phase;
    }
}
void __stdcall tick() {
    ++host->ticks;
    try {
        MG_CHECK(host->ticks < 5000);
        MG_CHECK(g_FormCur && g_MatrixMap && !IsWindowVisible(host->window));
        if (host->demo != DemoProfile::None) {
            MG_CHECK(random::mode() == random::Mode::ParkMiller);
            const DWORD flags = MMFLAG_AUTOMATIC_MODE | MMFLAG_FLYCAM | MMFLAG_FULLAUTO;
            MG_CHECK((g_MatrixMap->m_Flags & flags) == flags);
            MG_CHECK(!g_MatrixMap->m_DialogModeName);
            const auto &link = g_MatrixMap->m_Camera.GetLinkPoint();
            MG_CHECK(std::isfinite(link.x) && std::isfinite(link.y) && std::isfinite(link.z));
            MG_CHECK(std::isfinite(g_MatrixMap->m_Camera.GetAngleX()) && std::isfinite(g_MatrixMap->m_Camera.GetAngleZ()));
            if (host->demo == DemoProfile::CameraFights) { camera_fights_tick(); return; }
            if (host->demo == DemoProfile::Reinforcement || host->demo == DemoProfile::NoReinforcement) {
                const bool eligible = host->demo == DemoProfile::Reinforcement;
                if (host->phase == 0) {
                    MG_CHECK(g_Config.m_MaintenanceTime == 20000);
                    MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 200);
                    MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0);
                    host->initial_draws = random::draw_count();
                    host->demo_start_time = g_MatrixMap->GetTime();
                    ++host->phase;
                }
                CMatrixBuilding *factory = nullptr;
                for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
                    if (object->IsLiveBuilding()) { MG_CHECK(!factory); factory = object->AsBuilding(); }
                MG_CHECK(factory && factory->m_Kind == BUILDING_TITAN);
                MG_CHECK(factory->GetSide() == (eligible ? PLAYER_SIDE : 2));
                const int elapsed = g_MatrixMap->GetTime() - host->demo_start_time;
                MG_CHECK(elapsed < 10000);
                if (g_MatrixMap->m_AD_Obj_cnt) {
                    MG_CHECK(eligible && g_MatrixMap->m_AD_Obj_cnt == 1);
                    auto *requested = g_MatrixMap->m_AD_Obj[0];
                    MG_CHECK(requested && requested->IsFlyer());
                    auto *carrier = static_cast<CMatrixFlyer *>(requested);
                    MG_CHECK(carrier->CarryingRobot());
                    MG_CHECK(carrier->GetCarryingRobot() && carrier->GetCarryingRobot()->IsLiveRobot());
                    auto *cargo = carrier->GetCarryingRobot()->AsRobot();
                    MG_CHECK(cargo->GetSide() == PLAYER_SIDE && cargo->m_CurrState == ROBOT_CARRYING);
                    MG_CHECK(cargo->GetHitPoint() == 150);
                    MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() > 0 && g_MatrixMap->BeforeMaintenanceTime() <= 200);
                    host->request_time = elapsed;
                }
                if (host->request_time || (!eligible && elapsed >= 1500)) {
                    host->observed_draws = random::draw_count() - host->initial_draws;
                    MG_CHECK(host->observed_draws > 0);
                    if (!eligible) MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 0);
                    CMatrixRobotAI *player = nullptr;
                    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
                        if (object->IsLiveRobot() && object->GetSide() == PLAYER_SIDE &&
                            object->AsRobot()->m_CurrState != ROBOT_CARRYING) {
                            MG_CHECK(!player); player = object->AsRobot();
                        }
                    MG_CHECK(player && player->GetGroupLogic() >= 0 && player->GetGroupLogic() < MAX_LOGIC_GROUP);
                    const auto &logic = g_MatrixMap->GetPlayerSide()->m_LogicGroup[player->GetGroupLogic()];
                    std::cout << "demo_ai_action=" << logic.m_Action.m_Type << " robots=" << logic.RobotsCnt()
                        << " request_time=" << host->request_time << '\n';
                    MG_CHECK(logic.m_Action.m_Type == (eligible ? mlat_Defence : mlat_Capture));
                    auto *form = static_cast<CFormMatrixGame *>(g_FormCur);
                    form->Keyboard(true, VK_ESCAPE); form->Keyboard(false, VK_ESCAPE);
                    MG_CHECK(g_ExitState == 1 && FLAG(g_Flags, GFLAG_EXITLOOP));
                    MG_CHECK(!Input::isVKeyPressed(VK_ESCAPE));
                    host->demo_exited = true;
                    host->phase = 2;
                }
                return;
            }
            if (host->phase == 0) {
                MG_CHECK(random::mode() == random::Mode::ParkMiller);
                host->demo_start_time = g_MatrixMap->GetTime();
                ++host->phase;
            } else if (host->phase == 1 && g_MatrixMap->GetTime() >= host->demo_start_time + 250 &&
                       host->demo == DemoProfile::Escape) {
                auto *form = static_cast<CFormMatrixGame *>(g_FormCur);
                form->Keyboard(true, VK_ESCAPE); form->Keyboard(false, VK_ESCAPE);
                MG_CHECK(g_ExitState == 1 && FLAG(g_Flags, GFLAG_EXITLOOP));
                MG_CHECK(!Input::isVKeyPressed(VK_ESCAPE));
                host->demo_exited = true;
                ++host->phase;
            } else if (host->phase == 1 && g_MatrixMap->GetTime() >= host->demo_start_time + 250) {
                const int eliminated = host->demo == DemoProfile::Loss ? PLAYER_SIDE : 2;
                CMatrixRobotAI *victim = nullptr;
                for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
                    if (object->IsLiveRobot() && object->GetSide() == eliminated) {
                        MG_CHECK(victim == nullptr);
                        victim = object->AsRobot();
                    }
                MG_CHECK(victim);
                MG_CHECK(victim->Damage(WEAPON_INSTANT_DEATH, victim->GetGeoCenter(), D3DXVECTOR3(0,0,1), 0, nullptr));
                MG_CHECK(!victim->IsLiveRobot());
                ++host->phase;
            } else if (host->phase == 2 &&
                       (host->demo == DemoProfile::Win ? FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) :
                        g_MatrixMap->GetPlayerSide()->GetStatus() == SS_NONE)) {
                MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_WIN) == (host->demo == DemoProfile::Win));
                MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP));
                host->pending_outcome = true;
            }
            return;
        }
        if (host->phase == 0) {
            MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_AUTOMATIC_MODE | MMFLAG_FLYCAM | MMFLAG_FULLAUTO));
            MG_CHECK(!Input::isVKeyPressed(VK_F12));
            MG_CHECK(random::mode() == random::Mode::ParkMiller);
            MG_CHECK(g_MatrixMap->m_DialogModeName && wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_BEGIN) == 0);
            auto *form = static_cast<CFormMatrixGame *>(g_FormCur);
            form->Keyboard(true, VK_RETURN); form->Keyboard(false, VK_RETURN);
            MG_CHECK(!g_MatrixMap->m_DialogModeName && !Input::isVKeyPressed(VK_RETURN));
            MG_CHECK(!FLAG(g_Flags, GFLAG_4SPEED | GFLAG_KEEPALIVE));
            if (host->type_toggles) {
                for (const std::string_view word : {"NEED4SPEED", "KEEPALIVE"})
                    for (const char key : word) {
                        form->Keyboard(true, key); form->Keyboard(false, key);
                        MG_CHECK(!Input::isVKeyPressed(key));
                    }
#if MATRIXGAME_SCENARIO_CHEATS
                MG_CHECK((g_Flags & (GFLAG_4SPEED | GFLAG_KEEPALIVE)) == (GFLAG_4SPEED | GFLAG_KEEPALIVE));
#else
                MG_CHECK(!FLAG(g_Flags, GFLAG_4SPEED | GFLAG_KEEPALIVE));
#endif
                host->toggles_observed = true;
            }
            if (host->building_restart) restart_buildings(*form);
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
    } catch (const std::exception &error) {
        host->failure = error.what();
        for (auto &core : host->camera_cores) core.reset();
        throw;
    } catch (...) {
        host->failure = "Unexpected native host callback exception";
        for (auto &core : host->camera_cores) core.reset();
        throw;
    }
}
enum class ConfigurationProfile { Text, Packed, InvalidPackedHeader, RepeatedPneumaticCleanup };
void demo_assets() {
    for (auto demo : {DemoProfile::Reinforcement, DemoProfile::NoReinforcement, DemoProfile::CameraFights})
        for (bool packed : {false, true}) {
            Fixture fixture;
            tests::write_native_session_assets();
            const bool camera = demo == DemoProfile::CameraFights;
            if (camera) tests::write_native_demo_camera(L"synthetic-DeMo.map");
            else tests::write_native_demo_reinforcement(L"synthetic-DeMo.map", demo == DemoProfile::Reinforcement);
            CStorage map;
            MG_CHECK(map.Load(L"synthetic-DeMo.map"));
            const auto actors = camera ? 3u : 2u;
            for (const auto &[name, type] : {
                std::pair{DATA_ROBOTS_X, ST_FLOAT}, {DATA_ROBOTS_Y, ST_FLOAT},
                {DATA_ROBOTS_SIDE, ST_BYTE}, {DATA_ROBOTS_SHADOW, ST_BYTE},
                {DATA_ROBOTS_SHADOWSIZE, ST_INT32}, {DATA_ROBOTS_GROUP, ST_INT32}}) {
                auto *column = map.GetBuf(DATA_ROBOTS, name, type);
                MG_CHECK(column && column->GetArraysCount() == 1 && column->GetArrayLength(0) == actors);
            }
            auto *units = map.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, ST_WCHAR);
            MG_CHECK(units && units->GetArraysCount() == actors);
            for (DWORD i = 0; i < actors; ++i) MG_CHECK(units->GetAsWStr(i) == units->GetAsWStr(0));
            if (!camera) {
                for (const auto &[name, type] : {
                    std::pair{DATA_BUILDINGS_X, ST_FLOAT}, {DATA_BUILDINGS_Y, ST_FLOAT},
                    {DATA_BUILDINGS_SIDE, ST_BYTE}, {DATA_BUILDINGS_KIND, ST_BYTE},
                    {DATA_BUILDINGS_SHADOW, ST_BYTE}, {DATA_BUILDINGS_ANGLE, ST_BYTE},
                    {DATA_BUILDINGS_SHADOWSIZE, ST_INT32}}) {
                    auto *column = map.GetBuf(DATA_BUILDINGS, name, type);
                    MG_CHECK(column && column->GetArraysCount() == 1 && column->GetArrayLength(0) == 1);
                }
                auto *sides = map.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_SIDE, ST_BYTE);
                MG_CHECK(sides->GetFirst<BYTE>(0)[0] == (demo == DemoProfile::Reinforcement ? PLAYER_SIDE : 2));
                MG_CHECK(fs::is_regular_file(L"synthetic-transport-body.vo"));
            }
            CBlockPar data;
            if (packed) {
                tests::pack_native_session_configuration();
                MG_CHECK(!fs::exists(L"CFG/robots/data.txt") && !fs::exists(L"CFG/robots/iface.txt"));
                CStorage archive;
                MG_CHECK(archive.Load(L"CFG/robots.dat"));
                archive.RestoreBlockPar(L"da", data);
            } else data.LoadFromTextFile(L"CFG/robots/data.txt");
            if (camera) {
                MG_CHECK(data.BlockGet(PAR_SOURCE_CHARS)->BlockGet(L"Chassis")->ParGet(PAR_SOURCE_CHASSIS2_MOVE_SPEED) == L"0");
                MG_CHECK(data.BlockGet(PAR_SOURCE_WEAPONS)->BlockGet(PAR_SOURCE_DAMAGES)->BlockGet(PAR_SOURCE_DAMAGES_ROBOT)
                    ->ParGet(L"WEAPON_LASER") == L"1,0,0");
            } else {
                MG_CHECK(data.BlockGet(PAR_SOURCE_TIMINGS)->ParGet(PAR_SOURCE_TIMINGS_MAINTENANCE) == L"20000");
                auto *body = data.BlockGet(L"Models")->BlockGet(L"Flyers")->BlockGet(L"1")->BlockGet(L"Body");
                MG_CHECK(body->ParGet(L"Model") == L"synthetic-transport-body.vo" && body->ParGet(L"Texture") == L"pixel");
                auto *order = data.BlockGet(PAR_SOURCE_FLYER_ORDERS)->BlockGet(PAR_SOURCE_FLYER_ORDERS_GIVE_BOT);
                MG_CHECK(order->ParGet(L"Flyer") == L"1" && order->ParGet(L"Hitpoint") == L"1000");
                auto *bot = order->BlockGet(L"100");
                MG_CHECK(bot->ParGet(L"BotChassis") == L"2" && bot->ParGet(L"BotArmor") == L"1");
                MG_CHECK(bot->ParGet(L"BotHead") == L"0" && bot->ParGet(L"BotStrength") == L"1" &&
                         bot->ParGet(L"BotHitpoint") == L"1500");
            }
        }
}
struct SessionOptions {
    bool leave_key_held{};
    ConfigurationProfile profile{ConfigurationProfile::Text};
    bool type_toggles{};
    DemoProfile demo{DemoProfile::None};
    bool building_restart{};
    bool cannon_restart{};
};
void native_run_lifetimes_impl(const SessionOptions &options) {
    MG_CHECK(!options.cannon_restart || options.building_restart);
    MG_CHECK(!options.building_restart || (!options.type_toggles && options.demo == DemoProfile::None));
    MG_CHECK(!options.type_toggles || options.demo == DemoProfile::None);
    MG_CHECK(!options.leave_key_held || (!options.type_toggles && options.demo == DemoProfile::None &&
        !options.building_restart && options.profile == ConfigurationProfile::Text));
    MG_CHECK((!options.type_toggles && options.demo == DemoProfile::None && !options.building_restart) ||
        options.profile == ConfigurationProfile::Text || options.profile == ConfigurationProfile::InvalidPackedHeader);
    const bool leave_key_held = options.leave_key_held;
    const auto profile = options.profile;
    const bool type_toggles = options.type_toggles;
    const auto demo = options.demo;
    const bool building_restart = options.building_restart;
    const bool cannon_restart = options.cannon_restart;
    const bool packed = profile != ConfigurationProfile::Text;
    Fixture fixture;
    tests::write_native_session_assets();
    if (building_restart) {
        tests::write_native_session_building_assets();
        if (cannon_restart) tests::write_native_session_cannon_assets();
        tests::write_native_session_map(L"synthetic.map", true, cannon_restart);
    }
    const bool ai_demo = demo >= DemoProfile::Reinforcement;
    // Every ordinary reentry restores the original declaration, including the
    // camera profile's stationary chassis and reinforcement-specific timings.
    if (ai_demo) fs::copy_file(L"CFG/robots/data.txt", L"data.normal");
    if (demo == DemoProfile::Reinforcement || demo == DemoProfile::NoReinforcement)
        tests::write_native_demo_reinforcement(L"synthetic-DeMo.map", demo == DemoProfile::Reinforcement);
    else if (demo == DemoProfile::CameraFights) tests::write_native_demo_camera(L"synthetic-DeMo.map");
    else if (demo != DemoProfile::None) fs::copy_file(L"synthetic.map", L"synthetic-DeMo.map");
    if (packed) tests::pack_native_session_configuration();
    tests::NativeWindowContainment containment(native_diagnostic, false);
    Window window(containment.parent());
    MG_CHECK(window.registered && window.parent && window.child && !IsWindowVisible(window.child));
    MG_CHECK(GetParent(window.child) == containment.parent());
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
        if (lifetime == 1 && (profile == ConfigurationProfile::Packed ||
            profile == ConfigurationProfile::RepeatedPneumaticCleanup)) continue;
        const bool failing = lifetime == 1;
        HostObservation observed;
        // Diagnostics can also arrive while the host/device guards retire.
        // Clear this alias before its stack observation is destroyed.
        struct ObservationScope {
            ~ObservationScope() { host = nullptr; }
        } observation_scope;
        observed.window = window.child;
        observed.leave_key_held = leave_key_held;
        observed.building_restart = building_restart;
        observed.cannon_restart = cannon_restart;
        observed.type_toggles = type_toggles && lifetime == 0;
        observed.demo = lifetime == 0 ? demo : DemoProfile::None;
        host = &observed;
        if (failing) {
            if (packed) {
                fs::rename(L"CFG/robots.dat", L"parked-valid-config");
                std::ofstream broken(L"CFG/robots.dat", std::ios::binary);
                broken.write("BAD!", 4);
                MG_CHECK(broken.good());
            } else fs::rename(L"CFG/robots/iface.txt", L"CFG/robots/iface.saved");
        }
        SRobotGameState result{41,42,43,44,45,46};
        const auto unchanged = result;
        std::wstring map = lifetime == 0 && demo != DemoProfile::None ? L".\\synthetic-DeMo.map" : L".\\synthetic.map";
        observed.entry_time = std::time(nullptr);
        const int exit = entry->m_Run(GetModuleHandleW(nullptr), window.child, map.data(), &settings,
            nullptr, nullptr, nullptr, nullptr, nullptr, &result);
        SetUnhandledExceptionFilter(nullptr);
        std::set_terminate([] { std::abort(); });
        const int expected_exit = failing ? 100 : observed.demo != DemoProfile::None ? 1 : 0;
        if (exit != expected_exit) {
            std::ifstream errors("matrixgame-dll-errors.log", std::ios::binary);
            const std::string diagnostic((std::istreambuf_iterator<char>(errors)), {});
            std::cerr << "Unexpected Run result " << exit << ": "
                << (diagnostic.empty() ? "No DLL failure diagnostic recorded" : diagnostic) << '\n';
        }
        if (!failing && profile == ConfigurationProfile::RepeatedPneumaticCleanup)
            CMatrixRobot::DestroyPneumaticData();
        if (failing) {
            if (packed) {
                fs::remove(L"CFG/robots.dat");
                fs::rename(L"parked-valid-config", L"CFG/robots.dat");
            } else fs::rename(L"CFG/robots/iface.saved", L"CFG/robots/iface.txt");
        }
        if (lifetime == 0 && ai_demo) {
            if (packed) {
                fs::remove(L"CFG/robots.dat");
                fs::rename(L"parked-text-config", L"CFG/robots");
            }
            fs::rename(L"CFG/robots/data.txt", L"data.demo");
            fs::rename(L"data.normal", L"CFG/robots/data.txt");
            if (packed) tests::pack_native_session_configuration();
        }
        std::cout << "fixture=synthetic-session-v1 packed=" << packed << " lifetime=" << lifetime << " result=" << exit
            << " seed_interval=[" << observed.entry_time << ',' << observed.first_progress_time
            << "] ticks=" << observed.ticks << " moved=" << observed.moved
            << " request_time=" << observed.request_time << " observed_draws=" << observed.observed_draws
            << " buildings=" << building_restart << " cannons=" << cannon_restart
            << " restarts=" << observed.restarts
            << " demo=" << static_cast<int>(observed.demo) << " toggles=" << observed.toggles_observed << '\n';
        if (!observed.failure.empty()) throw std::runtime_error(observed.failure);
        MG_CHECK(observed.restarts == (building_restart && !failing ? 2 : 0));
        MG_CHECK(exit == expected_exit);
        if (failing && packed) MG_CHECK(observed.progress == 0 && !observed.map_observed && observed.ticks == 0);
        else MG_CHECK(observed.progress > 0 && observed.map_observed);
        MG_CHECK(observed.initial_cheat_flags == 0);
        MG_CHECK(observed.toggles_observed == observed.type_toggles);
        MG_CHECK(observed.begins == (failing ? 0 : 1));
        if (failing) {
            MG_CHECK(std::memcmp(&result, &unchanged, sizeof(result)) == 0);
            std::ifstream errors("matrixgame-dll-errors.log", std::ios::binary);
            const std::string diagnostic((std::istreambuf_iterator<char>(errors)), {});
            MG_CHECK(diagnostic.find(packed ? "Invalid packed configuration: cfg\\robots.dat" :
                "Error open file: cfg\\robots\\iface.txt") != std::string::npos);
        }
        else if (observed.demo != DemoProfile::None) {
            MG_CHECK(observed.ticks >= 2 && observed.phase == 2);
            MG_CHECK(observed.demo == DemoProfile::Win || observed.demo == DemoProfile::Loss ?
                observed.pending_outcome : observed.demo_exited);
            if (!ai_demo) MG_CHECK(observed.texts > 0);
            MG_CHECK(observed.texts == observed.text_clears);
        } else {
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
        MG_CHECK(SUCCEEDED(native.device->Present(nullptr, nullptr, nullptr, nullptr)));
        if (leave_key_held) MG_CHECK(!Input::isVKeyPressed(VK_F12));
        for (const auto &core : observed.camera_cores) MG_CHECK(!core);
#ifdef MEM_SPY_ENABLE
        MG_CHECK(Base::SMemHeader::first_mem_block == nullptr);
#endif
    }
}
void native_demo_reinforcement() { native_run_lifetimes_impl({.demo = DemoProfile::Reinforcement}); }
void native_demo_packed_reinforcement() {
    native_run_lifetimes_impl({.profile = ConfigurationProfile::InvalidPackedHeader, .demo = DemoProfile::Reinforcement});
}
void native_demo_no_reinforcement() { native_run_lifetimes_impl({.demo = DemoProfile::NoReinforcement}); }
void native_demo_packed_no_reinforcement() {
    native_run_lifetimes_impl({.profile = ConfigurationProfile::InvalidPackedHeader, .demo = DemoProfile::NoReinforcement});
}
void native_demo_camera_fights() { native_run_lifetimes_impl({.demo = DemoProfile::CameraFights}); }
void native_demo_packed_camera_fights() {
    native_run_lifetimes_impl({.profile = ConfigurationProfile::InvalidPackedHeader, .demo = DemoProfile::CameraFights});
}
void native_run_lifetimes() { native_run_lifetimes_impl({}); }
void native_packed_lifetimes() { native_run_lifetimes_impl({.profile = ConfigurationProfile::Packed}); }
void native_packed_header_failure() { native_run_lifetimes_impl({.profile = ConfigurationProfile::InvalidPackedHeader}); }
void pneumatic_retirement() { native_run_lifetimes_impl({.profile = ConfigurationProfile::RepeatedPneumaticCleanup}); }
void native_held_input_retirement() { native_run_lifetimes_impl({.leave_key_held = true}); }
void native_building_restart() { native_run_lifetimes_impl({.building_restart = true}); }
void native_cannon_restart() { native_run_lifetimes_impl({.building_restart = true, .cannon_restart = true}); }
void native_packed_building_restart() {
    native_run_lifetimes_impl({.profile = ConfigurationProfile::InvalidPackedHeader, .building_restart = true});
}
void native_packed_cannon_restart() {
    native_run_lifetimes_impl({.profile = ConfigurationProfile::InvalidPackedHeader,
        .building_restart = true, .cannon_restart = true});
}
void native_cheat_lifetimes() { native_run_lifetimes_impl({.type_toggles = true}); }
void native_demo_lifetimes() { native_run_lifetimes_impl({.demo = DemoProfile::Escape}); }
void native_demo_win() { native_run_lifetimes_impl({.demo = DemoProfile::Win}); }
void native_demo_loss() { native_run_lifetimes_impl({.demo = DemoProfile::Loss}); }
void native_cheat_packed_lifetimes() {
    native_run_lifetimes_impl({.profile = ConfigurationProfile::InvalidPackedHeader, .type_toggles = true});
}
void native_demo_packed_lifetimes() {
    native_run_lifetimes_impl({.profile = ConfigurationProfile::InvalidPackedHeader, .demo = DemoProfile::Escape});
}
void native_demo_packed_win() {
    native_run_lifetimes_impl({.profile = ConfigurationProfile::InvalidPackedHeader, .demo = DemoProfile::Win});
}
void native_demo_packed_loss() {
    native_run_lifetimes_impl({.profile = ConfigurationProfile::InvalidPackedHeader, .demo = DemoProfile::Loss});
}
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const tests::Case cases[] = {
        {"game.session.demo_assets", demo_assets},
        {"game.session.native_demo_reinforcement", native_demo_reinforcement},
        {"game.session.native_demo_no_reinforcement", native_demo_no_reinforcement},
        {"game.session.native_demo_camera_fights", native_demo_camera_fights},
        {"game.session.native_demo_packed_reinforcement", native_demo_packed_reinforcement},
        {"game.session.native_demo_packed_no_reinforcement", native_demo_packed_no_reinforcement},
        {"game.session.native_demo_packed_camera_fights", native_demo_packed_camera_fights},
        {"game.session.native_run_lifetimes", native_run_lifetimes},
        {"game.session.native_building_restart", native_building_restart},
        {"game.session.native_cannon_restart", native_cannon_restart},
        {"game.session.native_packed_building_restart", native_packed_building_restart},
        {"game.session.native_packed_cannon_restart", native_packed_cannon_restart},
        {"game.session.native_cheat_lifetimes", native_cheat_lifetimes},
        {"game.session.native_demo_lifetimes", native_demo_lifetimes},
        {"game.session.native_demo_win", native_demo_win},
        {"game.session.native_demo_loss", native_demo_loss},
        {"game.session.native_cheat_packed_lifetimes", native_cheat_packed_lifetimes},
        {"game.session.native_demo_packed_lifetimes", native_demo_packed_lifetimes},
        {"game.session.native_demo_packed_win", native_demo_packed_win},
        {"game.session.native_demo_packed_loss", native_demo_packed_loss},
        {"game.session.native_packed_lifetimes", native_packed_lifetimes},
        {"game.session.native_packed_header_failure", native_packed_header_failure},
        {"game.session.pneumatic_retirement", pneumatic_retirement},
        {"game.session.held_input_retirement", native_held_input_retirement}};
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) return EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
