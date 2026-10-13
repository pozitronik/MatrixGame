// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_flyer.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixRobot.hpp"
#include "test_support.hpp"
#include <array>
#include <memory>
#include <set>
#include <vector>

namespace {
using tests::battle::Session;
using Cost = std::array<int, MAX_RESOURCES>;
struct ReleaseCore { void operator()(SObjectCore *core) const { core->Release(); } };
using CoreLease = std::unique_ptr<SObjectCore, ReleaseCore>;
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
struct MaintenanceInput : CMatrixMapLogic {
    static void enable(CMatrixMapLogic &map) { map.*&MaintenanceInput::m_MaintenancePRC = 100; }
};
void unavailable_robot_icons() {
    for (int repeat = 0; repeat < 2; ++repeat) {
        Session session({random::Mode::ParkMiller, 83, 10, 1000, false, false, true});
        auto *robot = source_robot();
        MG_CHECK(session.rejected_icon_targets() == 0);
        const auto draws = random::draw_count();
        robot->CreateTextures();
        MG_CHECK(!robot->GetBigTexture() && !robot->GetMedTexture());
        MG_CHECK(session.rejected_icon_targets() == 1 && random::draw_count() == draws);
        robot->CreateTextures();
        MG_CHECK(!robot->GetBigTexture() && !robot->GetMedTexture());
        MG_CHECK(session.rejected_icon_targets() == 2);
        MG_CHECK(session.metadata().find("storage-device-unavailable-icons") != std::string::npos);
    }
    Session ordinary;
    MG_CHECK(ordinary.rejected_icon_targets() == 0);
    MG_CHECK(ordinary.metadata().find("unavailable-icons") == std::string::npos);
}

void reinforcement_delivery() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        tests::BattleMaterialProfile material_profile;
        Session session({mode, 83, 10, 6000, false, false, true});
        std::cout << session.metadata("synthetic-reinforcement-v1") << '\n';
        tests::install_battle_transport();
        auto factory = base();
        MG_CHECK(session.remove(2) && session.remove(4));
        MaintenanceInput::enable(*g_MatrixMap);
        g_Config.m_MaintenanceTime = 1000;
        g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1;
        g_MatrixMap->SetMaintenanceTime(0);
        const auto before = resources();
        const auto draws = random::draw_count();
        factory->Maintenance();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 1);
        auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
        MG_CHECK(carrier->CarryingRobot());
        auto *robot = static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
        CoreLease robot_core(robot->GetCore(DEBUG_CALL_INFO));
        CoreLease carrier_core(carrier->GetCore(DEBUG_CALL_INFO));
        MG_CHECK(robot->GetSide() == PLAYER_SIDE && robot->m_CurrState == ROBOT_CARRYING);
        MG_CHECK(robot->m_UnitCnt == 2 && robot->GetHitPoint() == 150);
        MG_CHECK(session.rejected_icon_targets() == 1);
        MG_CHECK(random::draw_count() == draws + 3);
        MG_CHECK(resources() == before && g_MatrixMap->BeforeMaintenanceTime() == 1000);
        const int place = robot->GetEnv()->m_Place;
        const int group = robot->GetGroupLogic();
        MG_CHECK(place >= 0 && place < g_MatrixMap->m_RN.m_PlaceCnt);
        MG_CHECK(group >= 0 && g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group].Order() == mpo_Attack);
        MG_CHECK(g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group].m_To == g_MatrixMap->m_RN.m_Place[place].m_Pos);
        MG_CHECK(!robot->GetBigTexture() && !robot->GetMedTexture());
        const auto after_request = random::draw_count();
        factory->Maintenance(); // Cooldown rejects a duplicate before selecting another proposal.
        MG_CHECK(random::draw_count() == after_request && g_MatrixMap->m_AD_Obj_cnt == 1);
        int released = -1, landed = -1;
        while (g_MatrixMap->m_AD_Obj_cnt && session.frame() < 5900) {
            session.advance();
            MG_CHECK(robot_core->m_Object == robot);
            if (released < 0 && robot->m_CurrState != ROBOT_CARRYING) released = session.time();
            if (landed < 0 && released >= 0 && robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD) landed = session.time();
        }
        std::cout << "Reinforcement release=" << released << " landed=" << landed << " done=" << session.time() << '\n';
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && carrier_core->m_Object == nullptr);
        MG_CHECK(released == (mode == random::Mode::ParkMiller ? 4470 : 4960));
        MG_CHECK(landed == (mode == random::Mode::ParkMiller ? 4790 : 5280));
        MG_CHECK(session.time() == (mode == random::Mode::ParkMiller ? 8750 : 9230));
        MG_CHECK(in_logic(robot) && robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        MG_CHECK(resources() == before);
    }
}

