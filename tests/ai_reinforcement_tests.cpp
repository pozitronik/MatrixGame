// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_flyer.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixRobot.hpp"
#include "test_support.hpp"
#include <memory>
#include <vector>

namespace tests::ai {
namespace {
battle::Profile automatic_profile(random::Mode mode, unsigned seed) {
    battle::Profile profile;
    profile.mode = mode;
    profile.seed = seed;
    profile.audio = false;
    profile.initially_selected = false;
    profile.unavailable_icon_targets = true;
    return profile;
}
struct CoreRelease { void operator()(SObjectCore *core) const { core->Release(); } };
using CoreLease = std::unique_ptr<SObjectCore, CoreRelease>;
struct AutomaticScene {
    BattleMaterialProfile material;
    battle::Session session;
    std::vector<EnginePtr<CMatrixBuilding>> buildings;
    unsigned decision_seed;
    random::Mode decision_mode;
    bool started = false;
    explicit AutomaticScene(random::Mode mode, unsigned seed = 79)
        : session(automatic_profile(mode, seed)), decision_seed(seed), decision_mode(mode) {
        install_battle_transport();
        MG_CHECK(!g_MatrixMap->MaintenanceDisabled());
        g_Config.m_MaintenanceTime = 20000;
        g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1;
        g_MatrixMap->SetMaintenanceTime(0);
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsRobot()) {
                object->AsRobot()->GetEnv()->m_Place = -1;
                if (object->GetSide() == PLAYER_SIDE) object->AsRobot()->SetTeam(0);
            }
        SETFLAG(g_MatrixMap->m_Flags, MMFLAG_AUTOMATIC_MODE);
    }
    CMatrixBuilding *add(int side, float coordinate, EBuildingType kind = BUILDING_TITAN) {
        CMatrixRobotAI *donor = nullptr;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsLiveRobot()) { donor = object->AsRobot(); break; }
        MG_CHECK(donor);
        auto owned = make_battle_building(*donor->m_Unit[0].m_Graph->VO(), side, kind, coordinate, coordinate);
        auto *result = owned.get();
        result->AddLT();
        buildings.push_back(std::move(owned));
        return result;
    }
    void decide() {
        if (!started) {
            // The fixed decision sequence begins after geometry construction.
            random::seed(decision_seed, decision_mode);
            started = true;
        }
        g_MatrixMap->GetPlayerSide()->LogicTakt(10);
    }
    CMatrixRobotAI *cargo() {
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 1);
        auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
        MG_CHECK(carrier->CarryingRobot());
        return static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
    }
};
}

void automatic_reinforcement_buildings() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        AutomaticScene scene(mode);
        scene.add(0, 100);
        scene.add(2, 500);
        scene.add(PLAYER_SIDE, 300)->m_State = BUILDING_DIP;
        scene.add(PLAYER_SIDE, 400)->m_State = BUILDING_DIP_EXPLODED;
        scene.decide();
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && g_MatrixMap->BeforeMaintenanceTime() == 0);
        MG_CHECK(random::draw_count() == 1);
        scene.add(PLAYER_SIDE, 100);
        random::seed(79, mode); // Explicitly restart the same decision schedule after adding the eligible input.
        scene.decide();
        auto *cargo = scene.cargo();
        std::cout << scene.session.metadata("synthetic-ai-reinforcement-v1") << '\n';
        std::cout << "eligible place=" << cargo->GetEnv()->m_Place << " draws=" << random::draw_count() << '\n';
        MG_CHECK(cargo->GetSide() == PLAYER_SIDE && cargo->GetHitPoint() == 150);
        MG_CHECK(random::draw_count() == 6);
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 20000);
        MG_CHECK(scene.session.rejected_icon_targets() == 1);
    }
}

void automatic_reinforcement_choices() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int choice = 0; choice < 3; ++choice) {
        const unsigned seed = choice == 0 ? 79 : choice == 1 ?
            (mode == random::Mode::ParkMiller ? 23 : 36) :
            (mode == random::Mode::ParkMiller ? 6389 : 491);
        AutomaticScene scene(mode, seed);
        scene.add(PLAYER_SIDE, 100);
        scene.add(PLAYER_SIDE, 500);
        scene.decide();
        std::cout << scene.session.metadata("synthetic-ai-reinforcement-choice-v1") << '\n';
        std::cout << "seed=" << seed << " mode=" << int(mode) << " carriers=" << g_MatrixMap->m_AD_Obj_cnt;
        if (g_MatrixMap->m_AD_Obj_cnt) std::cout << " place=" << scene.cargo()->GetEnv()->m_Place;
        std::cout << " draws=" << random::draw_count() << '\n';
        MG_CHECK(random::draw_count() == (choice == 0 ? 7u : choice == 1 ? 6u : 2u));
        if (choice == 2) {
            MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && g_MatrixMap->BeforeMaintenanceTime() == 0);
        } else {
            scene.cargo();
            // TaktTL may reassign the environment place in the same side call.
            // The request's player-order record retains its original drop destination.
            const auto &request = g_MatrixMap->GetPlayerSide()->m_PlayerGroup[0];
            MG_CHECK(request.Order() == mpo_Attack);
            MG_CHECK(request.m_To == (choice == 0 ? CPoint(48, 48) : CPoint(8, 8)));
        }
    }
}

