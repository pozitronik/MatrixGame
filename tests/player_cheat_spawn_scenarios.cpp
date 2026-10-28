// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "battle_material.hpp"
#include "battle_mesh.hpp"
#include "battle_building.hpp"
#include "battle_flyer.hpp"
#include "test_support.hpp"
#include "player_group_capacity_fixture.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixFormGame.hpp"
#include "Interface/CConstructor.h"
#include "cheats.hpp"
#include "input.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <sstream>
#include <vector>

namespace {
using namespace tests::battle;
CMatrixSideUnit &side() { return *g_MatrixMap->GetPlayerSide(); }
constexpr bool form_cheats = MATRIXGAME_SCENARIO_CHEATS != 0;
void command(CFormMatrixGame &form, const char *word, bool through_form) {
    for (; *word; ++word) {
        if (through_form) {
            form.Keyboard(true, static_cast<uint8_t>(*word));
            form.Keyboard(false, static_cast<uint8_t>(*word));
            MG_CHECK(!Input::isVKeyPressed(static_cast<uint8_t>(*word)));
        }
        else {
            const bool matched = Cheats::processInput(static_cast<uint8_t>(*word));
            MG_CHECK(matched == (word[1] == '\0'));
        }
    }
}
CMatrixRobotAI *registered(const Session &session, EntityId id) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot() && session.matches(id, object->AsRobot())) return object->AsRobot();
    throw std::runtime_error("Missing registered robot");
}
int player_robots() {
    int count = 0;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && object->GetSide() == PLAYER_SIDE) ++count;
    return count;
}
bool in_logic(const CMatrixMapStatic *expected) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object == expected) return true;
    return false;
}
using Core = std::unique_ptr<SObjectCore, void (*)(SObjectCore *)>;
Core retain(CMatrixMapStatic &object) {
    return Core(object.GetCore(DEBUG_CALL_INFO), [](SObjectCore *value) { value->Release(); });
}
std::array<int, MAX_RESOURCES> resources() {
    std::array<int, MAX_RESOURCES> result{};
    for (int i = 0; i < MAX_RESOURCES; ++i) result[i] = side().GetResourcesAmount(static_cast<ERes>(i));
    return result;
}
void inspect_robot(CMatrixRobotAI &robot, bool crazy) {
    MG_CHECK(robot.GetSide() == PLAYER_SIDE && robot.IsCrazy() == crazy);
    MG_CHECK(robot.m_UnitCnt == (crazy ? 8 : 7));
    std::vector<int> weapons;
    int head = 0, chassis_count = 0, armor_count = 0, head_count = 0;
    for (int i = 0; i < robot.m_UnitCnt; ++i) {
        const auto &unit = robot.m_Unit[i];
        MG_CHECK(unit.m_Graph && unit.m_Graph->GetSkin());
        if (unit.m_Type == MRT_CHASSIS) {
            MG_CHECK(unit.u1.s1.m_Kind == RUK_CHASSIS_ANTIGRAVITY);
            ++chassis_count;
        }
        if (unit.m_Type == MRT_ARMOR) {
            MG_CHECK(unit.u1.s1.m_Kind == RUK_ARMOR_FULLSTACK);
            ++armor_count;
        }
        if (unit.m_Type == MRT_HEAD) {
            head = unit.u1.s1.m_Kind;
            ++head_count;
        }
        if (unit.m_Type == MRT_WEAPON) weapons.push_back(unit.u1.s1.m_Kind);
    }
    std::sort(weapons.begin(), weapons.end());
    const std::vector<int> expected = crazy ? std::vector<int>{RUK_WEAPON_CANNON, RUK_WEAPON_MISSILE,
        RUK_WEAPON_MORTAR, RUK_WEAPON_LASER, RUK_WEAPON_PLASMA} : std::vector<int>(4, RUK_WEAPON_LASER);
    MG_CHECK(chassis_count == 1 && armor_count == 1 && head_count == 1);
    MG_CHECK(weapons == expected && head == (crazy ? RUK_HEAD_FIREWALL : RUK_HEAD_DYNAMO));
    for (int i = 0; i < int(weapons.size()); ++i) MG_CHECK(robot.GetWeapon(i).IsEffectPresent());
}
std::string created_state(const Session &session, CMatrixRobotAI &robot) {
    std::ostringstream state;
    state << session.snapshot() << ":created=" << robot.GetSide() << ',' << int(robot.m_CurrState)
          << ',' << std::bit_cast<uint32_t>(robot.m_PosX) << ',' << std::bit_cast<uint32_t>(robot.m_PosY)
          << ',' << std::bit_cast<uint32_t>(robot.GetHitPoint()) << ',' << robot.IsCrazy();
    const int group = robot.GetGroupLogic();
    MG_CHECK(group >= -1 && group < MAX_LOGIC_GROUP);
    state << ":group=" << group;
    if (group >= 0) {
        const auto &order = side().m_PlayerGroup[group];
        state << ':' << int(order.Order()) << ':' << order.m_To.x << ',' << order.m_To.y << ':' << (order.m_Obj == nullptr);
    }
    return state.str();
}
void cheat_assets() {
    g_Config.m_RobotShadow = SHADOW_OFF;
    g_Config.m_ObjTexturesGloss = false;
    g_Config.m_ItemChars[CHASSIS5_STRUCTURE] = 1000;
    g_Config.m_ItemChars[CHASSIS5_MOVE_SPEED] = .8f;
    g_Config.m_ItemChars[CHASSIS5_MOVE_WATER_CORR] = .5f;
    g_Config.m_ItemChars[CHASSIS5_MOVE_SLOPE_CORR_DOWN] = 2.5f;
    g_Config.m_ItemChars[CHASSIS5_MOVE_SLOPE_CORR_UP] = .7f;
    g_Config.m_ItemChars[CHASSIS5_ROTATION_SPEED] = .04f;
    g_Config.m_ItemChars[ARMOR5_STRUCTURE] = 500;
    g_Config.m_ItemChars[ARMOR5_ROTATION_SPEED] = .04f;
    const tests::MeshAttachment chassis[]{{1, L"armor", 0, 0, 20}, {10, L"left-exhaust", -5, -10, 5}, {11, L"right-exhaust", 5, -10, 5}};
    const tests::MeshAttachment armor[]{{1, L"head", 0, 0, 20}, {2, L"weapon", -20, 0, 10}, {3, L"weapon", -10, 0, 10},
        {4, L"weapon", 10, 0, 10}, {5, L"weapon", 20, 0, 10}, {6, L"mortar", 0, 0, 20}};
    auto load = [](const wchar_t *path, int kind, std::span<const tests::MeshAttachment> links, bool combat = false) {
        const auto name = std::wstring(path) + std::to_wstring(kind);
        tests::install_battle_texture(name.c_str());
        tests::load_battle_material_mesh(links, (name + L".vo").c_str(), name.c_str(), combat);
    };
    load(OBJECT_PATH_ROBOT_CHASSIS, RUK_CHASSIS_ANTIGRAVITY, chassis);
    load(OBJECT_PATH_ROBOT_ARMOR, RUK_ARMOR_FULLSTACK, armor);
    const tests::MeshAttachment head{1, L"top", 0, 0, 20};
    for (int kind : {int(RUK_HEAD_DYNAMO), int(RUK_HEAD_FIREWALL)}) load(OBJECT_PATH_ROBOT_HEAD, kind, std::span(&head, 1));
    const tests::MeshAttachment muzzle{1, L"muzzle", 0, 15, 0};
    for (auto kind : {RUK_WEAPON_CANNON, RUK_WEAPON_MISSILE, RUK_WEAPON_PLASMA, RUK_WEAPON_LASER, RUK_WEAPON_MORTAR}) {
        load(OBJECT_PATH_ROBOT_WEAPON, kind, std::span(&muzzle, 1), true);
        g_Config.m_WeaponRadius[WeapKind2Index(kind)] = 100;
        g_Config.m_WeaponCooldown[WeapKind2Index(kind)] = 1000;
    }
    auto &slots = g_MatrixMap->m_RobotWeaponMatrix[RUK_ARMOR_FULLSTACK - 1];
    slots = {};
    slots.cnt = 5;
    slots.common = 4;
    slots.extra = 1;
    constexpr DWORD common = (1 << (RUK_WEAPON_CANNON-1)) | (1 << (RUK_WEAPON_MISSILE-1)) |
        (1 << (RUK_WEAPON_PLASMA-1)) | (1 << (RUK_WEAPON_LASER-1));
    for (int i = 0; i < 5; ++i) {
        slots.list[i].id = i + 2;
        slots.list[i].access_invert = i < 4 ? common : (1 << (RUK_WEAPON_MORTAR-1)) | (1 << (RUK_WEAPON_BOMB-1));
    }
    auto *names = g_MatrixData->BlockGetAdd(IF_LABELS_BLOCKPAR)->BlockGetAdd(L"RobotNames");
    names->ParSetAdd(L"Hull5Key", L"Synthetic Fullstack ");
    names->ParSetAdd(L"Chas5Key", L"antigravity ");
    names->ParSetAdd(L"Head2Key", L"Dynamo");
    names->ParSetAdd(L"Head4Key", L"Firewall");
    for (int base : {CHASSIS5_TITAN, ARMOR5_TITAN, HEAD2_TITAN, HEAD4_TITAN, WEAPON2_TITAN,
                     WEAPON3_TITAN, WEAPON5_TITAN, WEAPON6_TITAN, WEAPON8_TITAN})
        for (int r = 0; r < MAX_RESOURCES; ++r) g_Config.m_Price[base + r] = 11 + r;
}
std::vector<std::string> spawn_run(random::Mode mode, int step, bool through_form, bool capacity) {
    tests::BattleMaterialProfile material;
    tests::BattleCombatAssets assets;
    Session session({mode, 80, step, 2000, false, false, true});
    assets.install();
    cheat_assets();
    CFormMatrixGame form;
    if (capacity) {
        for (int i = 0; i < MAX_ROBOTS; ++i) {
            auto *robot = i < 2 ? registered(session, i+1) : tests::BattleGeometry::create_robot(*g_MatrixMap,
                PLAYER_SIDE, 60 + (i%8)*70, 60 + (i/8)*70);
            robot->SetGroupLogic(i);
        }
        MG_CHECK(player_robots() == MAX_ROBOTS);
    }
    g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(300, 100, 0);
    std::cout << session.metadata("cheat-spawn-v1") << " form=" << through_form << " capacity=" << capacity << std::endl;
    const auto balance = resources();
    const int before = player_robots();
    std::vector<CMatrixRobotAI *> original;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot()) original.push_back(object->AsRobot());
    command(form, "SPAWN", through_form);
    const bool enabled = !through_form || form_cheats;
    MG_CHECK(player_robots() == before + int(enabled) && resources() == balance);
    CMatrixRobotAI *created = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && std::find(original.begin(), original.end(), object->AsRobot()) == original.end()) {
            MG_CHECK(!created);
            created = object->AsRobot();
        }
    MG_CHECK(bool(created) == enabled);
    if (!enabled) return {session.snapshot()};
    inspect_robot(*created, false);
    auto core = retain(*created);
    MG_CHECK(created->m_PosX == 300 && created->m_PosY == 100 && created->GetMaxHitPoint() == 1000);
    MG_CHECK(created->GetBase() == nullptr && created->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
    MG_CHECK(created->GetGroupLogic() == (capacity ? MAX_LOGIC_GROUP-1 : 0));
    MG_CHECK(side().m_PlayerGroup[created->GetGroupLogic()].Order() == mpo_Stop);
    MG_CHECK(side().GetStatValue(STAT_ROBOT_BUILD) == 0 && session.rejected_icon_targets() == 1);
    std::vector<std::string> trace{created_state(session, *created)};
    if (capacity) return trace; // Last valid slot; do not advance an over-limit side through unrelated AI arrays.
    side().CreateGroupFromCurrent(created);
    side().Select(ROBOT, nullptr);
    session.ground_command(450, 120);
    MG_CHECK(side().m_PlayerGroup[created->GetGroupLogic()].Order() == mpo_MoveTo);
    const float x = created->m_PosX, y = created->m_PosY;
    while (session.time() < 10000) {
        session.advance();
        MG_CHECK(core->m_Object == created && created->IsLiveRobot() && !created->IsCrazy());
        MG_CHECK(std::isfinite(created->m_PosX) && std::isfinite(created->m_PosY));
        trace.push_back(created_state(session, *created));
    }
    MG_CHECK(std::abs(created->m_PosX-x) + std::abs(created->m_PosY-y) > 20);
    MG_CHECK(created->PLIsInPlace() && created->m_PosX > 400);
    return trace;
}
template <bool Capacity> void spawn_creation() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool through_form : {false, true}) {
        const auto first = spawn_run(mode, step, through_form, Capacity);
        MG_CHECK(first == spawn_run(mode, step, through_form, Capacity));
    }
}
void crazy_creation() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool through_form : {false, true}) for (bool have_base : {false, true}) {
        tests::BattleMaterialProfile material;
        tests::BattleCombatAssets assets;
        Session session({mode, 80, step, 1000, false, false, true});
        assets.install();
    cheat_assets();
        auto base = tests::make_battle_building(*registered(session, 1)->m_Unit[0].m_Graph->VO(), PLAYER_SIDE, BUILDING_BASE, 100, 100);
        side().m_Constructor->SetSide(PLAYER_SIDE);
        side().m_Constructor->SetBase(have_base ? base.get() : nullptr);
        CFormMatrixGame form;
        std::cout << session.metadata("cheat-crazy-queue-v1") << " form=" << through_form << " base=" << have_base << std::endl;
        const auto balance = resources();
        std::vector<CMatrixRobotAI *> queued;
        std::vector<Core> cores;
        const bool enabled = (!through_form || form_cheats) && have_base;
        for (int attempt = 0; attempt <= MAX_STACK_UNITS; ++attempt) {
            command(form, "CRAZYBOT", through_form);
            const int expected = enabled ? std::min(attempt+1, MAX_STACK_UNITS) : 0;
            MG_CHECK(base->m_BS.GetItemsCnt() == expected && player_robots() == 2 && resources() == balance);
            MG_CHECK(session.rejected_icon_targets() == unsigned(expected));
            int i = 0;
            for (auto *item = base->m_BS.GetTopItem(); item; item = item->m_NextStackItem, ++i) {
                MG_CHECK(i < expected && item->IsRobot());
                auto *created = item->AsRobot();
                inspect_robot(*created, true);
                MG_CHECK(created->GetBase() == base.get() && created->GetTeam() == 0);
                if (i == int(queued.size())) {
                    queued.push_back(created);
                    cores.push_back(retain(*created));
                }
                MG_CHECK(queued[i] == created && cores[i]->m_Object == created);
                MG_CHECK(item->m_PrevStackItem == (i ? queued[i-1] : nullptr));
            }
            MG_CHECK(i == expected);
        }
        MG_CHECK(side().GetStatValue(STAT_ROBOT_BUILD) == 0);
        side().m_Constructor->SetBase(nullptr);
        base.reset();
        for (const auto &core : cores) MG_CHECK(core->m_Object == nullptr);
    }
}
void spawn_exhausted() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool through_form : {false, true}) {
        tests::BattleMaterialProfile material;
        tests::BattleCombatAssets assets;
        tests::capacity::FullPlayerGroups scene(mode, step);
        assets.install();
        cheat_assets();
        CFormMatrixGame form;
        g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(300, 100, 0);
        MG_CHECK(!side().HasFreePlayerLogicGroup());
        const auto before = scene.snapshot();
        command(form, "SPAWN", through_form);
        MG_CHECK(scene.snapshot() == before);
        MG_CHECK(player_robots() == MAX_LOGIC_GROUP);
        MG_CHECK(scene.session.time() == 0 && scene.session.frame() == 0);
        std::cout << scene.session.metadata("spawn-exhausted-v1") << " form=" << through_form << '\n';
    }
}
std::vector<std::string> crazy_deployment_run(random::Mode mode, int step, bool through_form) {
    tests::BattleMaterialProfile material;
    tests::BattleCombatAssets assets;
    Session session({mode, 80, step, 2000, false, false, true});
    assets.install();
    cheat_assets();
    auto owner = tests::make_battle_building(*registered(session, 1)->m_Unit[0].m_Graph->VO(),
        PLAYER_SIDE, BUILDING_BASE, 100, 100);
    auto *base = owner.get();
    g_MatrixMap->AddObject(owner.release(), true);
    MG_CHECK(session.remove(1) && session.remove(2));
    auto *support = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 500, 80);
    side().PGOrderStop(side().RobotToLogicGroup(support));
    side().m_Constructor->SetSide(PLAYER_SIDE);
    side().m_Constructor->SetBase(base);
    g_Config.m_Timings[UNIT_ROBOT] = 100;
    for (int i = 0; i < MAX_RESOURCES; ++i) g_Config.m_Timings[i] = 100000;
    g_Config.m_Timings[RESOURCE_BASE] = 100000;
    CFormMatrixGame form;
    const auto balance = resources();
    std::cout << session.metadata("cheat-crazy-deployment-v1") << " form=" << through_form << std::endl;
    command(form, "CRAZYBOT", through_form);
    if (through_form && !form_cheats) {
        MG_CHECK(base->m_BS.GetItemsCnt() == 0 && player_robots() == 1 && resources() == balance);
        return {session.snapshot()};
    }
    MG_CHECK(base->m_BS.GetItemsCnt() == 1 && player_robots() == 1);
    auto *robot = base->m_BS.GetTopItem()->AsRobot();
    auto core = retain(*robot);
    inspect_robot(*robot, true);
    bool spawned = false, opened = false, moving_out = false, detached = false, closed = false;
    std::vector<std::string> trace;
    while (session.time() < 10000 && !closed) {
        session.advance();
        MG_CHECK(core->m_Object == robot && robot->IsCrazy() && robot->GetSide() == PLAYER_SIDE);
        if (in_logic(robot) && robot->m_CurrState == ROBOT_IN_SPAWN) spawned = true;
        if (spawned) MG_CHECK(robot->GetMaxHitPoint() == 100000);
        if (base->m_State == BASE_OPENED) opened = true;
        if (robot->m_CurrState == ROBOT_BASE_MOVEOUT) moving_out = true;
        if (spawned && !detached && robot->GetBase() == nullptr) {
            detached = true;
            MG_CHECK(robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD && !robot->IsDisableManual());
            MG_CHECK(std::hypot(robot->m_PosX-100, robot->m_PosY-100) >= BASE_DIST);
        }
        closed = detached && base->m_State == BASE_CLOSED;
        trace.push_back(created_state(session, *robot) + ":base=" + std::to_string(int(base->m_State)) +
            ',' + std::to_string(std::bit_cast<uint32_t>(base->m_BaseFloor)));
    }
    MG_CHECK(spawned && opened && moving_out && detached && closed);
    MG_CHECK(base->m_BS.GetItemsCnt() == 0 && player_robots() == 2 && !base->IsSpawningBot());
    MG_CHECK(side().GetStatValue(STAT_ROBOT_BUILD) == 0 && resources() == balance);
    inspect_robot(*robot, true);
    side().m_Constructor->SetBase(nullptr);
    return trace;
}
template <random::Mode Mode, int Step, bool ThroughForm> void crazy_deployment() {
    // Crazy logic retains process-global stuck history. Each CTest invocation
    // must start fresh; repeating Sessions here would supply a different history.
    const auto trace = crazy_deployment_run(Mode, Step, ThroughForm);
    uint64_t digest = 14695981039346656037ull;
    for (const auto &frame : trace) {
        for (unsigned char byte : frame) digest = (digest ^ byte) * 1099511628211ull;
        digest = (digest ^ '\n') * 1099511628211ull;
    }
    std::cout << "Crazy deployment trace: frames=" << trace.size() << " fnv1a64=" << digest << '\n';
}
struct MaintenanceInput : CMatrixMap {
    static void percentage(CMatrixMap &map, int percent) { map.*&MaintenanceInput::m_MaintenancePRC = percent; }
};
std::vector<std::string> debug_building_run(random::Mode mode, int step, int owner, int percent, int remaining) {
    tests::BattleMaterialProfile material;
    Session session({mode, 80, step, 1000, false, false, true});
    tests::install_battle_transport();
    auto building = tests::make_battle_building(*registered(session, 1)->m_Unit[0].m_Graph->VO(),
        owner, BUILDING_TITAN, 100, 100);
    MG_CHECK(session.remove(2) && session.remove(4));
    MaintenanceInput::percentage(*g_MatrixMap, percent);
    g_Config.m_MaintenanceTime = 1000;
    g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1;
    g_MatrixMap->SetMaintenanceTime(remaining);
    g_MatrixMap->m_TraceStopObj = building.get();
    const auto draws = random::draw_count();
    const auto balance = resources();
    CFormMatrixGame form;
    form.Keyboard(true, 'F');
    form.Keyboard(false, 'F');
#if defined(_DEBUG) && !defined(_RELDEBUG)
    constexpr bool debug_f = true;
#else
    constexpr bool debug_f = false;
#endif
    const bool accepted = debug_f && owner != 0 && percent != 0;
    std::cout << session.metadata("debug-building-reinforcement-v1") << " owner=" << owner
        << " percent=" << percent << " remaining=" << remaining << std::endl;
    MG_CHECK(!Input::isVKeyPressed('F'));
    MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == int(accepted));
    MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == (accepted ? 1000 : debug_f ? 0 : remaining));
    MG_CHECK(random::draw_count() == draws + (accepted ? 3 : 0) && resources() == balance);
    MG_CHECK(session.rejected_icon_targets() == unsigned(accepted));
    std::vector<std::string> trace{session.snapshot()};
    if (accepted) {
        auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
        MG_CHECK(carrier->GetSide() == owner && carrier->CarryingRobot());
        auto *robot = static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
        auto carrier_core = retain(*carrier), robot_core = retain(*robot);
        MG_CHECK(robot->GetSide() == PLAYER_SIDE && robot->m_CurrState == ROBOT_CARRYING);
        MG_CHECK(robot->GetGroupLogic() >= 0 && robot->GetGroupLogic() < MAX_LOGIC_GROUP);
        MG_CHECK(side().m_PlayerGroup[robot->GetGroupLogic()].Order() == mpo_Attack);
        while (session.time() < 100) {
            session.advance();
            MG_CHECK(carrier_core->m_Object == carrier && robot_core->m_Object == robot);
            MG_CHECK(carrier->GetSide() == owner && robot->GetSide() == PLAYER_SIDE);
            MG_CHECK(carrier->CarryingRobot() && robot->m_CurrState == ROBOT_CARRYING);
            trace.push_back(created_state(session, *robot));
        }
    }
    g_MatrixMap->m_TraceStopObj = nullptr;
    return trace;
}
void debug_building_reinforcement() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (int owner : {0, PLAYER_SIDE, 2}) for (int percent : {0, 100}) for (int remaining : {0, 1234}) {
        const auto first = debug_building_run(mode, step, owner, percent, remaining);
        MG_CHECK(first == debug_building_run(mode, step, owner, percent, remaining));
    }
}
constexpr tests::Case cases[]{
    {"game.player.spawn_cheat_creation", spawn_creation<false>},
    {"game.player.spawn_cheat_capacity", spawn_creation<true>},
    {"game.player.spawn_cheat_exhausted", spawn_exhausted},
    {"game.player.crazy_cheat_creation", crazy_creation},
    {"game.player.crazy_cheat_deployment.park_miller_10_raw", crazy_deployment<random::Mode::ParkMiller, 10, false>},
    {"game.player.crazy_cheat_deployment.park_miller_16_raw", crazy_deployment<random::Mode::ParkMiller, 16, false>},
    {"game.player.crazy_cheat_deployment.legacy_crt_10_raw", crazy_deployment<random::Mode::LegacyCRT, 10, false>},
    {"game.player.crazy_cheat_deployment.legacy_crt_16_raw", crazy_deployment<random::Mode::LegacyCRT, 16, false>},
    {"game.player.crazy_cheat_deployment.park_miller_10_form", crazy_deployment<random::Mode::ParkMiller, 10, true>},
    {"game.player.crazy_cheat_deployment.park_miller_16_form", crazy_deployment<random::Mode::ParkMiller, 16, true>},
    {"game.player.crazy_cheat_deployment.legacy_crt_10_form", crazy_deployment<random::Mode::LegacyCRT, 10, true>},
    {"game.player.crazy_cheat_deployment.legacy_crt_16_form", crazy_deployment<random::Mode::LegacyCRT, 16, true>},
    {"game.player.debug_building_reinforcement", debug_building_reinforcement},
};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] {std::abort();});
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
