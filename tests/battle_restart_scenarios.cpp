// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_geometry.hpp"
#include "battle_mesh.hpp"
#include "battle_material.hpp"
#include "battle_building.hpp"
#include "command_world.hpp"
#include "test_support.hpp"
#include "CStorage.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixMultiSelection.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceButton.h"
#include "Interface/MatrixHint.hpp"
#include "input.hpp"
#include "loop_script.hpp"
#include <cstring>
#include <cmath>
#include <bit>
#include <float.h>
#include <filesystem>
#include <sstream>
#include <vector>

// Supply visibility, as the existing selection fixtures do; geometry picking
// and form dispatch remain production implementations.
struct SelectionFixture {
    explicit SelectionFixture(CMatrixMapStatic *visible) {
        MG_CHECK(CMultiSelection::m_GameSelection == nullptr);
        CMatrixMapStatic::objects_left = 0;
        CMatrixMapStatic::objects_rite = 1;
        CMatrixMapStatic::objects[0] = visible;
        g_IFaceList->m_InFocus = UNKNOWN;
    }
    ~SelectionFixture() {
        while (CMultiSelection::m_First) {
            auto *selection = CMultiSelection::m_First;
            HDelete(CMultiSelection, selection, g_MatrixHeap);
        }
        CMatrixMapStatic::objects[0] = nullptr;
        CMatrixMapStatic::objects_left = CMatrixMapStatic::objects_rite = 0;
    }
};

namespace {
using namespace tests::battle;
struct RestartMapObservation : CMatrixMap {
    static int result_countdown(const CMatrixMap &map) { return map.* &RestartMapObservation::m_BeforeWinLooseDialogCount; }
};
void press(CFormMatrixGame &form, uint8_t key) {
    form.Keyboard(true, key);
    form.Keyboard(false, key);
}
void schema(CStorage &storage, const wchar_t *table,
            std::initializer_list<std::pair<const wchar_t *, EStorageType>> columns) {
    CStorageRecord record(table);
    for (const auto &[name, type] : columns) record.AddItem(CStorageRecordItem(name, type));
    storage.AddRecord(record);
    for (const auto &[name, type] : columns)
        if (type != ST_WCHAR) storage.GetBuf(table, name, type)->AddArray();
}
template<class T> void values(CStorage &storage, const wchar_t *table, const wchar_t *column,
                             EStorageType type, std::initializer_list<T> values) {
    storage.GetBuf(table, column, type)->AddToArray<T>(0, values.begin(), static_cast<int>(values.size()));
}

// Only dynamics are serialized: Restart retains the already loaded terrain.
struct ReloadScene {
    std::filesystem::path path = std::filesystem::current_path() /
        ("restart-scene-" + std::to_string(GetCurrentProcessId()) + ".map");
    tests::BattleMaterialProfile material;
    CBitmap bitmap;
    int previous_exit = g_ExitState;

