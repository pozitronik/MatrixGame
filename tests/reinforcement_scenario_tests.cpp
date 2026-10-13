// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_flyer.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFormGame.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceStatic.h"
#include "input.hpp"
#include "test_support.hpp"
#include <array>
#include <memory>
#include <set>
#include <vector>

namespace {
using tests::battle::Session;
using Cost = std::array<int, MAX_RESOURCES>;
tests::battle::Profile icon_profile(random::Mode mode) {
    tests::battle::Profile profile;
    profile.mode = mode;
    profile.seed = 83;
    profile.step_ms = 10;
    profile.max_frames = 6000;
    profile.audio = false;
    profile.initially_selected = false;
    profile.unavailable_icon_targets = true;
    return profile;
}
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
std::set<std::size_t> map_robot_ids() {
    std::set<std::size_t> result;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot()) result.insert(object->AsRobot()->UID);
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
void unavailable_robot_icons() {
    for (int repeat = 0; repeat < 2; ++repeat) {
        Session session(icon_profile(random::Mode::ParkMiller));
        auto *robot = source_robot();
        MG_CHECK(session.rejected_icon_targets() == 0);
        const auto draws = random::draw_count();
        robot->CreateTextures();
        MG_CHECK(!robot->GetBigTexture() && !robot->GetMedTexture());
        MG_CHECK(session.rejected_icon_targets() == 1 && random::draw_count() == draws);
        robot->CreateTextures();
        MG_CHECK(!robot->GetBigTexture() && !robot->GetMedTexture());
        MG_CHECK(session.rejected_icon_targets() == 2 && random::draw_count() == draws);
        MG_CHECK(session.metadata().find("storage-device-unavailable-icons") != std::string::npos);
    }
    Session ordinary;
    MG_CHECK(ordinary.rejected_icon_targets() == 0);
    MG_CHECK(ordinary.metadata().find("unavailable-icons") == std::string::npos);
}

void reinforcement_delivery() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int repeat = 0; repeat < 3; ++repeat) {
        tests::BattleMaterialProfile material_profile;
        Session session(icon_profile(mode));
        std::cout << session.metadata("synthetic-reinforcement-v1") << '\n';
        tests::install_battle_transport();
        auto factory = base();
        MG_CHECK(session.remove(2) && session.remove(4));
        MG_CHECK(!g_MatrixMap->MaintenanceDisabled());
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
            if (released < 0 && robot->m_CurrState != ROBOT_CARRYING) {
                released = session.time();
                MG_CHECK(robot->m_CurrState == ROBOT_FALLING);
                MG_CHECK(carrier_core->m_Object == carrier && !carrier->CarryingRobot());
            }
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
        Session session(icon_profile(mode));
        std::cout << "score=" << score << " " << session.metadata("synthetic-reinforcement-reservations-v1") << '\n';
        tests::install_battle_transport(score);
        auto factory = base();
        // All four places are otherwise free; one live robot reserves place zero.
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsLiveRobot()) object->AsRobot()->GetEnv()->m_Place = -1;
        source_robot()->GetEnv()->m_Place = 0;
        MG_CHECK(!g_MatrixMap->MaintenanceDisabled());
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
            MG_CHECK(robot->GetEnv()->m_Place > 0 && robot->GetEnv()->m_Place < g_MatrixMap->m_RN.m_PlaceCnt);
            MG_CHECK(robot->GetSide() == PLAYER_SIDE);
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
        Session session(icon_profile(mode));
        std::cout << session.metadata("synthetic-reinforcement-cap-v1") << '\n';
        tests::install_battle_transport();
        auto factory = base();
        MG_CHECK(!g_MatrixMap->MaintenanceDisabled());
        g_Config.m_MaintenanceTime = 1000;
        g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1;
        g_MatrixMap->SetMaintenanceTime(0);
        std::vector<CMatrixFlyer *> carriers;
        for (int i = 0; i < MAX_ALWAYS_DRAW_OBJ; ++i) {
            auto *carrier = g_MatrixMap->StaticAdd<CMatrixFlyer>(true);
            g_MatrixMap->m_AD_Obj[g_MatrixMap->m_AD_Obj_cnt++] = carrier;
            carriers.push_back(carrier);
        }
        const auto robots_before = map_robot_ids();
        const auto before = resources();
        const auto draws = random::draw_count();
        factory->Maintenance();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == MAX_ALWAYS_DRAW_OBJ);
        MG_CHECK(map_robot_ids() == robots_before);
        MG_CHECK(random::draw_count() == draws + 2 && session.rejected_icon_targets() == 0);
        MG_CHECK(resources() == before && g_MatrixMap->BeforeMaintenanceTime() == 1000);
        for (int i = 0; i < MAX_ALWAYS_DRAW_OBJ; ++i)
            MG_CHECK(g_MatrixMap->m_AD_Obj[i] == carriers[i] && !carriers[i]->CarryingRobot());
        // These setup-only idle carriers have no trajectory; do not tick them.
        for (auto *carrier : carriers) g_MatrixMap->StaticDelete(carrier);
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0);
    }
}