void reinforcement_reservations() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int score : {60, 100, 130}) {
        tests::BattleMaterialProfile material_profile;
        Session session({mode, 83, 10, 6000, false, false, true});
        tests::install_battle_transport(score);
        auto factory = base();
        // All four places are otherwise free; one live robot reserves place zero.
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsLiveRobot()) object->AsRobot()->GetEnv()->m_Place = -1;
        source_robot()->GetEnv()->m_Place = 0;
        MaintenanceInput::enable(*g_MatrixMap);
        g_Config.m_MaintenanceTime = 1000;
        g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1.5f;
        g_MatrixMap->SetMaintenanceTime(0);
        const auto before = resources();
        const auto draws = random::draw_count();
        factory->Maintenance();
        const int expected = score == 60 ? 2 : 1;
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == expected);
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 1500 && resources() == before);
        MG_CHECK(session.rejected_icon_targets() == expected);
        MG_CHECK(random::draw_count() == draws + 1 + 2 * expected);
        std::set<int> places;
        std::vector<CoreLease> robot_cores, carrier_cores;
        for (int i = 0; i < expected; ++i) {
            auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[i]);
            MG_CHECK(carrier->CarryingRobot());
            auto *robot = static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
            MG_CHECK(robot->GetEnv()->m_Place != 0 && robot->GetSide() == PLAYER_SIDE);
            MG_CHECK(places.insert(robot->GetEnv()->m_Place).second);
            robot_cores.emplace_back(robot->GetCore(DEBUG_CALL_INFO));
            carrier_cores.emplace_back(carrier->GetCore(DEBUG_CALL_INFO));
        }
        while (g_MatrixMap->m_AD_Obj_cnt && session.frame() < 5900) session.advance();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && resources() == before);
        for (int i = 0; i < expected; ++i) {
            MG_CHECK(carrier_cores[i]->m_Object == nullptr && robot_cores[i]->m_Object);
            MG_CHECK(robot_cores[i]->m_Object->AsRobot()->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        }
    }
}

void reinforcement_carrier_limit() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        tests::BattleMaterialProfile material_profile;
        Session session({mode, 83, 10, 1000, false, false, true});
        tests::install_battle_transport();
        auto factory = base();
        MaintenanceInput::enable(*g_MatrixMap);
        g_Config.m_MaintenanceTime = 1000;
        g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1;
        g_MatrixMap->SetMaintenanceTime(0);
        std::vector<CMatrixFlyer *> carriers;
        for (int i = 0; i < MAX_ALWAYS_DRAW_OBJ; ++i) {
            auto *carrier = g_MatrixMap->StaticAdd<CMatrixFlyer>(true);
            g_MatrixMap->m_AD_Obj[g_MatrixMap->m_AD_Obj_cnt++] = carrier;
            carriers.push_back(carrier);
        }
        const auto before = resources();
        const auto draws = random::draw_count();
        factory->Maintenance();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == MAX_ALWAYS_DRAW_OBJ);
        MG_CHECK(random::draw_count() == draws + 2 && session.rejected_icon_targets() == 0);
        MG_CHECK(resources() == before && g_MatrixMap->BeforeMaintenanceTime() == 1000);
        for (int i = 0; i < MAX_ALWAYS_DRAW_OBJ; ++i)
            MG_CHECK(g_MatrixMap->m_AD_Obj[i] == carriers[i] && !carriers[i]->CarryingRobot());
        // These setup-only idle carriers have no trajectory; do not tick them.
        for (auto *carrier : carriers) g_MatrixMap->StaticDelete(carrier);
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0);
    }
}

const tests::Case cases[] = {
    {"game.reinforcement.unavailable_robot_icons", unavailable_robot_icons},
    {"game.reinforcement.delivery", reinforcement_delivery},
    {"game.reinforcement.reservations", reinforcement_reservations},
    {"game.reinforcement.carrier_limit", reinforcement_carrier_limit},
};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Reinforcement fixture test leaked tracked heap allocations.\n";
        result = EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