    ReloadScene() {
        MG_CHECK(!std::filesystem::exists(path));
        g_MatrixMap->m_Ids[g_MatrixMap->m_IdsCnt - 1] = path.wstring();
        g_Config.m_RobotShadow = SHADOW_OFF;
        g_Config.m_DrawAllObjectsToMinimap = 0;
        const tests::MeshAttachment attachment{1, L"armor", 0, 0, 20};
        tests::install_battle_texture(L"Matrix\\Robot\\Chassis2");
        tests::load_battle_material_mesh(std::span(&attachment, 1), L"Matrix\\Robot\\Chassis2.vo", L"Matrix\\Robot\\Chassis2");
        g_MatrixData->BlockGetAdd(IF_LABELS_BLOCKPAR)->BlockGetAdd(L"RobotNames")
            ->ParSetAdd(L"Chas2Key", L"Synthetic wheel");
        g_ExitState = 0;

        auto *hints = HNew(g_MatrixHeap) CInterface;
        g_IFaceList->m_Hints = hints;
        g_IFaceList->m_First = g_IFaceList->m_Last = hints;
        bitmap.CreateRGBA(2, 2);
        std::memset(bitmap.Data(), 255, bitmap.Pitch() * bitmap.SizeY());
        CMatrixHint::SetRuntimeBitmap(L"restart-pixel", &bitmap);
        g_MatrixData->BlockGetAdd(PAR_REPLACE);
        for (const auto *name : {IF_HINTS_OK, IF_HINTS_CANCEL, IF_HINTS_CANCEL_MENU,
             IF_HINTS_CONTINUE, IF_HINTS_SURRENDER, IF_HINTS_RESET, IF_HINTS_EXIT, IF_HINTS_HELP}) {
            auto *button = HNew(g_MatrixHeap) CIFaceButton;
            button->m_Type = IFACE_PUSH_BUTTON;
            button->m_strName = name;
            MG_CHECK(hints->AddElement(button));
        }
        for (const auto *name : {TEMPLATE_DIALOG_MENU, TEMPLATE_DIALOG_RESET, TEMPLATE_DIALOG_BEGIN,
                                TEMPLATE_DIALOG_WIN, TEMPLATE_DIALOG_LOOSE}) {
            std::wstring markup = L"-1|_BITMAP:restart-pixel:";
            for (int i = 0; i < 6; ++i) markup += L"|_MOD:COPY|_BITMAP:restart-pixel:";
            g_MatrixData->BlockGetAdd(PAR_TEMPLATES)->ParSetAdd(name, markup);
        }
        write();
    }
    void write() {
        CStorage storage;
        schema(storage, DATA_PROPERTIES, {{DATA_PROPERTIES_NAME, ST_WCHAR}, {DATA_PROPERTIES_VALUE, ST_WCHAR}});
        storage.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, ST_WCHAR)->AddWStr(DATA_SIDERESINFO);
        storage.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, ST_WCHAR)->AddWStr(L"1,101,102,103,104|2,201,202,203,204");
        schema(storage, DATA_OBJECTS, {{DATA_OBJECTS_X, ST_FLOAT}, {DATA_OBJECTS_Y, ST_FLOAT},
            {DATA_OBJECTS_ANGLE_Z, ST_FLOAT}, {DATA_OBJECTS_Z, ST_FLOAT}, {DATA_OBJECTS_SCALE, ST_FLOAT}, {DATA_OBJECTS_TYPE, ST_DWORD}});
        schema(storage, DATA_BUILDINGS, {{DATA_BUILDINGS_X, ST_FLOAT}, {DATA_BUILDINGS_Y, ST_FLOAT},
            {DATA_BUILDINGS_SIDE, ST_BYTE}, {DATA_BUILDINGS_KIND, ST_BYTE}, {DATA_BUILDINGS_SHADOW, ST_BYTE},
            {DATA_BUILDINGS_SHADOWSIZE, ST_INT32}, {DATA_BUILDINGS_ANGLE, ST_BYTE}});
        schema(storage, DATA_CANNONS, {{DATA_CANNONS_X, ST_FLOAT}, {DATA_CANNONS_Y, ST_FLOAT},
            {DATA_CANNONS_SIDE, ST_BYTE}, {DATA_CANNONS_KIND, ST_BYTE}, {DATA_CANNONS_SHADOW, ST_BYTE},
            {DATA_CANNONS_SHADOWSIZE, ST_INT32}, {DATA_CANNONS_ANGLE, ST_FLOAT}});
        schema(storage, DATA_ROBOTS, {{DATA_ROBOTS_X, ST_FLOAT}, {DATA_ROBOTS_Y, ST_FLOAT},
            {DATA_ROBOTS_SIDE, ST_BYTE}, {DATA_ROBOTS_SHADOW, ST_BYTE}, {DATA_ROBOTS_SHADOWSIZE, ST_INT32},
            {DATA_ROBOTS_GROUP, ST_INT32}, {DATA_ROBOTS_UNITS, ST_WCHAR}});
        values<float>(storage, DATA_ROBOTS, DATA_ROBOTS_X, ST_FLOAT, {100, 500});
        values<float>(storage, DATA_ROBOTS, DATA_ROBOTS_Y, ST_FLOAT, {200, 500});
        values<BYTE>(storage, DATA_ROBOTS, DATA_ROBOTS_SIDE, ST_BYTE, {1, 2});
        values<BYTE>(storage, DATA_ROBOTS, DATA_ROBOTS_SHADOW, ST_BYTE, {0, 0});
        values<INT32>(storage, DATA_ROBOTS, DATA_ROBOTS_SHADOWSIZE, ST_INT32, {0, 0});
        values<INT32>(storage, DATA_ROBOTS, DATA_ROBOTS_GROUP, ST_INT32, {0, 1});
        const auto units = std::to_wstring(MRT_CHASSIS) + L"," + std::to_wstring(RUK_CHASSIS_WHEEL) + L",0";
        storage.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, ST_WCHAR)->AddWStr(units.c_str());
        storage.GetBuf(DATA_ROBOTS, DATA_ROBOTS_UNITS, ST_WCHAR)->AddWStr(units.c_str());
        storage.Save(path.c_str());
    }
    ~ReloadScene() {
        g_MatrixMap->LeaveDialogMode();
        CMatrixHint::SetRuntimeBitmap(L"restart-pixel", nullptr);
        g_ExitState = previous_exit;
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

std::vector<CMatrixRobotAI *> robots(int side = 0) {
    std::vector<CMatrixRobotAI *> result;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && (!side || object->GetSide() == side)) result.push_back(object->AsRobot());
    return result;
}
void select(CMatrixRobotAI *robot) {
    auto &side = *g_MatrixMap->GetPlayerSide();
    side.CreateGroupFromCurrent(robot);
    side.m_CurrSel = ROBOT_SELECTED;
}
void select_building_before_tick(CFormMatrixGame &form) {
    const auto time = g_MatrixMap->GetTime();
    auto *geometry = tests::load_battle_mesh({});
    auto building = tests::make_battle_building(*geometry, PLAYER_SIDE, BUILDING_TITAN, 320, 320);
    SelectionFixture visible(building.get());
    D3DXMATRIX identity;
    D3DXMatrixIdentity(&identity);
    const auto center = g_MatrixMap->m_Camera.Project(building->GetGeoCenter(), identity);
    const CPoint from(static_cast<int>(center.x) - 2, static_cast<int>(center.y) - 2);
    const CPoint to(from.x + 4, from.y + 4);
    MG_CHECK(building->InRect(CRect(from.x, from.y, to.x, to.y)));
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
    form.MouseMove(from.x, from.y);
    form.MouseKey(B_DOWN, VK_LBUTTON, from.x, from.y);
    MG_CHECK(CMultiSelection::m_GameSelection != nullptr);
    form.MouseMove(to.x, to.y);
    form.MouseKey(B_UP, VK_LBUTTON, to.x, to.y);
    auto &player = *g_MatrixMap->GetPlayerSide();
    MG_CHECK(CMultiSelection::m_GameSelection == nullptr);
    MG_CHECK(!Input::isVKeyPressed(VK_LBUTTON));
    MG_CHECK(player.m_CurrSel == BUILDING_SELECTED && player.m_ActiveObject == building.get());
    MG_CHECK(g_MatrixMap->GetTime() == time);
    player.Select(NOTHING, nullptr);
}
enum class Origin { Moving, PendingWin, PendingLoss };
void restart_profile(Origin origin, bool check_retired_groups = false) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        for (int step : {10, 16}) {
            Profile profile{mode};
            profile.step_ms = step;
            profile.unavailable_icon_targets = true;
            profile.initially_selected = false;
            Session session(profile);
            ReloadScene scene;
            CFormMatrixGame form;
            auto *const map = g_MatrixMap;
            auto *const terrain = map->m_Unit;
            std::cout << session.metadata("storage-restart-v1") << " origin=" << static_cast<int>(origin) << '\n';
            for (int attempt = 0; attempt < 2; ++attempt) {
                if (check_retired_groups && robots(PLAYER_SIDE).size() == 1)
                    tests::BattleGeometry::create_robot(*map, PLAYER_SIDE, 140, 260);
                const auto group_members = robots(PLAYER_SIDE);
                auto before_robots = robots();
                std::vector<size_t> previous_ids;
                for (auto *robot : before_robots) previous_ids.push_back(robot->UID);
                if (origin == Origin::Moving) {
                    auto *robot = robots(PLAYER_SIDE).front();
                    select(robot);
                    const float x = robot->m_PosX, y = robot->m_PosY;
                    session.ground_command(500, 300);
                    for (int i = 0; i < 25; ++i) session.advance();
                    MG_CHECK(robot->m_PosX != x || robot->m_PosY != y);
                    MG_CHECK(map->GetPlayerSide()->m_PlayerGroup[robot->GetGroupLogic()].Order() == mpo_MoveTo);
                } else {
                    for (auto *robot : robots(origin == Origin::PendingLoss ? PLAYER_SIDE : 2)) map->StaticDelete(robot);
                    // Wait until production has classified the side, then stop before its delayed dialog.
                    for (int i = 0; map->GetSideById(origin == Origin::PendingLoss ? PLAYER_SIDE : 2)->GetStatus() == SS_ACTIVE && i < 250; ++i) session.advance();
                    MG_CHECK(map->GetSideById(origin == Origin::PendingLoss ? PLAYER_SIDE : 2)->GetStatus() != SS_ACTIVE);
                    MG_CHECK(!map->IsPaused() && map->m_DialogModeName == nullptr);
                    MG_CHECK(bool(FLAG(map->m_Flags, MMFLAG_WIN)) == (origin == Origin::PendingWin));
                }
                if (origin == Origin::Moving) {
                    // A transient old-battle base and its owned build queue are absent from the saved scene.
                    auto *geometry = tests::load_battle_mesh({});
                    auto base = tests::make_battle_building(*geometry, PLAYER_SIDE, BUILDING_BASE, 180, 480);
                    map->AddObject(base.get(), true);
                    auto *registered = base.release();
                    auto queued = tests::make_engine_object<CMatrixRobotAI>();
                    queued->m_Side = PLAYER_SIDE;
                    queued->UnitInsert(0, MRT_CHASSIS, RUK_CHASSIS_WHEEL);
                    queued->SetBase(registered);
                    registered->m_BS.AddItem(queued.get());
                    MG_CHECK(registered->m_BS.GetItemsCnt() == 1);
                    queued.release();
                }
                auto &player = *map->GetPlayerSide();
                if (check_retired_groups) {
                    MG_CHECK(group_members.size() == 2 && group_members[0] != group_members[1]);
                    // Finish movement first: an unpaused tick would pump the
                    // inactive selection that this regression needs to retain.
                    player.ResetSelection();
                    player.PumpGroups();
                    select(group_members[0]);
                    select(group_members[1]);
                }
                player.SetStatValue(STAT_TIME, 1234);
                player.SetStatValue(STAT_ROBOT_KILL, 7);
                player.SetResourceAmount(TITAN, 777);
                const auto old_time = session.time();
                random::Rnd(); // Make a seed/reset distinguishable from an untouched stream.
                const auto old_draws = random::draw_count();
                Input::onKeyDown(VK_UP);
                press(form, VK_ESCAPE);
                if (check_retired_groups) {
                    auto *first = player.GetFirstGroup();
                    MG_CHECK(player.GetCurGroup() == nullptr);
                    MG_CHECK(first && first->m_NextGroup && first->m_NextGroup->m_NextGroup == nullptr);
                    auto *second = first->m_NextGroup;
                    MG_CHECK(first->GetObjectsCnt() == 1 && second->GetObjectsCnt() == 1);
                    MG_CHECK(first->m_FirstObject->GetObject() == group_members[0]);
                    MG_CHECK(second->m_FirstObject->GetObject() == group_members[1]);
                }
                press(form, 'R');
                MG_CHECK(map->m_DialogModeHints.size() == 2);
                for (int i = 0; i < 3; ++i) session.advance();
                MG_CHECK(session.time() == old_time);
                SEffectHandler smoke;
                CMatrixEffect::CreateSmoke(&smoke, D3DXVECTOR3(100, 100, 20), 10000, 1000, 100, 0xffffffff);
                MG_CHECK(smoke.effect != nullptr && map->m_EffectsFirst != nullptr);
                press(form, VK_RETURN);
                MG_CHECK(g_MatrixMap == map && map->m_Unit == terrain);
                MG_CHECK(map->IsPaused());
                MG_CHECK(map->m_DialogModeName && wcscmp(map->m_DialogModeName, TEMPLATE_DIALOG_BEGIN) == 0);
                MG_CHECK(map->m_DialogModeHints.size() == 1);
                MG_CHECK(session.time() == old_time);
                MG_CHECK(RestartMapObservation::result_countdown(*map) == 0);
                MG_CHECK(!FLAG(map->m_Flags, MMFLAG_WIN));
                MG_CHECK(g_ExitState == 0 && !FLAG(g_Flags, GFLAG_EXITLOOP));
                MG_CHECK(player.GetStatValue(STAT_TIME) == 0 && player.GetStatValue(STAT_ROBOT_KILL) == 0);
                MG_CHECK(robots().size() == 2);
                MG_CHECK(player.GetStatus() == SS_ACTIVE && map->GetSideById(2)->GetStatus() == SS_ACTIVE);
                MG_CHECK(player.GetCurSelGroup()->GetObjectsCnt() == 0);
                MG_CHECK(player.GetCurGroup() == nullptr && player.m_CurrSel == NOTHING_SELECTED);
                MG_CHECK(map->m_EffectsFirst == nullptr && smoke.effect == nullptr);
                MG_CHECK(random::mode() == mode && random::draw_count() == old_draws);
                int object_count = 0;
                for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
                    ++object_count;
                    MG_CHECK(object->IsLiveRobot());
                }
                MG_CHECK(object_count == 2);
                for (int y = 0; y < map->m_Size.y; ++y)
                    for (int x = 0; x < map->m_Size.x; ++x) MG_CHECK(map->UnitGet(x, y)->m_Base == nullptr);
                if (check_retired_groups) MG_CHECK(player.GetFirstGroup() == nullptr);
                if (check_retired_groups)
                    for (auto *group = player.GetFirstGroup(); group; group = group->m_NextGroup) {
                        for (auto *entry = group->m_FirstObject; entry; entry = entry->m_NextObject) {
                            for (size_t i = 0; i < before_robots.size(); ++i) {
                                if (entry->GetObject() != before_robots[i]) continue;
                                std::cout << "retained group member from retired robot uid=" << previous_ids[i];
                                bool recycled = false;
                                for (auto *live : robots()) if (live == entry->GetObject()) {
                                    std::cout << " now aliases live uid=" << live->UID;
                                    recycled = true;
                                }
                                std::cout << (recycled ? "\n" : " with no live replacement\n");
                                MG_CHECK(entry->GetObject() != before_robots[i]);
                            }
                        }
                    }
                for (auto *robot : robots()) {
                    for (auto uid : previous_ids) MG_CHECK(robot->UID != uid);
                    MG_CHECK(robot->m_PosX == (robot->GetSide() == PLAYER_SIDE ? 100 : 500));
                    MG_CHECK(robot->m_PosY == (robot->GetSide() == PLAYER_SIDE ? 200 : 500));
                }
                for (int side = 1; side <= 2; ++side)
                    for (int resource = 0; resource < MAX_RESOURCES; ++resource)
                        MG_CHECK(map->GetSideById(side)->GetResourcesAmount(static_cast<ERes>(resource)) == 100 * side + 1 + resource);
                std::cout << "restart=" << attempt << " time=" << old_time << " draws="
                          << random::draw_count() - old_draws << " held=" << Input::isVKeyPressed(VK_UP)
                          << " effects=" << (map->m_EffectsFirst != nullptr) << '\n';
                MG_CHECK(Input::isVKeyPressed(VK_UP)); // Map reload does not synthesize key releases.
                Input::onKeyUp(VK_UP);
                press(form, VK_RETURN);
                MG_CHECK(!map->IsPaused() && map->m_DialogModeName == nullptr);
                if (check_retired_groups) select_building_before_tick(form);
                auto *robot = robots(PLAYER_SIDE).front();
                select(robot);
                session.ground_command(500, 300);
                for (int i = 0; i < 25; ++i) session.advance();
                MG_CHECK(robot->m_PosX != 100 || robot->m_PosY != 200);
                while (session.time() < old_time + 2200) {
                    session.advance();
                    MG_CHECK(!map->IsPaused() && map->m_DialogModeName == nullptr);
                }
            }
        }
    }
}
void restart_movement() { restart_profile(Origin::Moving); }
void restart_pending_win() { restart_profile(Origin::PendingWin); }
void restart_pending_loss() { restart_profile(Origin::PendingLoss); }
void restart_retired_group() { restart_profile(Origin::Moving, true); }
struct RestartLoopForm : CFormMatrixGame {
    Session &session;
    CForm *previous = g_FormCur;
    std::vector<int> ticks;
    std::vector<std::string> trace;
    unsigned draws{}, paused_ticks{};
    uint64_t last_rng_draws{};
    explicit RestartLoopForm(Session &value) : session(value) {
        g_FormCur = this;
        SETFLAG(g_Flags, GFLAG_APPACTIVE);
        SETFLAG(g_MatrixMap->m_Flags, MMFLAG_VIDEO_RESOURCES_READY);
    }
    ~RestartLoopForm() { g_FormCur = previous; }
    std::string snapshot() const {
        std::ostringstream state;
        auto &player = *g_MatrixMap->GetPlayerSide();
        state << session.time() << ':' << random::draw_count() << ':' << g_MatrixMap->IsPaused()
              << ':' << bool(FLAG(g_Flags, GFLAG_4SPEED)) << ':' << bool(FLAG(g_Flags, GFLAG_KEEPALIVE))
              << ':' << Input::isVKeyPressed(VK_UP) << ':' << player.m_CurrSel
              << ':' << player.GetCurSelGroup()->GetObjectsCnt();
        for (auto *robot : robots()) {
            state << '|' << robot->GetSide() << ':' << std::bit_cast<uint32_t>(robot->m_PosX)
                  << ',' << std::bit_cast<uint32_t>(robot->m_PosY);
            if (robot->GetSide() == PLAYER_SIDE) {
                const int group = robot->GetGroupLogic();
                MG_CHECK(group >= -1 && group < MAX_LOGIC_GROUP);
                if (group >= 0) {
                    const auto &order = player.m_PlayerGroup[group];
                    state << ':' << order.Order() << ':' << order.m_To.x << ',' << order.m_To.y
                          << ':' << (order.m_Obj == nullptr);
                }
            }
        }
        for (int side = 1; side <= 2; ++side)
            for (int resource = 0; resource < MAX_RESOURCES; ++resource)
                state << ':' << g_MatrixMap->GetSideById(side)->GetResourcesAmount(static_cast<ERes>(resource));
        return state.str();
    }
    void Takt(int delta) override {
        MG_CHECK(draws == ticks.size());
#if defined(__MINGW32__)
        MG_CHECK((_controlfp(0, 0) & _MCW_PC) == _PC_24);
#endif
        const int before = session.time();
        const bool paused = g_MatrixMap->IsPaused();
        const auto before_rng = random::draw_count();
        ticks.push_back(delta);
        CFormMatrixGame::Takt(delta);
        MG_CHECK(session.time() == before + (paused ? 0 : delta));
        if (paused) {
            ++paused_ticks;
            MG_CHECK(random::draw_count() == before_rng);
        }
        for (auto *robot : robots()) MG_CHECK(std::isfinite(robot->m_PosX) && std::isfinite(robot->m_PosY));
#if defined(__MINGW32__)
        MG_CHECK((_controlfp(0, 0) & _MCW_PC) == _PC_24);
#endif
    }
    void Draw() override {
        MG_CHECK(draws + 1 == ticks.size() && !FLAG(g_Flags, GFLAG_APPCLOSE));
        ++draws;
        g_MatrixMap->m_Camera.BeforeDraw();
        last_rng_draws = random::draw_count();
        trace.push_back(snapshot());
    }
};
void loop_key(tests::loop::Round &round, uint8_t key) {
    round.messages.emplace_back(WM_KEYDOWN, key);
    round.messages.emplace_back(WM_KEYUP, key);
}
void loop_word(tests::loop::Round &round, const char *word) {
    for (; *word; ++word) loop_key(round, static_cast<uint8_t>(*word));
}
void loop_restart_toggles() {
    constexpr bool cheats = MATRIXGAME_SCENARIO_CHEATS != 0;
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        std::vector<std::string> reference;
        for (int repeat = 0; repeat < 2; ++repeat) {
            Profile profile{mode, 85, step, 1000, false, false, true};
            Session session(profile);
            ReloadScene scene;
            RestartLoopForm form(session);
            auto *const map = g_MatrixMap;
            auto *const terrain = map->m_Unit;
            std::cout << session.metadata("loop-restart-toggles-v1") << " repeat=" << repeat << '\n';
            tests::loop::Script script;
            // Give each fresh order time to pass the robot's normal planning cadence.
            for (int index = 1; index <= 99; ++index) script.rounds.push_back({step * index * 1000LL, {}});
            loop_word(script.rounds[0], "NEED4SPEED");
            loop_word(script.rounds[0], "KEEPALIVE");
            const auto player_robot = [&] {
                const auto live = robots(PLAYER_SIDE);
                MG_CHECK(!live.empty());
                return live.front();
            };
            auto move = [&] {
                auto *robot = player_robot();
                select(robot);
                session.ground_command(500, 300);
                const int group = robot->GetGroupLogic();
                MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
                MG_CHECK(map->GetPlayerSide()->m_PlayerGroup[group].Order() == mpo_MoveTo);
                map->GetPlayerSide()->PLDropAllActions();
            };
            script.rounds[0].after_messages = move;
            int restarts = 0, old_time = 0;
            size_t old_uid = 0;
            uint64_t paused_draws = 0;
            std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> old_core(nullptr,
                [](SObjectCore *core) { core->Release(); });
            for (int index : {29, 61}) script.rounds[index].after_messages = [&] {
                auto *robot = player_robot();
                std::cout << "before restart=" << restarts << " time=" << session.time() << " position="
                          << robot->m_PosX << ',' << robot->m_PosY << " ticks=" << form.ticks.size()
                          << " paused=" << map->IsPaused() << '\n';
                MG_CHECK(std::abs(robot->m_PosX - 100) + std::abs(robot->m_PosY - 200) > 5);
                old_uid = robot->UID;
                old_core.reset(robot->GetCore(DEBUG_CALL_INFO));
                auto &player = *map->GetPlayerSide();
                player.SetStatValue(STAT_TIME, 1234);
                player.SetStatValue(STAT_ROBOT_KILL, 7);
                for (int side = 1; side <= 2; ++side)
                    for (int resource = 0; resource < MAX_RESOURCES; ++resource)
                        map->GetSideById(side)->SetResourceAmount(static_cast<ERes>(resource), 777 + resource);
                // This iteration ticks once before the following restart messages.
                old_time = session.time() + step * (cheats ? 4 : 1);
            };
            for (int index : {30, 62}) {
                script.rounds[index].messages.emplace_back(WM_KEYDOWN, VK_UP);
                loop_key(script.rounds[index], VK_ESCAPE);
                loop_key(script.rounds[index], 'R');
                loop_key(script.rounds[index], VK_RETURN);
                script.rounds[index].after_messages = [&] {
                    ++restarts;
                    MG_CHECK(g_MatrixMap == map && map->m_Unit == terrain && session.time() == old_time);
                    MG_CHECK(map->IsPaused() && map->m_DialogModeName &&
                        std::wstring(map->m_DialogModeName) == TEMPLATE_DIALOG_BEGIN);
                    MG_CHECK(robots().size() == 2 && robots(PLAYER_SIDE).size() == 1);
                    auto *robot = player_robot();
                    MG_CHECK(robot->UID != old_uid && old_core->m_Object == nullptr && old_core->m_Ref == 1);
                    MG_CHECK(robot->m_PosX == 100 && robot->m_PosY == 200);
                    auto &player = *map->GetPlayerSide();
                    MG_CHECK(player.GetCurSelGroup()->GetObjectsCnt() == 0 && player.GetCurGroup() == nullptr);
                    MG_CHECK(player.GetFirstGroup() == nullptr && player.m_CurrSel == NOTHING_SELECTED);
                    MG_CHECK(player.GetStatValue(STAT_TIME) == 0 && player.GetStatValue(STAT_ROBOT_KILL) == 0);
                    const int group = robot->GetGroupLogic();
                    MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP && player.m_PlayerGroup[group].Order() == mpo_Stop);
                    for (int side = 1; side <= 2; ++side)
                        for (int resource = 0; resource < MAX_RESOURCES; ++resource)
                            MG_CHECK(map->GetSideById(side)->GetResourcesAmount(static_cast<ERes>(resource)) ==
                                100 * side + 1 + resource);
                    MG_CHECK(bool(FLAG(g_Flags, GFLAG_4SPEED)) == cheats && bool(FLAG(g_Flags, GFLAG_KEEPALIVE)) == cheats);
                    MG_CHECK(Input::isVKeyPressed(VK_UP));
                    MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP) && g_ExitState == 0);
                    MG_CHECK(random::mode() == mode && form.last_rng_draws > 0 &&
                        random::draw_count() == form.last_rng_draws);
                    paused_draws = random::draw_count();
                };
            }
            for (int index : {31, 63}) {
                loop_key(script.rounds[index], VK_RETURN);
                script.rounds[index].messages.emplace_back(WM_KEYUP, VK_UP);
                script.rounds[index].after_messages = [&] {
                    MG_CHECK(!map->IsPaused() && map->m_DialogModeName == nullptr && session.time() == old_time);
                    MG_CHECK(random::mode() == mode && random::draw_count() == paused_draws);
                    MG_CHECK(!Input::isVKeyPressed(VK_UP));
                    move();
                };
            }
            loop_word(script.rounds[93], "NEED4SPEED");
            loop_word(script.rounds[93], "KEEPALIVE");
            script.rounds[93].after_messages = move;
            script.rounds[98].messages.emplace_back(WM_CLOSE, 0);
            std::string close_state;
            script.rounds[98].after_messages = [&] {
                MG_CHECK(FLAG(g_Flags, GFLAG_APPCLOSE));
                close_state = form.snapshot();
            };
            MG_CHECK(L3GRun(script.platform()) == 1);
            MG_CHECK(!close_state.empty() && close_state == form.snapshot());
            MG_CHECK(restarts == 2 && form.draws == 98 && form.ticks.size() == 98);
            MG_CHECK(form.paused_ticks == 2 && script.clock_reads == 99);
            for (int index = 0; index < 93; ++index) MG_CHECK(form.ticks[index] == step * (cheats ? 4 : 1));
            for (int index = 93; index < 98; ++index) MG_CHECK(form.ticks[index] == step);
            MG_CHECK(session.time() == step * (cheats ? 369 : 96));
            MG_CHECK(!FLAG(g_Flags, GFLAG_4SPEED) && !FLAG(g_Flags, GFLAG_KEEPALIVE));
            MG_CHECK(FLAG(g_Flags, GFLAG_APPCLOSE) && !FLAG(g_Flags, GFLAG_EXITLOOP));
            MG_CHECK(!map->IsPaused());
            const auto *last_robot = player_robot();
            MG_CHECK(std::abs(last_robot->m_PosX - 100) + std::abs(last_robot->m_PosY - 200) > 5);
            MG_CHECK(script.waits == 0 && script.round == script.rounds.size());
            if (repeat == 0) reference = form.trace;
            else MG_CHECK(form.trace == reference);
        }
    }
}
constexpr tests::Case cases[] = {
    {"game.loop.restart_toggles", loop_restart_toggles},
    {"game.restart.retired_group", restart_retired_group},
    {"game.restart.movement", restart_movement},
    {"game.restart.pending_win", restart_pending_win},
    {"game.restart.pending_loss", restart_pending_loss},
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
