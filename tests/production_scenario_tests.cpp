// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_material.hpp"
#include "battle_mesh.hpp"
#include "command_world.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObjectBuilding.hpp"
#include "Interface/CInterface.h"
#include "Interface/CConstructor.h"
#include "Interface/CCounter.h"
#include "Interface/CHistory.h"
#include "MatrixGame.h"
#include "MatrixSkinManager.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace {
using tests::battle::Session;
using Cost = std::array<int, MAX_RESOURCES>;

CMatrixRobotAI *source_robot() {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && object->GetSide() == PLAYER_SIDE) return object->AsRobot();
    throw std::runtime_error("Missing synthetic chassis");
}
Cost resources() {
    Cost result{};
    for (int i = 0; i < MAX_RESOURCES; ++i)
        result[i] = g_MatrixMap->GetPlayerSide()->GetResourcesAmount(static_cast<ERes>(i));
    return result;
}
bool in_logic(CMatrixMapStatic *expected) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object == expected) return true;
    return false;
}

void constructor_assets(bool repair = false, int repair_slots = 1) {
    MG_CHECK(repair_slots >= 1 && repair_slots <= 2);
    tests::install_battle_texture(OBJECT_PATH_ROBOT_CHASSIS L"2");
    tests::install_battle_texture(OBJECT_PATH_ROBOT_ARMOR L"1");
    const tests::MeshAttachment link{1, L"armor", 0, 0, 20};
    tests::load_battle_material_mesh(std::span(&link, 1), OBJECT_PATH_ROBOT_CHASSIS L"2.vo",
                                     OBJECT_PATH_ROBOT_CHASSIS L"2");
    const tests::MeshAttachment links[] = {{1, L"head", 0, 0, 20}, {2, L"weapon", 0, 0, 0},
                                         {3, L"weapon", 5, 0, 0}};
    tests::load_battle_material_mesh(std::span(links, repair ? 1u + repair_slots : 1u), OBJECT_PATH_ROBOT_ARMOR L"1.vo",
                                     OBJECT_PATH_ROBOT_ARMOR L"1");
    if (repair) {
        tests::install_battle_texture(OBJECT_PATH_ROBOT_WEAPON L"10");
        const tests::MeshAttachment muzzles[] = {{1, L"muzzle", 0, 15, 0}, {2, L"repair-end", 0, 25, 0}};
        tests::load_battle_material_mesh(muzzles, OBJECT_PATH_ROBOT_WEAPON L"10.vo",
                                         OBJECT_PATH_ROBOT_WEAPON L"10");
        auto &slots = g_MatrixMap->m_RobotWeaponMatrix[RUK_ARMOR_BIREX - 1];
        slots.cnt = slots.common = repair_slots;
        for (int i = 0; i < repair_slots; ++i) {
            slots.list[i].id = 2 + i;
            slots.list[i].access_invert = 1 << (RUK_WEAPON_REPAIR - 1);
        }
        for (int i = 0; i < MAX_RESOURCES; ++i)
            g_Config.m_Price[WEAPON1_TITAN + (RUK_WEAPON_REPAIR - 1) * MAX_RESOURCES + i] = 31 + i;
    }
    auto *names = g_MatrixData->BlockGetAdd(IF_LABELS_BLOCKPAR)->BlockGetAdd(L"RobotNames");
    names->ParSetAdd(L"Hull1Key", L"Synthetic hull ");
    names->ParSetAdd(L"Chas2Key", L"wheels");
    g_Config.m_RobotShadow = SHADOW_OFF;
    g_Config.m_ObjTexturesGloss = false;
    g_Config.m_ItemChars[ARMOR1_STRUCTURE] = 500;
    for (int i = 0; i < MAX_RESOURCES; ++i) {
        g_Config.m_Price[CHASSIS2_TITAN + i] = 11 + i;
        g_Config.m_Price[ARMOR1_TITAN + i] = 21 + i;
    }
}

Cost side_resources(const CMatrixSideUnit &side) {
    Cost result{};
    for (int r = 0; r < MAX_RESOURCES; ++r) result[r] = side.GetResourcesAmount(static_cast<ERes>(r));
    return result;
}

enum class PurchaseEnd { Queued, Neutralized, Deleted };
void ai_production_purchase(PurchaseEnd end = PurchaseEnd::Queued) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int shortage = -1; shortage < (end == PurchaseEnd::Queued ? 4 : 0); ++shortage) {
        tests::BattleMaterialProfile material_profile;
        Session session({mode, 83, 10, 1000, false, false});
        std::cout << "AI purchase shortage=" << shortage << " end=" << int(end) << " profile: "
                  << session.metadata("synthetic-ai-purchase-v1") << '\n';
        constructor_assets(true);
        auto owned = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
            2, BUILDING_BASE, 100, 100);
        for (unsigned id = 1; id <= 4; ++id) MG_CHECK(session.remove(id));
        auto *side = g_MatrixMap->GetSideById(2);
        side->m_Constructor->SetSide(2);
        // Run the real regional analysis with no base/definitions, before purchase.
        side->TaktHL();
        auto *factory = owned.get();
        g_MatrixMap->AddObject(factory, true);
        owned.release();
        struct DefinitionsGuard { ~DefinitionsGuard() { SSpecialBot::ClearAIRobotType(); } } definitions_guard;
        CBlockPar definitions;
        definitions.ParAdd(L"1", L"W,1S,R");
        definitions.ParAdd(L"2", L"W,1S"); // Affordable alternative after the repair purchase.
        SSpecialBot::LoadAIRobotType(definitions, g_MatrixMap->m_RobotWeaponMatrix);
        MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == 2);
        MG_CHECK(SSpecialBot::m_AIRobotTypeList[0].m_HaveRepair);
        const Cost price{63, 66, 69, 72};
        for (int r = 0; r < MAX_RESOURCES; ++r) {
            MG_CHECK(SSpecialBot::m_AIRobotTypeList[0].m_Resources[r] == price[r]);
            side->SetResourceAmount(static_cast<ERes>(r), price[r] - (r == shortage ? 1 : 0));
            g_Config.m_Timings[r] = 1000;
        }
        side->m_WaitResMul = 0; // Reject unavailable funds without an income forecast.
        const auto before = side_resources(*side);
        const auto player_before = resources();
        const auto draws = random::draw_count();
        side->BuildRobot();
        if (shortage >= 0) {
            MG_CHECK(factory->m_BS.GetItemsCnt() == 0);
            MG_CHECK(side_resources(*side) == before && random::draw_count() == draws);
            MG_CHECK(side->m_BuildRobotLast == -1 && side->m_WaitResForBuildRobot == -1);
        }
        else {
            MG_CHECK(factory->m_BS.GetItemsCnt() == 1);
            MG_CHECK((side_resources(*side) == Cost{}));
            MG_CHECK(random::draw_count() == draws + 1 && side->m_BuildRobotLast == 0);
            auto *built = factory->m_BS.GetTopItem()->AsRobot();
            MG_CHECK(built->GetSide() == 2 && built->GetBase() == factory && !in_logic(built));
            MG_CHECK(built->m_UnitCnt == 3 && built->m_Unit[2].m_Type == MRT_WEAPON);
            MG_CHECK(built->m_Unit[2].u1.s1.m_Kind == RUK_WEAPON_REPAIR);
            MG_CHECK(built->m_Unit[2].u1.s1.m_LinkMatrix == 2 && built->m_Unit[2].m_Graph->GetSkin());
            Cost constructor_price{};
            side->m_Constructor->GetConstructionPrice(constructor_price.data());
            MG_CHECK(constructor_price == price);
            if (end != PurchaseEnd::Queued) {
                // Ownership loss/destruction, not a computer cancellation command.
                g_Config.m_Timings[UNIT_ROBOT] = 100;
                factory->m_BS.TickTimer(99);
                MG_CHECK(!in_logic(built) && factory->m_BS.GetItemsCnt() == 1);
                MG_CHECK((side_resources(*side) == Cost{}));
                if (end == PurchaseEnd::Neutralized) {
                    factory->SetNeutral();
                    MG_CHECK(factory->GetSide() == 0 && factory->m_BS.GetItemsCnt() == 0);
                    factory->SetNeutral();
                }
                g_MatrixMap->StaticDelete(factory);
                MG_CHECK(side_resources(*side) == price && resources() == player_before);
                MG_CHECK(random::draw_count() == draws + 1);
                continue;
            }
            // A queued unit blocks the strategy before another proposal/debit/draw.
            for (int r = 0; r < MAX_RESOURCES; ++r) side->SetResourceAmount(static_cast<ERes>(r), price[r]);
            side->BuildRobot();
            MG_CHECK(factory->m_BS.GetItemsCnt() == 1 && factory->m_BS.GetTopItem() == built);
            MG_CHECK(side_resources(*side) == price && random::draw_count() == draws + 1);
        }
        MG_CHECK(resources() == player_before);
    }
}

