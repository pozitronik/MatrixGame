// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include <memory>
#include <cmath>

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
namespace {
void disabled_blocker_run(bool lethal, bool apply_hit = true) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        BattleCombatAssets assets;
        auto settings = profile(mode);
        settings.layout = NavigationLayout::Corridor;
        settings.step_ms = step;
        battle::Session session(settings);
        assets.install();
        MG_CHECK(session.remove(2));
        // Keep opposing traffic outside the passage; their AI still ticks.
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsLiveRobot() && object->GetSide() == 2) {
                object->AsRobot()->SetMaxSpeed(0);
                object->AsRobot()->m_GroupSpeed = 0;
            }
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto *follower = side.GetCurGroup()->GetObjectByN(0)->AsRobot();
        follower->m_PosX = 250; follower->m_PosY = 300;
        follower->MapPosCalc(); follower->RChange(MR_Matrix); follower->RNeed(MR_Matrix); follower->JoinToGroup();
        auto *blocker = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 310, 300);
        blocker->InitMaxHitpoint(100);
        blocker->SetGroupLogic(-1);
        blocker->SetMaxSpeed(0.15f); blocker->m_GroupSpeed = 0.15f;
        blocker->MoveTo(58, 28);
        std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(blocker->GetCore(DEBUG_CALL_INFO),
            [](SObjectCore *value) { value->Release(); });
        session.ground_command(500, 300);
        const auto intent = session.robot(1);
        while (session.time() < 500) session.advance();
        MG_CHECK(blocker->m_PosX > 310 && follower->m_PosX > 250);
        const float x = blocker->m_PosX, y = blocker->m_PosY;
        const float follower_at_hit = follower->m_PosX;
        const int hit_time = session.time();
        const auto weapon = lethal ? WEAPON_LASER : WEAPON_LIGHTENING;
        g_Config.m_RobotDamages[Weap2Index(weapon)] = {lethal ? 100 : 0, 0, 0};
        g_Config.m_RobotDamages[Weap2Index(WEAPON_SHORTED)] = {0, 0, 0};
        g_MatrixMap->m_Difficulty.k_damage_enemy_to_player = 1;
        if (apply_hit) {
            const bool destroyed = blocker->Damage(weapon, blocker->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr);
            MG_CHECK(destroyed == lethal);
        }
        MG_CHECK(lethal ? !blocker->IsLiveRobot() : StatusObservation::shorted(*blocker) == apply_hit);
        int disabled_frames = 0, retirement = 0, passed_wreck = 0;
        bool recovered = false;
        while (session.time() < 20000) {
            session.advance();
            const auto state = session.robot(1);
            MG_CHECK(state.alive && state.order == mpo_MoveTo);
            MG_CHECK(state.destination_x == intent.destination_x && state.destination_y == intent.destination_y);
            MG_CHECK(std::isfinite(state.x) && std::isfinite(state.y));
            if (state.x >= 280 && state.x <= 360) MG_CHECK(state.y >= 280 && state.y <= 320);
            MG_CHECK(session.robot(3).x == 500 && session.robot(3).y == 500);
            MG_CHECK(session.robot(4).x == 540 && session.robot(4).y == 540);
            if (lethal) {
                if (!passed_wreck && state.x > x) {
                    passed_wreck = session.time();
                    MG_CHECK(core->m_Object == blocker && !blocker->IsLiveRobot());
                }
                if (core->m_Object) {
                    MG_CHECK(core->m_Object == blocker && !blocker->IsLiveRobot());
                    ++disabled_frames;
                } else if (!retirement) retirement = session.time();
            } else {
                MG_CHECK(core->m_Object == blocker && blocker->IsLiveRobot());
                if (StatusObservation::shorted(*blocker)) {
                    MG_CHECK(blocker->m_PosX == x && blocker->m_PosY == y);
                    MG_CHECK(!recovered && follower->m_PosX < x);
                    ++disabled_frames;
                } else if (!recovered) {
                    if (apply_hit) {
                        MG_CHECK(session.time() - hit_time >= 500 && session.time() - hit_time < 500 + step);
                        MG_CHECK(blocker->m_PosX != x || blocker->m_PosY != y);
                        MG_CHECK(follower->m_PosX > follower_at_hit && follower->m_PosX < blocker->m_PosX);
                        // The distant live leader throttles the follower before contact.
                        // Without robot collision handling it advances over 10 units here.
                        const float progress = follower->m_PosX - follower_at_hit;
                        std::cout << "follower_progress_during_short=" << progress << " step=" << step << std::endl;
                        MG_CHECK(progress < 10.0f);
                        MG_CHECK(blocker->FindOrderLikeThat(ROT_MOVE_TO));
                    }
                    recovered = true;
                }
            }
        }
        std::cout << session.metadata("disabled-corridor-blocker-v2") << " lethal=" << lethal << " apply_hit=" << apply_hit
                  << " disabled_frames=" << disabled_frames << " retired=" << retirement
                  << " follower=" << follower->m_PosX << ',' << follower->m_PosY
                  << " in_place=" << session.robot(1).in_place << '\n';
        MG_CHECK((disabled_frames > 0) == apply_hit);
        MG_CHECK(lethal ? retirement > hit_time : recovered);
        if (lethal) MG_CHECK(passed_wreck > hit_time && passed_wreck < retirement);
        std::cout << "passed_wreck=" << passed_wreck << " leader=" << (core->m_Object ? blocker->m_PosX : -1) << std::endl;
        MG_CHECK(session.robot(1).in_place && follower->m_PosX == 500 && follower->m_PosY == 300);
        MG_CHECK(side.GetStatValue(STAT_ROBOT_KILL) == 0);
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == (lethal ? 1 : 0));
    }
}
}
void shorted_route_blocker() { disabled_blocker_run(false, false); disabled_blocker_run(false); }
void dying_route_blocker() { disabled_blocker_run(true); }

}