void reinforcement_player_request() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (bool keyboard : {false, true}) for (auto kind : {BUILDING_BASE, BUILDING_TITAN}) {
        tests::BattleMaterialProfile material_profile;
        Session session(icon_profile(mode));
        std::cout << "keyboard=" << keyboard << " building=" << kind << " "
                  << session.metadata("synthetic-reinforcement-command-v1") << '\n';
        tests::install_battle_transport();
        auto factory = tests::make_battle_building(*source_robot()->m_Unit[0].m_Graph->VO(),
            PLAYER_SIDE, kind, 100, 100);
        auto &side = *g_MatrixMap->GetPlayerSide();
        struct SelectionReset {
            CMatrixSideUnit &side;
            ~SelectionReset() {
                side.m_ActiveObject = nullptr;
                side.m_CurrSel = NOTHING_SELECTED;
                Input::onFocusLost();
            }
        } reset{side};
        CFormMatrixGame form;
        CIFaceStatic button;
        button.m_strName = IF_CALL_FROM_HELL;
        auto request = [&] {
            if (keyboard) {
                form.Keyboard(true, 'H');
                form.Keyboard(false, 'H');
                MG_CHECK(!Input::isVKeyPressed('H'));
            } else g_IFaceList->PlayerAction(&button);
        };
        g_Config.m_MaintenanceTime = 1000;
        g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1;
        g_MatrixMap->SetMaintenanceTime(0);
        MG_CHECK(!g_MatrixMap->MaintenanceDisabled());
        const auto before = resources();
        const auto initial_robots = map_robot_ids();
        const auto draws = random::draw_count();
        side.m_ActiveObject = factory.get();
        side.m_CurrSel = NOTHING_SELECTED;
        request();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && random::draw_count() == draws);
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 0);
        side.m_CurrSel = kind == BUILDING_BASE ? BASE_SELECTED : BUILDING_SELECTED;
        factory->m_Side = 0; // A formerly owned, now neutral selection cannot request help.
        request();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && random::draw_count() == draws);
        factory->m_Side = PLAYER_SIDE;
        request();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 1 && random::draw_count() == draws + 3);
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 1000 && resources() == before);
        auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
        MG_CHECK(carrier->CarryingRobot());
        auto *robot = static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
        CoreLease robot_core(robot->GetCore(DEBUG_CALL_INFO));
        CoreLease carrier_core(carrier->GetCore(DEBUG_CALL_INFO));
        MG_CHECK(robot->GetSide() == PLAYER_SIDE && !initial_robots.contains(robot->UID));
        MG_CHECK(map_robot_ids().size() == initial_robots.size() + 1);
        MG_CHECK(session.rejected_icon_targets() == 1);
        const auto requested = random::draw_count();
        request();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 1 && random::draw_count() == requested);
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 1000);
        side.m_ActiveObject = nullptr;
        side.m_CurrSel = NOTHING_SELECTED;
        while (g_MatrixMap->m_AD_Obj_cnt && session.frame() < 1500) session.advance();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && carrier_core->m_Object == nullptr);
        MG_CHECK(robot_core->m_Object == robot && robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        MG_CHECK(map_robot_ids().size() == initial_robots.size() + 1 && resources() == before);
    }
}

void reinforcement_mixed_proposals() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (unsigned seed : {83u, 84u}) {
        tests::BattleMaterialProfile material_profile;
        auto profile = icon_profile(mode);
        profile.seed = seed;
        Session session(profile);
        std::cout << session.metadata("synthetic-reinforcement-proposals-v1") << '\n';
        auto &order = tests::install_battle_transport(60);
        auto *second = order.BlockAdd(L"100");
        second->ParAdd(L"BotChassis", L"2");
        second->ParAdd(L"BotArmor", L"1");
        second->ParAdd(L"BotHead", L"0");
        second->ParAdd(L"BotStrength", L"2");
        second->ParAdd(L"BotHitpoint", L"2400");
        auto factory = base();
        g_Config.m_MaintenanceTime = 1000;
        g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1;
        g_MatrixMap->SetMaintenanceTime(0);
        MG_CHECK(!g_MatrixMap->MaintenanceDisabled());
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsLiveRobot()) object->AsRobot()->GetEnv()->m_Place = -1;
        const auto before = resources();
        const auto draws = random::draw_count();
        factory->Maintenance();
        std::vector<CoreLease> cargo;
        std::set<int> places;
        // Measured seed and RNG negative controls: the two sixty-point robots
        // differ from the one hundred-point choice in both identity and health.
        const int expected = mode == random::Mode::ParkMiller && seed == 84 ? 2 : 1;
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == expected);
        MG_CHECK(random::draw_count() == draws + (expected == 2 ? 5 : 3));
        MG_CHECK(session.rejected_icon_targets() == expected);
        std::cout << "proposal seed=" << seed << " mode=" << int(mode)
                  << " draws=" << random::draw_count() - draws << " hp=";
        for (int i = 0; i < g_MatrixMap->m_AD_Obj_cnt; ++i) {
            auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[i]);
            MG_CHECK(carrier->CarryingRobot());
            auto *robot = static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
            std::cout << robot->GetHitPoint() << ',';
            MG_CHECK(robot->GetSide() == PLAYER_SIDE);
            MG_CHECK(robot->GetHitPoint() == (expected == 2 ? 150 : 240));
            MG_CHECK(robot->GetEnv()->m_Place >= 0 && robot->GetEnv()->m_Place < g_MatrixMap->m_RN.m_PlaceCnt);
            cargo.emplace_back(robot->GetCore(DEBUG_CALL_INFO));
            MG_CHECK(places.insert(robot->GetEnv()->m_Place).second);
        }
        std::cout << '\n';
        MG_CHECK(resources() == before && g_MatrixMap->BeforeMaintenanceTime() == 1000);
        while (g_MatrixMap->m_AD_Obj_cnt && session.frame() < 1500) session.advance();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && resources() == before);
        for (auto &core : cargo) {
            MG_CHECK(core->m_Object && core->m_Object->IsLiveRobot());
            MG_CHECK(core->m_Object->AsRobot()->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        }
    }
}

const tests::Case cases[] = {
    {"game.reinforcement.mixed_proposals", reinforcement_mixed_proposals},
    {"game.reinforcement.player_request", reinforcement_player_request},
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