void ai_purchase() { ai_production_purchase(); }
void ai_purchase_neutralized() { ai_production_purchase(PurchaseEnd::Neutralized); }
void ai_purchase_deleted() { ai_production_purchase(PurchaseEnd::Deleted); }

enum class StrategicEnd { Deploy, RemoveWaitingBase, RemovePaidBase };
void strategic_purchase(StrategicEnd ending) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int step : {10, 16}) {
        tests::BattleMaterialProfile materials;
        Session session({mode, 83, step, 2000, false, false, true});
        constructor_assets(true, 2);
        auto owned = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
            PLAYER_SIDE, BUILDING_BASE, 100, 100);
        auto *factory = owned.get();
        factory->m_TopPoint = D3DXVECTOR3(100, 100, 40);
        g_MatrixMap->AddObject(owned.release(), true);
        const auto release_core = [](SObjectCore *core) { core->Release(); };
        std::unique_ptr<SObjectCore, decltype(release_core)> base_core(factory->GetCore(DEBUG_CALL_INFO), release_core);
        auto &side = *g_MatrixMap->GetPlayerSide();
        side.m_Constructor->SetSide(PLAYER_SIDE);
        side.PGCalcStat();
        side.SetResourceForceUp(100);
        side.m_WaitResMul = 1;
        std::fill(std::begin(g_Config.m_Price), std::end(g_Config.m_Price), 0);
        g_Config.m_WeaponStrengthAI[RUK_WEAPON_REPAIR] = 50;
        for (int r = 0; r < MAX_RESOURCES; ++r) {
            g_Config.m_Price[WEAPON1_TITAN + (RUK_WEAPON_REPAIR - 1) * MAX_RESOURCES + r] = 60;
            g_Config.m_Timings[r] = 100;
            side.SetResourceAmount(static_cast<ERes>(r), 90);
        }
        g_Config.m_Timings[RESOURCE_BASE] = 100;
        g_Config.m_Timings[UNIT_ROBOT] = 100;
        struct DefinitionsGuard { ~DefinitionsGuard() { SSpecialBot::ClearAIRobotType(); } } definitions_guard;
        MG_CHECK(!SSpecialBot::m_AIRobotTypeList && SSpecialBot::m_AIRobotTypeCnt == 0);
        CBlockPar definitions;
        definitions.ParAdd(L"1", L"W,1S,R");
        definitions.ParAdd(L"1", L"W,1S,RR");
        SSpecialBot::LoadAIRobotType(definitions, g_MatrixMap->m_RobotWeaponMatrix);
        MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == 2);
        MG_CHECK(SSpecialBot::m_AIRobotTypeList[0].m_Strength == 100);
        MG_CHECK(SSpecialBot::m_AIRobotTypeList[1].m_Strength == 50);
        for (int r = 0; r < MAX_RESOURCES; ++r) {
            MG_CHECK(SSpecialBot::m_AIRobotTypeList[0].m_Resources[r] == 120);
            MG_CHECK(SSpecialBot::m_AIRobotTypeList[1].m_Resources[r] == 60);
        }
        const auto draws = random::draw_count();
        const auto build_stat = side.GetStatValue(STAT_ROBOT_BUILD);
        side.BuildRobot();
        MG_CHECK(side.m_WaitResForBuildRobot == 0 && side.m_BuildRobotLast == -1);
        MG_CHECK(factory->m_BS.GetItemsCnt() == 0 && resources() == (Cost{90, 90, 90, 90}));
        MG_CHECK(random::draw_count() == draws + 1);
        std::cout << "strategic ending=" << int(ending) << ' '
                  << session.metadata("synthetic-strategic-purchase-v1") << '\n';
        if (ending == StrategicEnd::RemoveWaitingBase) {
            g_MatrixMap->StaticDelete(factory);
            MG_CHECK(!base_core->m_Object && side.GetMaxSideRobots() == 0);
            const auto before = resources();
            for (int frame = 0; frame < 100; ++frame) {
                session.advance();
                side.BuildRobot();
                MG_CHECK(resources() == before && side.m_WaitResForBuildRobot == 0);
                MG_CHECK(side.m_BuildRobotLast == -1 && side.GetStatValue(STAT_ROBOT_BUILD) == build_stat);
            }
            // Losing the only base removes capacity; this guard precedes wait cancellation.
            MG_CHECK(random::draw_count() == draws + 1);
            continue;
        }
        int payouts = 0;
        while (factory->m_BS.GetItemsCnt() == 0 && session.time() < 2000) {
            const auto before = resources();
            session.advance();
            MG_CHECK(base_core->m_Object == factory);
            const auto earned = resources();
            const int change = earned[0] - before[0];
            MG_CHECK(change == 0 || change == 3);
            for (int r = 0; r < MAX_RESOURCES; ++r) MG_CHECK(earned[r] == before[r] + change);
            payouts += change / 3;
            side.BuildRobot();
            if (payouts < 10) {
                MG_CHECK(factory->m_BS.GetItemsCnt() == 0 && resources() == earned);
                MG_CHECK(side.m_WaitResForBuildRobot == 0 && side.m_BuildRobotLast == -1);
            }
        }
        MG_CHECK(payouts == 10 && factory->m_BS.GetItemsCnt() == 1);
        // Sixteen-millisecond frames contain 10+6 ms object ticks. Income
        // discards overshoot, yielding two payouts per thirteen such frames.
        MG_CHECK(session.time() == (step == 10 ? 1000 : 1040));
        MG_CHECK(resources() == Cost{} && side.m_WaitResForBuildRobot == -1 && side.m_BuildRobotLast == 0);
        MG_CHECK(random::draw_count() == draws + 1); // The retained plan needs no new lottery.
        auto *built = factory->m_BS.GetTopItem()->AsRobot();
        std::unique_ptr<SObjectCore, decltype(release_core)> robot_core(built->GetCore(DEBUG_CALL_INFO), release_core);
        MG_CHECK(!in_logic(built) && built->GetBase() == factory && built->GetSide() == PLAYER_SIDE);
        MG_CHECK(built->m_UnitCnt == 4);
        for (int unit = 2; unit < 4; ++unit)
            MG_CHECK(built->m_Unit[unit].m_Type == MRT_WEAPON &&
                     built->m_Unit[unit].u1.s1.m_Kind == RUK_WEAPON_REPAIR);
        Cost constructor_price{};
        side.m_Constructor->GetConstructionPrice(constructor_price.data());
        MG_CHECK(constructor_price == (Cost{120, 120, 120, 120}));
        side.BuildRobot();
        MG_CHECK(factory->m_BS.GetItemsCnt() == 1 && factory->m_BS.GetTopItem() == built);
        MG_CHECK(resources() == Cost{} && random::draw_count() == draws + 1);
        if (ending == StrategicEnd::RemovePaidBase) {
            g_MatrixMap->StaticDelete(factory);
            MG_CHECK(!base_core->m_Object && !robot_core->m_Object);
            MG_CHECK(resources() == constructor_price);
            MG_CHECK(side.GetStatValue(STAT_ROBOT_BUILD) == build_stat);
            for (int frame = 0; frame < 100; ++frame) {
                session.advance();
                side.BuildRobot();
                MG_CHECK(!base_core->m_Object && !robot_core->m_Object);
                MG_CHECK(resources() == constructor_price);
                MG_CHECK(side.m_WaitResForBuildRobot == -1 && side.m_BuildRobotLast == 0);
                MG_CHECK(side.GetStatValue(STAT_ROBOT_BUILD) == build_stat);
            }
            MG_CHECK(random::draw_count() == draws + 1);
            continue;
        }
        const int purchased = session.time();
        int spawned = -1, opened = -1, detached = -1, closed = -1;
        while (session.time() < purchased + 8000 && closed < 0) {
            const auto before = resources();
            session.advance();
            MG_CHECK(base_core->m_Object == factory && robot_core->m_Object == built);
            const auto after = resources();
            const int change = after[0] - before[0];
            MG_CHECK(change == 0 || change == 3);
            for (int r = 0; r < MAX_RESOURCES; ++r) MG_CHECK(after[r] == before[r] + change);
            if (spawned < 0 && in_logic(built)) {
                spawned = session.time();
                MG_CHECK(factory->m_BS.GetItemsCnt() == 0 && factory->IsSpawningBot());
                MG_CHECK(built->m_CurrState == ROBOT_IN_SPAWN && built->IsDisableManual());
                MG_CHECK(side.GetStatValue(STAT_ROBOT_BUILD) == build_stat + 1);
            }
            if (opened < 0 && factory->m_State == BASE_OPENED) opened = session.time();
            if (spawned >= 0 && detached < 0 && built->GetBase() == nullptr) {
                detached = session.time();
                const D3DXVECTOR2 delta(built->m_PosX - factory->m_Pos.x, built->m_PosY - factory->m_Pos.y);
                MG_CHECK(D3DXVec2LengthSq(&delta) >= BASE_DIST * BASE_DIST);
                MG_CHECK(built->m_CurrState == ROBOT_SUCCESSFULLY_BUILD && !built->IsDisableManual());
                MG_CHECK(factory->m_State == BASE_CLOSING && !factory->IsSpawningBot());
            }
            if (detached >= 0 && factory->m_State == BASE_CLOSED) closed = session.time();
        }
        std::cout << "purchase=" << purchased << " spawned=" << spawned << " opened=" << opened
                  << " detached=" << detached << " closed=" << closed << '\n';
        MG_CHECK(spawned >= purchased + 100 && spawned < purchased + 100 + step);
        MG_CHECK(opened > spawned && detached > opened && closed > detached);
        MG_CHECK(opened == (step == 10 ? 2340 : 2400));
        MG_CHECK(detached == (step == 10 ? 4590 : 4656));
        MG_CHECK(closed == (step == 10 ? 5840 : 5904));
        MG_CHECK(base_core->m_Object == factory && robot_core->m_Object == built);
        MG_CHECK(in_logic(built) && built->IsLiveRobot() && built->GetSide() == PLAYER_SIDE);
        MG_CHECK(built->GetBase() == nullptr && factory->m_BS.GetItemsCnt() == 0);
        MG_CHECK(side.GetStatValue(STAT_ROBOT_BUILD) == build_stat + 1);
        const int final_balance = step == 10 ? 144 : 138;
        MG_CHECK(resources() == (Cost{final_balance, final_balance, final_balance, final_balance}));
        MG_CHECK(random::draw_count() == draws + 4);
    }
}
void strategic_deployment() { strategic_purchase(StrategicEnd::Deploy); }
void strategic_wait_base_removed() { strategic_purchase(StrategicEnd::RemoveWaitingBase); }
void strategic_paid_base_removed() { strategic_purchase(StrategicEnd::RemovePaidBase); }

