// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include <memory>

namespace tests::navigation {
namespace {
struct StatusObservation : CMatrixMapStatic {
    static bool shorted(CMatrixMapStatic &target) { return (target.* &StatusObservation::IsShorted)(); }
};
battle::Profile profile(random::Mode mode) {
    battle::Profile result{mode, 81, 10, 2500, false, true};
    result.layout = NavigationLayout::Open;
    return result;
}
}

void shorted_movement_recovery() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session(profile(mode));
        assets.install();
        std::cout << session.metadata("navigation-shorted-movement-v1") << '\n';
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto *robot = side.GetCurGroup()->GetObjectByN(0)->AsRobot();
        session.ground_command(500, 200);
        for (int i = 0; i < 50; ++i) session.advance();
        MG_CHECK(robot->m_PosX > 100);
        const auto order = session.robot(1);
        const float x = robot->m_PosX, y = robot->m_PosY;
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LIGHTENING)] = {0, 0, 0};
        g_Config.m_RobotDamages[Weap2Index(WEAPON_SHORTED)] = {0, 0, 0};
        MG_CHECK(!robot->Damage(WEAPON_LIGHTENING, robot->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
        MG_CHECK(StatusObservation::shorted(*robot));
        for (int i = 0; i < 49; ++i) {
            session.advance();
            MG_CHECK(StatusObservation::shorted(*robot));
            MG_CHECK(robot->m_PosX == x && robot->m_PosY == y);
            MG_CHECK(session.robot(1).order == mpo_MoveTo);
            MG_CHECK(robot->FindOrderLikeThat(ROT_MOVE_TO));
            MG_CHECK(session.robot(1).destination_x == order.destination_x);
            MG_CHECK(session.robot(1).destination_y == order.destination_y);
        }
        session.advance();
        MG_CHECK(!StatusObservation::shorted(*robot));
        MG_CHECK(robot->m_PosX != x || robot->m_PosY != y);
        for (int i = 0; i < 1800; ++i) session.advance();
        MG_CHECK(!StatusObservation::shorted(*robot) && robot->IsLiveRobot());
        MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        MG_CHECK(robot->m_PosX == 500 && robot->m_PosY == 200);
        MG_CHECK(side.GetStatValue(STAT_ROBOT_KILL) == 0);
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    }
}

void drowning_retirement() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session(profile(mode));
        assets.install();
        std::cout << session.metadata("navigation-drowning-retirement-v1") << '\n';
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto *robot = side.GetCurGroup()->GetObjectByN(0)->AsRobot();
        std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(robot->GetCore(DEBUG_CALL_INFO),
            [](SObjectCore *value) { value->Release(); });
        // Declare a deep cell under the stationary wheel chassis. This checks
        // depth evaluation and death, not generation of terrain traversal masks.
        g_MatrixMap->UnitGetTest(5, 10)->a1 = -102;
        MG_CHECK(robot->Z_From_Pos() == -102 && !robot->IsMustDie());
        g_MatrixMap->UnitGetTest(5, 10)->a1 = -103;
        MG_CHECK(robot->Z_From_Pos() == -103 && robot->IsMustDie());
        session.advance();
        MG_CHECK(core->m_Object == robot && robot->m_CurrState == ROBOT_DIP);
        for (int i = 0; i < 600; ++i) session.advance();
        MG_CHECK(!core->m_Object && !session.robot(1).alive);
        MG_CHECK(session.robot(2).alive);
        MG_CHECK(side.GetStatValue(STAT_ROBOT_KILL) == 0);
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    }
}
}
