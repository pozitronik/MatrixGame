// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "command_world.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObjectBuilding.hpp"
#include "Interface/CInterface.h"
#include <array>
#include <cmath>

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
tests::EnginePtr<CMatrixBuilding> base() {
    return tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
        PLAYER_SIDE, BUILDING_BASE, 100, 100);
}
CMatrixRobotAI *enqueue(CMatrixBuilding &factory) {
    auto robot = tests::make_engine_object<CMatrixRobotAI>();
    robot->m_Side = PLAYER_SIDE;
    robot->m_PosX = factory.m_Pos.x;
    robot->m_PosY = factory.m_Pos.y;
    robot->m_Forward = robot->m_HullForward = D3DXVECTOR3(1, 0, 0);
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
    factory.m_BS.AddItem(result);
    robot.release();
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
        bool moving_out = false;
        for (int i = 0; i < 1000 && robot->GetBase(); ++i) {
            session.advance();
            moving_out |= robot->m_CurrState == ROBOT_BASE_MOVEOUT;
        }
        MG_CHECK(moving_out);
        MG_CHECK(robot->GetBase() == nullptr);
        MG_CHECK(robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        MG_CHECK(!robot->IsDisableManual());
        MG_CHECK(!factory->IsSpawningBot());
        MG_CHECK(factory->m_State == BASE_CLOSING);
        for (int i = 0; i < 130; ++i) session.advance();
        MG_CHECK(factory->m_State == BASE_CLOSED && factory->m_BaseFloor == 0);
        MG_CHECK(resources() == before);
    }
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

const tests::Case cases[] = {{"game.production.queue_deployment", queue_deployment},
                           {"game.production.base_exit", base_exit},
                           {"game.production.building_geometry", building_geometry},
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
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