void player_constructor_purchase() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int requested : {1, 3}) for (bool cancel : {false, true}) {
        tests::BattleMaterialProfile material_profile;
        tests::battle::Profile profile;
        profile.mode = mode;
        profile.seed = 83;
        profile.audio = false;
        profile.initially_selected = false;
        profile.unavailable_icon_targets = true;
        Session session(profile);
        constructor_assets();
        std::cout << "requested=" << requested << " cancel=" << cancel << " "
                  << session.metadata("synthetic-player-constructor-v1") << '\n';
        MG_CHECK(g_ConfigHistory == nullptr);
        CHistory history;
        struct HistoryReset { ~HistoryReset() { g_ConfigHistory = nullptr; } } history_reset;
        g_ConfigHistory = &history;
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto &constructor = *side.m_Constructor;
        auto owned = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
            PLAYER_SIDE, BUILDING_BASE, 100, 100);
        auto *factory = owned.get();
        g_MatrixMap->AddObject(factory, true);
        owned.release();
        constructor.SetSide(PLAYER_SIDE);
        constructor.SetBase(factory);
        constructor.OperateUnit(MRT_CHASSIS, RUK_CHASSIS_WHEEL);
        constructor.OperateUnit(MRT_ARMOR, RUK_ARMOR_BIREX);
        auto &config = side.m_ConstructPanel->m_Configs[0];
        config.m_Chassis.m_nKind = RUK_CHASSIS_WHEEL;
        config.m_Hull.m_Unit.m_nKind = RUK_ARMOR_BIREX;
        const Cost price{32, 34, 36, 38};
        Cost actual{};
        constructor.GetConstructionPrice(actual.data());
        MG_CHECK(actual == price);
        for (int r = 0; r < MAX_RESOURCES; ++r)
            side.SetResourceAmount(static_cast<ERes>(r), requested * price[r]);
        const auto before = resources();
        g_IFaceList->m_RCountControl->Reset();
        for (int i = 1; i < requested; ++i) g_IFaceList->m_RCountControl->Inc();
        g_IFaceList->m_RCountControl->CheckUp();
        MG_CHECK(g_IFaceList->m_RCountControl->GetCounter() == requested);
        const auto draws = random::draw_count();
        constructor.RemoteBuild(nullptr); // The constructor's production build-button callback.
        MG_CHECK(factory->m_BS.GetItemsCnt() == requested && resources() == Cost{});
        MG_CHECK(random::draw_count() == draws && session.rejected_icon_targets() == requested);
        MG_CHECK(g_IFaceList->m_RCountControl->GetCounter() == 0);
        MG_CHECK(!side.m_ConstructPanel->IsActive());
        MG_CHECK(history.m_FirstConfig == history.m_LastConfig && history.m_FirstConfig);
        MG_CHECK(history.m_FirstConfig->m_Chassis.m_nKind == RUK_CHASSIS_WHEEL);
        MG_CHECK(history.m_FirstConfig->m_Hull.m_Unit.m_nKind == RUK_ARMOR_BIREX);
        std::vector<CMatrixRobotAI *> robots;
        struct ReleaseCore { void operator()(SObjectCore *core) const { core->Release(); } };
        std::vector<std::unique_ptr<SObjectCore, ReleaseCore>> robot_cores;
        for (auto *item = factory->m_BS.GetTopItem(); item; item = item->m_NextStackItem) {
            auto *robot = item->AsRobot();
            MG_CHECK(robot->GetSide() == PLAYER_SIDE && robot->GetBase() == factory);
            MG_CHECK(!in_logic(robot) && robot->m_UnitCnt == 2);
            MG_CHECK(!robot->GetBigTexture() && !robot->GetMedTexture());
            robots.push_back(robot);
            robot_cores.emplace_back(robot->GetCore(DEBUG_CALL_INFO));
        }
        MG_CHECK(robots.size() == static_cast<std::size_t>(requested));
        if (cancel) {
            for (int i = 0; i < requested; ++i) {
                MG_CHECK(factory->m_BS.GetTopItem() == robots[i]);
                factory->m_BS.DeleteItem(1);
                MG_CHECK(robot_cores[i]->m_Object == nullptr);
                for (int r = 0; r < MAX_RESOURCES; ++r)
                    MG_CHECK(resources()[r] == (i + 1) * price[r]);
            }
            MG_CHECK(resources() == before && factory->m_BS.GetItemsCnt() == 0);
        } else {
            g_Config.m_Timings[UNIT_ROBOT] = 100;
            g_Config.m_Timings[RESOURCE_BASE] = 100000;
            const int built = side.GetStatValue(STAT_ROBOT_BUILD);
            while (session.time() < 20000 &&
                   (factory->m_BS.GetItemsCnt() || factory->IsSpawningBot() || factory->m_State != BASE_CLOSED))
                session.advance();
            MG_CHECK(factory->m_BS.GetItemsCnt() == 0 && !factory->IsSpawningBot());
            MG_CHECK(factory->m_State == BASE_CLOSED && resources() == Cost{});
            MG_CHECK(side.GetStatValue(STAT_ROBOT_BUILD) == built + requested);
            for (std::size_t i = 0; i < robots.size(); ++i) {
                auto *robot = robots[i];
                MG_CHECK(robot_cores[i]->m_Object == robot);
                MG_CHECK(in_logic(robot) && robot->GetBase() == nullptr);
                MG_CHECK(robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD && !robot->IsDisableManual());
            }
        }
        constructor.SetBase(nullptr);
    }
}