void automatic_reinforcement_designs() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (bool alternate : {false, true}) {
        const unsigned seed = alternate ? (mode == random::Mode::ParkMiller ? 3 : 2) : 79;
        AutomaticScene scene(mode, seed);
        auto *order = g_MatrixData->BlockGet(PAR_SOURCE_FLYER_ORDERS)->BlockGet(PAR_SOURCE_FLYER_ORDERS_GIVE_BOT);
        auto *design = order->BlockGetAdd(L"110");
        design->ParSetAdd(L"BotChassis", L"2");
        design->ParSetAdd(L"BotArmor", L"1");
        design->ParSetAdd(L"BotHead", L"0");
        design->ParSetAdd(L"BotStrength", L"2");
        design->ParSetAdd(L"BotHitpoint", L"2300");
        scene.add(PLAYER_SIDE, 100);
        scene.decide();
        auto *cargo = scene.cargo();
        const bool second_design = (mode == random::Mode::ParkMiller) != alternate;
        MG_CHECK(cargo->GetHitPoint() == (second_design ? 230 : 150));
        MG_CHECK(cargo->m_UnitCnt == 2 && cargo->m_CurrState == ROBOT_CARRYING);
        const auto draws = random::draw_count();
        scene.decide(); // Active cooldown must preserve the ordered design.
        MG_CHECK(scene.cargo() == cargo && scene.session.rejected_icon_targets() == 1);
        // This is the whole side decision; its strategy still consumes one draw.
        MG_CHECK(draws == 6 && random::draw_count() == draws + 1);
        std::cout << scene.session.metadata("synthetic-ai-reinforcement-design-v1") << '\n';
        std::cout << "design_hitpoints=" << cargo->GetHitPoint() << " rng_draws=" << draws << '\n';
    }
}

void automatic_reinforcement_delivery() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        AutomaticScene scene(mode);
        auto *source = scene.add(PLAYER_SIDE, 100);
        g_MatrixMap->AddObject(scene.buildings.back().release(), true);
        scene.buildings.pop_back();
        CoreLease source_core(source->GetCore(DEBUG_CALL_INFO));
        scene.decide();
        std::cout << scene.session.metadata("synthetic-ai-reinforcement-delivery-v1") << '\n';
        auto *cargo = scene.cargo();
        auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
        CoreLease cargo_core(cargo->GetCore(DEBUG_CALL_INFO));
        CoreLease carrier_core(carrier->GetCore(DEBUG_CALL_INFO));
        // A player retakes control and the source factory disappears. An
        // already ordered carrier remains an independent map object.
        RESETFLAG(g_MatrixMap->m_Flags, MMFLAG_AUTOMATIC_MODE);
        g_MatrixMap->StaticDelete(source);
        MG_CHECK(source_core->m_Object == nullptr);
        int released = -1, landed = -1;
        while (g_MatrixMap->m_AD_Obj_cnt && scene.session.frame() < 2000) {
            scene.session.advance();
            MG_CHECK(cargo_core->m_Object == cargo);
            if (released < 0 && cargo->m_CurrState != ROBOT_CARRYING) {
                released = scene.session.time();
                MG_CHECK(carrier_core->m_Object == carrier);
                MG_CHECK(cargo->m_CurrState == ROBOT_FALLING && !carrier->CarryingRobot());
            }
            if (landed < 0 && released >= 0 && cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD)
                landed = scene.session.time();
        }
        std::cout << "delivery mode=" << int(mode) << " release=" << released << " landed=" << landed
                  << " done=" << scene.session.time() << '\n';
        MG_CHECK(released == (mode == random::Mode::ParkMiller ? 3580 : 3780));
        MG_CHECK(landed == (mode == random::Mode::ParkMiller ? 3900 : 4100));
        MG_CHECK(scene.session.time() == (mode == random::Mode::ParkMiller ? 6910 : 7370));
        MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && carrier_core->m_Object == nullptr);
        MG_CHECK(cargo->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
    }
}
}
