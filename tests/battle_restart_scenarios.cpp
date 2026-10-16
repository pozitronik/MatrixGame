// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_mesh.hpp"
#include "battle_material.hpp"
#include "battle_building.hpp"
#include "command_world.hpp"
#include "test_support.hpp"
#include "CStorage.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFormGame.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceButton.h"
#include "Interface/MatrixHint.hpp"
#include "input.hpp"
#include <cstring>
#include <filesystem>
#include <vector>

namespace {
using namespace tests::battle;
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
                player.SetStatValue(STAT_TIME, 1234);
                player.SetStatValue(STAT_ROBOT_KILL, 7);
                player.SetResourceAmount(TITAN, 777);
                const auto old_time = session.time();
                random::Rnd(); // Make a seed/reset distinguishable from an untouched stream.
                const auto old_draws = random::draw_count();
                Input::onKeyDown(VK_UP);
                press(form, VK_ESCAPE);
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
                MG_CHECK(map->m_BeforeWinCount == 0);
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
void restart_retired_group_probe() { restart_profile(Origin::Moving, true); }
constexpr tests::Case cases[] = {
    {"probe.restart.retired_group", restart_retired_group_probe},
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