void constructor_assembly() {
    // Two sessions per RNG also exercise destruction/recreation of cached skins.
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int repeat = 0; repeat < 2; ++repeat) {
        tests::BattleMaterialProfile material_profile;
        Session session({mode, 83, 10, 1000, false, false});
        std::cout << "Constructor profile: " << session.metadata("synthetic-material-boxes-v1") << '\n';
        constructor_assets();
        auto factory = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
            2, BUILDING_BASE, 100, 100);
        CConstructor constructor;
        constructor.SetSide(2);
        constructor.SetBase(factory.get());
        constructor.OperateUnit(MRT_CHASSIS, RUK_CHASSIS_WHEEL);
        constructor.OperateUnit(MRT_ARMOR, RUK_ARMOR_BIREX);
        Cost price{};
        constructor.GetConstructionPrice(price.data());
        MG_CHECK((price == Cost{32, 34, 36, 38}));
        MG_CHECK(constructor.GetConstructionStructure() == 150);
        const auto before = resources();
        const auto draws = random::draw_count();
        Cost computer_before{};
        auto *computer = g_MatrixMap->GetSideById(2);
        for (int i = 0; i < MAX_RESOURCES; ++i)
            computer_before[i] = computer->GetResourcesAmount(static_cast<ERes>(i));
        const D3DXVECTOR3 directions[] = {{0, 1, 0}, {-1, 0, 0}, {0, -1, 0}, {1, 0, 0}};
        for (int angle = 0; angle < 4; ++angle) {
            factory->m_Angle = angle;
            constructor.StackRobot(nullptr, angle == 3 ? -1 : angle);
            MG_CHECK(factory->m_BS.GetItemsCnt() == 1);
            auto *built = factory->m_BS.GetTopItem()->AsRobot();
            MG_CHECK(built->GetSide() == 2 && built->GetBase() == factory.get());
            MG_CHECK(built->GetTeam() == (angle == 3 ? 0 : angle));
            MG_CHECK(built->IsCrazy() == (angle == 3));
            MG_CHECK(built->m_PosX == 100 && built->m_PosY == 100);
            MG_CHECK(built->m_Forward == directions[angle] && built->m_HullForward == directions[angle]);
            MG_CHECK(built->m_Name == L"Synthetic hull wheels");
            MG_CHECK(built->m_UnitCnt == 2);
            MG_CHECK(built->m_Unit[0].m_Type == MRT_CHASSIS && built->m_Unit[0].u1.s1.m_Kind == RUK_CHASSIS_WHEEL);
            MG_CHECK(built->m_Unit[1].m_Type == MRT_ARMOR && built->m_Unit[1].u1.s1.m_Kind == RUK_ARMOR_BIREX);
            for (int i = 0; i < 2; ++i) {
                auto *graph = built->m_Unit[i].m_Graph;
                MG_CHECK(graph && graph->GetSkin());
                const auto *skin = static_cast<const SMatrixSkin *>(graph->GetSkin());
                MG_CHECK(skin->m_Tex->GetPixelColor(0, 0) == 0xff204060);
                MG_CHECK(skin->m_Tex->GetSizeX() == 2 && skin->m_Tex->GetSizeY() == 2);
            }
            MG_CHECK(!in_logic(built));
            for (int i = 0; i < MAX_RESOURCES; ++i)
                MG_CHECK(computer->GetResourcesAmount(static_cast<ERes>(i)) == computer_before[i] + angle * price[i]);
            factory->m_BS.DeleteItem(1);
            MG_CHECK(factory->m_BS.GetItemsCnt() == 0);
        }
        // StackRobot queues a design; payment belongs to its caller.
        MG_CHECK(resources() == before);
        for (int i = 0; i < MAX_RESOURCES; ++i)
            MG_CHECK(computer->GetResourcesAmount(static_cast<ERes>(i)) == computer_before[i] + 4 * price[i]);
        MG_CHECK(random::draw_count() == draws);
    }
}

void constructor_capacity() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        tests::BattleMaterialProfile material_profile;
        Session session({mode, 83, 10, 1000, false, false});
        constructor_assets();
        auto factory = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
            2, BUILDING_BASE, 100, 100);
        CConstructor constructor;
        constructor.SetSide(2);
        constructor.StackRobot(nullptr); // No base.
        MG_CHECK(factory->m_BS.GetItemsCnt() == 0);
        constructor.SetBase(factory.get());
        constructor.StackRobot(nullptr); // Empty design.
        MG_CHECK(factory->m_BS.GetItemsCnt() == 0);
        constructor.OperateUnit(MRT_CHASSIS, RUK_CHASSIS_WHEEL);
        constructor.OperateUnit(MRT_ARMOR, RUK_ARMOR_BIREX);
        std::vector<CMatrixRobotAI *> queued;
        const auto draws = random::draw_count();
        for (int i = 0; i < 6; ++i) {
            constructor.StackRobot(nullptr, 1);
            MG_CHECK(factory->m_BS.GetItemsCnt() == i + 1);
        }
        for (auto *item = factory->m_BS.GetTopItem(); item; item = item->m_NextStackItem)
            queued.push_back(item->AsRobot());
        MG_CHECK(queued.size() == 6);
        constructor.StackRobot(nullptr, 2); // Reject a seventh enqueue.
        MG_CHECK(factory->m_BS.GetItemsCnt() == 6 && factory->m_BS.GetTopItem() == queued.front());
        MG_CHECK(random::draw_count() == draws);
        for (int i = 0; i < 6; ++i) {
            MG_CHECK(factory->m_BS.GetTopItem() == queued[i]);
            MG_CHECK(queued[i]->GetTeam() == 1 && !in_logic(queued[i]));
            factory->m_BS.DeleteItem(1);
        }
        MG_CHECK(factory->m_BS.GetItemsCnt() == 0);
    }
}

void constructor_deployment() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        tests::BattleMaterialProfile material_profile;
        Session session({mode, 83, 10, 1000, false, false});
        constructor_assets();
        auto factory = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
            2, BUILDING_BASE, 100, 100);
        CConstructor constructor;
        constructor.SetSide(2);
        constructor.SetBase(factory.get());
        SSpecialBot design;
        design.m_Chassis.m_nKind = RUK_CHASSIS_WHEEL;
        design.m_Armor.m_Unit.m_nKind = RUK_ARMOR_BIREX;
        design.m_Team = 1;
        constructor.BuildSpecialBot(design);
        MG_CHECK(factory->m_BS.GetItemsCnt() == 1);
        auto *built = factory->m_BS.GetTopItem()->AsRobot();
        auto *side = g_MatrixMap->GetSideById(2);
        const auto statistic = side->GetStatValue(STAT_ROBOT_BUILD);
        const auto draws = random::draw_count();
        g_Config.m_Timings[UNIT_ROBOT] = 100;
        factory->m_BS.TickTimer(99);
        MG_CHECK(!in_logic(built) && factory->m_BS.GetItemsCnt() == 1);
        factory->m_BS.TickTimer(1);
        MG_CHECK(in_logic(built) && factory->m_BS.GetItemsCnt() == 0);
        MG_CHECK(built->m_CurrState == ROBOT_IN_SPAWN && built->GetBase() == factory.get());
        MG_CHECK(built->GetTeam() == 0 && built->m_UnitCnt == 2); // Spawn chooses the first empty computer team.
        MG_CHECK(factory->m_State == BASE_OPENING && factory->IsSpawningBot());
        MG_CHECK(side->GetStatValue(STAT_ROBOT_BUILD) == statistic + 1);
        MG_CHECK(random::draw_count() == draws); // Computer deployment has no player sound choice.
        g_MatrixMap->StaticDelete(built);
    }
}
tests::EnginePtr<CMatrixBuilding> base() {
    return tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
        PLAYER_SIDE, BUILDING_BASE, 100, 100);
}
CMatrixRobotAI *enqueue(CMatrixBuilding &factory) {
    auto robot = tests::make_engine_object<CMatrixRobotAI>();
    robot->m_Side = PLAYER_SIDE;
    robot->m_PosX = factory.m_Pos.x;
    robot->m_PosY = factory.m_Pos.y;
    MG_CHECK(factory.m_Angle >= 0 && factory.m_Angle < 4);
    const D3DXVECTOR3 directions[] = {{0, 1, 0}, {-1, 0, 0}, {0, -1, 0}, {1, 0, 0}};
    robot->m_Forward = robot->m_HullForward = directions[factory.m_Angle];
    robot->UnitInsert(0, MRT_CHASSIS, RUK_CHASSIS_WHEEL);
    robot->m_Unit[0].m_Graph = HNew(g_MatrixHeap) CVectorObjectAnim;
    robot->m_Unit[0].m_Graph->Init(source_robot()->m_Unit[0].m_Graph->VO(), nullptr);
    robot->m_Unit[0].m_Graph->FirstFrame();
    robot->m_Unit[0].m_Graph->SetAnimByName(ANIMATION_NAME_STAY);
    robot->CalcRobotMass();
    robot->m_GroupSpeed = robot->GetMaxSpeed();
    robot->MapPosCalc();
    robot->SetBase(&factory);
    robot->RNeed(MR_Matrix);
    auto *result = robot.get();
    const int count = factory.m_BS.GetItemsCnt();
    factory.m_BS.AddItem(result);
    const bool accepted = factory.m_BS.GetItemsCnt() == count + 1;
    if (accepted) robot.release(); // Transfer before any subsequent throwing check.
    MG_CHECK(accepted);
    MG_CHECK(!in_logic(result));
    return result;
}

void queue_deployment() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode, 83, 10, 1000, false, false});
        auto factory = base();
        g_Config.m_Timings[UNIT_ROBOT] = 100;
        const Cost before = resources();
        const auto built = g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_BUILD);
        auto *first = enqueue(*factory);
        auto *second = enqueue(*factory);
        factory->m_BS.TickTimer(99);
        MG_CHECK(!in_logic(first) && !in_logic(second));
        MG_CHECK(factory->m_BS.GetItemsCnt() == 2);
        MG_CHECK(!factory->IsSpawningBot());
        const auto draws = random::draw_count();
        factory->m_BS.TickTimer(1);
        MG_CHECK(in_logic(first) && !in_logic(second));
        MG_CHECK(first->m_CurrState == ROBOT_IN_SPAWN);
        MG_CHECK(first->GetBase() == factory.get());
        MG_CHECK(factory->IsSpawningBot() && factory->m_State == BASE_OPENING);
        MG_CHECK(factory->m_BS.GetItemsCnt() == 1 && factory->m_BS.GetTopItem() == second);
        MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_BUILD) == built + 1);
        MG_CHECK(random::draw_count() == draws + 1);
        MG_CHECK(resources() == before);
        factory->m_BS.TickTimer(200);
        MG_CHECK(!in_logic(second) && factory->m_BS.GetItemsCnt() == 1);
        factory->ResetSpawningBot();
        factory->m_State = BASE_CLOSED;
        factory->m_BS.TickTimer(0);
        MG_CHECK(in_logic(second) && factory->m_BS.GetItemsCnt() == 0);
        MG_CHECK(second->m_CurrState == ROBOT_IN_SPAWN);
        MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_BUILD) == built + 2);
        MG_CHECK(resources() == before);
        g_MatrixMap->StaticDelete(first);
        g_MatrixMap->StaticDelete(second);
    }
}

void base_exit() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode, 83, 10, 1500, false, false});
        auto owned = base();
        auto *factory = owned.get();
        g_MatrixMap->AddObject(factory, true);
        owned.release();
        g_Config.m_Timings[UNIT_ROBOT] = 100;
        g_Config.m_Timings[RESOURCE_BASE] = 100000;
        const Cost before = resources();
        auto *robot = enqueue(*factory);
        for (int i = 0; i < 9; ++i) session.advance();
        MG_CHECK(!in_logic(robot));
        session.advance();
        MG_CHECK(in_logic(robot) && robot->m_CurrState == ROBOT_IN_SPAWN);
        MG_CHECK(factory->m_State == BASE_OPENING && factory->IsSpawningBot());
        MG_CHECK(robot->IsDisableManual());
        int opened = -1, moving_out = -1, detached = -1, closed = -1;
        float detach_distance = -1;
        for (int i = 0; i < 1000 && closed < 0; ++i) {
            session.advance();
            if (opened < 0 && factory->m_State == BASE_OPENED) opened = session.time();
            if (moving_out < 0 && robot->m_CurrState == ROBOT_BASE_MOVEOUT) moving_out = session.time();
            if (detached < 0 && robot->GetBase() == nullptr) {
                detached = session.time();
                const D3DXVECTOR2 distance(robot->m_PosX - factory->m_Pos.x, robot->m_PosY - factory->m_Pos.y);
                detach_distance = D3DXVec2LengthSq(&distance);
                MG_CHECK(detach_distance >= BASE_DIST * BASE_DIST);
                MG_CHECK(robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
                MG_CHECK(!robot->IsDisableManual() && !factory->IsSpawningBot());
                MG_CHECK(factory->m_State == BASE_CLOSING);
            }
            if (detached >= 0 && factory->m_State == BASE_CLOSED) closed = session.time();
        }
        std::cout << "base lifecycle mode=" << int(mode) << " opened=" << opened << " moving_out=" << moving_out
                  << " detached=" << detached << " closed=" << closed << " distance_squared=" << detach_distance << '\n';
        MG_CHECK(opened == 1340 && moving_out == 1340);
        MG_CHECK(detached == 3590 && closed == 4840);
        MG_CHECK(robot->GetBase() == nullptr);
        MG_CHECK(robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        MG_CHECK(!robot->IsDisableManual());
        MG_CHECK(!factory->IsSpawningBot());
        MG_CHECK(factory->m_State == BASE_CLOSED && factory->m_BaseFloor == 0);
        MG_CHECK(resources() == before);
        std::vector<SMatrixMapUnit *> footprint;
        for (int y = 0; y < g_MatrixMap->m_Size.y; ++y)
            for (int x = 0; x < g_MatrixMap->m_Size.x; ++x) {
                auto *cell = g_MatrixMap->UnitGet(x, y);
                if (cell->m_Base == factory) footprint.push_back(cell);
            }
        MG_CHECK(footprint.size() == 9);
        g_MatrixMap->StaticDelete(factory);
        for (const auto *cell : footprint) MG_CHECK(cell->m_Base == nullptr);
    }
}

std::vector<SMatrixMapUnit *> registered_cells(CMatrixBuilding *base) {
    std::vector<SMatrixMapUnit *> result;
    for (int y = 0; y < g_MatrixMap->m_Size.y; ++y)
        for (int x = 0; x < g_MatrixMap->m_Size.x; ++x) {
            auto *cell = g_MatrixMap->UnitGet(x, y);
            if (cell->m_Base == base) result.push_back(cell);
        }
    MG_CHECK(result.size() == 9);
    return result;
}

void base_terrain_cleanup() {
    for (const auto &point : {CPoint(100, 100), CPoint(100, 300), CPoint(300, 100),
                              CPoint(30, 30), CPoint(30, 600), CPoint(600, 30), CPoint(600, 600)}) {
        Session session;
        auto &geometry = *source_robot()->m_Unit[0].m_Graph->VO();
        auto neighbor = tests::make_battle_building(geometry, PLAYER_SIDE, BUILDING_BASE, 160, 300);
        auto *live_neighbor = neighbor.get();
        g_MatrixMap->AddObject(neighbor.release(), true);
        auto neighbor_cells = registered_cells(live_neighbor);
        for (int repeat = 0; repeat < 3; ++repeat) {
            auto owner = tests::make_battle_building(geometry, PLAYER_SIDE, BUILDING_BASE,
                float(point.x), float(point.y));
            auto *base = owner.get();
            g_MatrixMap->AddObject(owner.release(), true);
            const auto cells = registered_cells(base);
            g_MatrixMap->StaticDelete(base);
            // Inspect only live terrain, never a retired building or its members.
            for (const auto *cell : cells) MG_CHECK(cell->m_Base == nullptr);
            for (const auto *cell : neighbor_cells) MG_CHECK(cell->m_Base == live_neighbor);
        }
        g_MatrixMap->StaticDelete(live_neighbor);
        for (const auto *cell : neighbor_cells) MG_CHECK(cell->m_Base == nullptr);
    }
}

void base_terrain_teardown() {
    Session session;
    auto &geometry = *source_robot()->m_Unit[0].m_Graph->VO();
    for (const auto &point : {CPoint(100, 300), CPoint(300, 100)}) {
        auto owner = tests::make_battle_building(geometry, PLAYER_SIDE, BUILDING_BASE,
            float(point.x), float(point.y));
        registered_cells(owner.get());
        g_MatrixMap->AddObject(owner.release(), true);
    }
    // Map::Clear calls StaticClear before UnitClear. Inspect this production
    // teardown boundary while the terrain storage is still alive.
    g_MatrixMap->StaticClear();
    for (int y = 0; y < g_MatrixMap->m_Size.y; ++y)
        for (int x = 0; x < g_MatrixMap->m_Size.x; ++x)
            MG_CHECK(g_MatrixMap->UnitGet(x, y)->m_Base == nullptr);
}

void building_geometry() {
    Session session({random::Mode::ParkMiller, 83, 10, 1000, false, false});
    for (auto kind : {BUILDING_BASE, BUILDING_TITAN, BUILDING_PLASMA,
                      BUILDING_ELECTRONIC, BUILDING_ENERGY, BUILDING_REPAIR}) {
        auto building = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
            PLAYER_SIDE, kind, 100, 100);
        MG_CHECK(building->IsLiveBuilding() && building->GetSide() == PLAYER_SIDE);
        MG_CHECK(building->m_GGraph->IsAlreadyLoaded());
        MG_CHECK(building->GetRadius() > 0 && std::isfinite(building->GetRadius()));
        MG_CHECK(building->m_TurretsMax > 0);
        float distance = -1;
        MG_CHECK(building->Pick(D3DXVECTOR3(100, 100, 60), D3DXVECTOR3(0, 0, -1), &distance));
        // Triangle intersection uses reciprocal multiplication; allow four float
        // ulps at this 40-unit distance, without relaxing gameplay trajectories.
        MG_CHECK(std::abs(distance - (60 - (building->m_BuildZ + 20))) <= 0.0000152587890625f);
        auto *cell = g_MatrixMap->UnitGet(5, 5);
        MG_CHECK((cell->m_Base == building.get()) == (kind == BUILDING_BASE));
        building.reset();
        MG_CHECK(cell->m_Base == nullptr);
    }
    for (const auto &point : {CPoint(100, 0), CPoint(-1, 100), CPoint(10000, 100)}) {
        bool rejected = false;
        try {
            auto invalid = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
                PLAYER_SIDE, BUILDING_BASE, float(point.x), float(point.y));
        }
        catch (const std::invalid_argument &) { rejected = true; }
        MG_CHECK(rejected);
        for (int y = 0; y < g_MatrixMap->m_Size.y; ++y)
            for (int x = 0; x < g_MatrixMap->m_Size.x; ++x)
                MG_CHECK(g_MatrixMap->UnitGet(x, y)->m_Base == nullptr);
    }
    // Asymmetry is allowed when the entire registered footprint can be cleared.
    auto asymmetric = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
        PLAYER_SIDE, BUILDING_BASE, 100, 120);
    asymmetric.reset();
    for (int y = 0; y < g_MatrixMap->m_Size.y; ++y)
        for (int x = 0; x < g_MatrixMap->m_Size.x; ++x)
            MG_CHECK(g_MatrixMap->UnitGet(x, y)->m_Base == nullptr);
}

void maintenance_clock() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session({mode, 83, step, 1000, false, false});
        g_MatrixMap->SetMaintenanceTime(32);
        g_MatrixMap->Pause(true);
        session.advance();
        session.advance();
        MG_CHECK(session.time() == 0 && g_MatrixMap->BeforeMaintenanceTime() == 32);
        g_MatrixMap->Pause(false);
        int remaining = 32;
        for (int i = 1; i <= 5; ++i) {
            session.advance();
            remaining = std::max(0, remaining - step);
            MG_CHECK(session.time() == i * step);
            MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == remaining);
        }
        g_MatrixMap->SetMaintenanceTime(1);
        session.advance();
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 0);
    }
}

void scheduled_income() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (auto kind : {BUILDING_BASE, BUILDING_TITAN, BUILDING_PLASMA, BUILDING_ELECTRONIC, BUILDING_ENERGY}) {
        Session session({mode, 83, 10, 1000, false, false});
        auto owned = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
            PLAYER_SIDE, kind, 100, 100);
        auto *factory = owned.get();
        g_MatrixMap->AddObject(factory, true);
        owned.release();
        for (int timing : {RESOURCE_BASE, RESOURCE_TITAN, RESOURCE_PLASMA, RESOURCE_ELECTRONICS, RESOURCE_ENERGY})
            g_Config.m_Timings[timing] = 100;
        g_MatrixMap->GetPlayerSide()->SetResourceForceUp(100);
        const auto before = resources();
        for (int i = 0; i < 9; ++i) session.advance();
        MG_CHECK(resources() == before);
        session.advance();
        const auto paid = resources();
        for (int r = 0; r < MAX_RESOURCES; ++r) {
            const bool matching = (kind == BUILDING_TITAN && r == TITAN) ||
                (kind == BUILDING_PLASMA && r == PLASMA) ||
                (kind == BUILDING_ELECTRONIC && r == ELECTRONICS) ||
                (kind == BUILDING_ENERGY && r == ENERGY);
            MG_CHECK(paid[r] == before[r] + (kind == BUILDING_BASE ? RESOURCES_INCOME_BASE :
                                            matching ? RESOURCES_INCOME : 0));
        }
        factory->SetNeutral();
        for (int i = 0; i < 20; ++i) session.advance();
        MG_CHECK(resources() == paid);
    }
}

void paused_production() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int pause_at : {90, 500}) {
        Session session({mode, 83, 10, 1500, false, false});
        auto owned = base();
        auto *factory = owned.get();
        g_MatrixMap->AddObject(factory, true);
        owned.release();
        g_Config.m_Timings[UNIT_ROBOT] = 100;
        g_Config.m_Timings[RESOURCE_BASE] = 100000;
        const auto before = resources();
        const int initial_built = g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_BUILD);
        auto *robot = enqueue(*factory);
        while (session.time() < pause_at) session.advance();
        MG_CHECK(in_logic(robot) == (pause_at >= 100));
        const auto state = factory->m_State;
        const float floor = factory->m_BaseFloor;
        const auto draws = random::draw_count();
        const int count = factory->m_BS.GetItemsCnt();
        const float x = robot->m_PosX, y = robot->m_PosY;
        g_MatrixMap->Pause(true);
        for (int i = 0; i < 50; ++i) session.advance();
        MG_CHECK(session.time() == pause_at && random::draw_count() == draws);
        MG_CHECK(factory->m_State == state && factory->m_BaseFloor == floor);
        MG_CHECK(factory->m_BS.GetItemsCnt() == count);
        MG_CHECK(robot->m_PosX == x && robot->m_PosY == y);
        MG_CHECK(resources() == before);
        g_MatrixMap->Pause(false);
        int opened = -1, detached = -1, closed = -1;
        while (session.time() < 5000 && closed < 0) {
            session.advance();
            if (opened < 0 && factory->m_State == BASE_OPENED) opened = session.time();
            if (detached < 0 && robot->GetBase() == nullptr) detached = session.time();
            if (detached >= 0 && factory->m_State == BASE_CLOSED) closed = session.time();
        }
        MG_CHECK(opened == 1340 && detached == 3590 && closed == 4840);
        MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_BUILD) == initial_built + 1);
        MG_CHECK(resources() == before && factory->m_BS.GetItemsCnt() == 0);
    }
}

void cancel_waiting_during_spawn() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode, 83, 10, 1500, false, false});
        auto owned = base();
        auto *factory = owned.get();
        g_MatrixMap->AddObject(factory, true);
        owned.release();
        g_Config.m_Timings[UNIT_ROBOT] = 100;
        g_Config.m_Timings[RESOURCE_BASE] = 100000;
        for (int r = 0; r < MAX_RESOURCES; ++r) g_Config.m_Price[CHASSIS2_TITAN + r] = 11 + r;
        const auto before = resources();
        const int built = g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_BUILD);
        auto *first = enqueue(*factory);
        auto *waiting = enqueue(*factory);
        while (session.time() < 500) session.advance();
        MG_CHECK(in_logic(first) && !in_logic(waiting));
        MG_CHECK(factory->m_State == BASE_OPENING && factory->IsSpawningBot());
        MG_CHECK(factory->m_BS.GetTopItem() == waiting);
        factory->m_BS.DeleteItem(waiting);
        MG_CHECK(factory->m_BS.GetItemsCnt() == 0 && factory->m_BS.GetTopItem() == nullptr);
        MG_CHECK(first->GetBase() == factory && first->IsDisableManual());
        auto expected = before;
        for (int r = 0; r < MAX_RESOURCES; ++r) expected[r] += 11 + r;
        MG_CHECK(resources() == expected);
        while (session.time() < 4840) session.advance();
        MG_CHECK(first->GetBase() == nullptr && !first->IsDisableManual());
        MG_CHECK(factory->m_State == BASE_CLOSED && !factory->IsSpawningBot());
        MG_CHECK(g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_BUILD) == built + 1);
        factory->m_BS.ClearStack();
        MG_CHECK(resources() == expected);
    }
}

enum class LandingRejection { Occupied, Incompatible, OverBudget };
void reinforcement_landing_rejection(LandingRejection rejection) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode, 83, 10, 1000, false, false});
        auto factory = base();
        MG_CHECK(!g_MatrixMap->MaintenanceDisabled());
        g_Config.m_MaintenanceTime = 1000;
        g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1;
        g_MatrixMap->SetMaintenanceTime(0);
        auto *definitions = g_MatrixData->BlockGetAdd(PAR_SOURCE_FLYER_ORDERS)
            ->BlockGetAdd(PAR_SOURCE_FLYER_ORDERS_GIVE_BOT);
        // A real rejected proposal; no robot assembly is reached at this price.
        definitions->BlockGetAdd(L"131");
        const int places = g_MatrixMap->m_RN.m_PlaceCnt;
        MG_CHECK(places == 4);
        int objects = 0, robot_index = 0;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            ++objects;
            if (object->IsLiveRobot()) {
                object->AsRobot()->GetEnv()->m_Place = rejection == LandingRejection::Occupied ? robot_index : -1;
                ++robot_index;
            }
        }
        MG_CHECK(robot_index == places);
        if (rejection == LandingRejection::Incompatible)
            for (int p = 0; p < places; ++p) g_MatrixMap->m_RN.m_Place[p].m_Move = 31;
        const auto before = resources();
        const auto draws = random::draw_count();
        factory->Maintenance();
        MG_CHECK(resources() == before && factory->m_BS.GetItemsCnt() == 0);
        int after = 0;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) ++after;
        MG_CHECK(after == objects);
        std::cout << "landing rejection=" << int(rejection) << " cooldown=" << g_MatrixMap->BeforeMaintenanceTime()
                  << " draws=" << random::draw_count() - draws << '\n';
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 1000);
        MG_CHECK(random::draw_count() == draws + (rejection == LandingRejection::OverBudget ? 11 : 1));
        const auto after_draws = random::draw_count();
        factory->Maintenance();
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 1000 && random::draw_count() == after_draws);
    }
}
void reinforcement_occupied() { reinforcement_landing_rejection(LandingRejection::Occupied); }
void reinforcement_incompatible() { reinforcement_landing_rejection(LandingRejection::Incompatible); }
void reinforcement_over_budget() { reinforcement_landing_rejection(LandingRejection::OverBudget); }

const tests::Case cases[] = {{"game.production.ai_purchase", ai_purchase},
                           {"game.production.strategic_deployment", strategic_deployment},
                           {"game.production.strategic_wait_base_removed", strategic_wait_base_removed},
                           {"game.production.strategic_paid_base_removed", strategic_paid_base_removed},
                           {"game.production.player_purchase", player_constructor_purchase},
                           {"game.production.ai_purchase_neutralized", ai_purchase_neutralized},
                           {"game.production.ai_purchase_deleted", ai_purchase_deleted},
                           {"game.production.reinforcement_occupied", reinforcement_occupied},
                           {"game.production.reinforcement_incompatible", reinforcement_incompatible},
                           {"game.production.reinforcement_over_budget", reinforcement_over_budget},
                           {"game.production.paused_production", paused_production},
                           {"game.production.cancel_waiting_during_spawn", cancel_waiting_during_spawn},
                           {"game.production.queue_deployment", queue_deployment},
                           {"game.production.constructor_assembly", constructor_assembly},
                           {"game.production.constructor_capacity", constructor_capacity},
                           {"game.production.constructor_deployment", constructor_deployment},
                           {"game.production.base_exit", base_exit},
                           {"game.production.building_geometry", building_geometry},
                           {"game.production.base_terrain_cleanup", base_terrain_cleanup},
                           {"game.production.base_terrain_teardown", base_terrain_teardown},
                           {"game.production.maintenance_clock", maintenance_clock},
                           {"game.production.scheduled_income", scheduled_income}};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Production fixture test leaked tracked heap allocations.\n";
        result = EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
