// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include <cmath>
#include <iostream>
#include <memory>

extern int g_fcnt;
namespace tests::navigation {
namespace {
struct ChassisEffectsAccess : CMatrixEffect { using CMatrixEffect::m_SpotProperties; };
struct ChassisEffects {
    SSpotProperties track = ChassisEffectsAccess::m_SpotProperties[SPOT_SOLE_TRACK];
    SSpotProperties foot = ChassisEffectsAccess::m_SpotProperties[SPOT_SOLE_PNEUMATIC];
    void install() {
        ChassisEffectsAccess::m_SpotProperties[SPOT_SOLE_TRACK] = ChassisEffectsAccess::m_SpotProperties[SPOT_SOLE_WHEEL];
        ChassisEffectsAccess::m_SpotProperties[SPOT_SOLE_PNEUMATIC] = ChassisEffectsAccess::m_SpotProperties[SPOT_SOLE_WHEEL];
    }
    ~ChassisEffects() {
        ChassisEffectsAccess::m_SpotProperties[SPOT_SOLE_TRACK] = track;
        ChassisEffectsAccess::m_SpotProperties[SPOT_SOLE_PNEUMATIC] = foot;
    }
};
struct PneumaticAccess : CMatrixRobot { using CMatrixRobot::m_Pneumaic; };
// Released after the last tick, while Session still owns the active heap. Robot
// destruction does not consult gait data; restore both process-global values.
struct PneumaticCatalog {
    SPneumaticData *previous = PneumaticAccess::m_Pneumaic;
    int previous_count = g_fcnt;
    bool installed = false;
    void install(CVectorObject *mesh) {
        CMatrixRobot::BuildPneumaticData(mesh);
        installed = true;
        MG_CHECK(g_fcnt == 4);
    }
    ~PneumaticCatalog() {
        if (installed) {
            CMatrixRobot::DestroyPneumaticData();
            PneumaticAccess::m_Pneumaic = previous;
            g_fcnt = previous_count;
        }
    }
};
void journey(ERobotUnitKind chassis, random::Mode mode, int step) {
    BattleCombatAssets assets;
    ChassisEffects effects;
    battle::Profile profile{mode, 81, step, 4000, false};
    profile.layout = NavigationLayout::Open;
    battle::Session session(profile);
    PneumaticCatalog pneumatic;
    assets.install();
    effects.install();
    // Other chassis must not supply the candidate's movement characteristic.
    // Existing sentinel robots have already cached their own characteristics.
    for (int kind = RUK_CHASSIS_PNEUMATIC; kind <= RUK_CHASSIS_ANTIGRAVITY; ++kind)
        g_Config.m_ItemChars[CHASSIS1_MOVE_SPEED + 6 * (kind - 1)] = 0;
    const int config = CHASSIS1_STRUCTURE + 6 * (static_cast<int>(chassis) - 1);
    g_Config.m_ItemChars[config] = 1000;
    g_Config.m_ItemChars[config + 1] = 0.04f;
    g_Config.m_ItemChars[config + 2] = 0.6f;
    g_Config.m_ItemChars[config + 3] = 0.5f;
    g_Config.m_ItemChars[config + 4] = 0.7f;
    g_Config.m_ItemChars[config + 5] = 2.5f;
    auto *robot = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 200, chassis);
    if (chassis == RUK_CHASSIS_PNEUMATIC) pneumatic.install(robot->m_Unit[0].m_Graph->VO());
    MG_CHECK(session.remove(1) && session.remove(2));
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (object->IsRobot() && object->GetSide() != PLAYER_SIDE) {
            object->AsRobot()->SetMaxSpeed(0);
            object->AsRobot()->m_GroupSpeed = 0;
        }
    }
    auto &side = *g_MatrixMap->GetPlayerSide();
    side.CreateGroupFromCurrent(robot);
    side.Select(ROBOT, nullptr);
    const auto release_core = [](SObjectCore *core) { core->Release(); };
    std::unique_ptr<SObjectCore, decltype(release_core)> core(robot->GetCore(DEBUG_CALL_INFO), release_core);
    std::cout << session.metadata("five-chassis-open-journey-v1") << " chassis=" << chassis << std::endl;
    MG_CHECK(robot->GetMaxSpeed() == 0.6f);
    session.ground_command(500, 200);
    const auto &group = side.m_PlayerGroup[robot->GetGroupLogic()];
    MG_CHECK(group.Order() == mpo_MoveTo && group.m_To == CPoint(48, 18));
    int moving_samples = 0;
    unsigned gait_frames = 0;
    int planted_samples = 0, stride_samples = 0;
    while (session.frame() < profile.max_frames) {
        const float previous_x = robot->m_PosX;
        session.advance();
        MG_CHECK(robot->IsLiveRobot() && !robot->IsMustDie());
        MG_CHECK(robot->m_Unit[0].u1.s1.m_Kind == chassis);
        MG_CHECK(group.Order() == mpo_MoveTo && group.m_To == CPoint(48, 18));
        MG_CHECK(std::isfinite(robot->m_PosX) && std::isfinite(robot->m_PosY));
        MG_CHECK(std::isfinite(robot->m_Speed) && robot->m_Speed >= 0 && robot->m_Speed <= 0.60001f);
        MG_CHECK(robot->m_PosX >= 90 && robot->m_PosX <= 510);
        MG_CHECK(std::abs(robot->m_PosY - 200) < 0.001f);
        MG_CHECK(session.robot(3).x == 500 && session.robot(3).y == 500);
        MG_CHECK(session.robot(4).x == 540 && session.robot(4).y == 540);
        MG_CHECK(core->m_Object == robot && core->m_Matrix._43 == 0);
        // D3DX normalization can round the upright unit vector one ulp below 1.
        MG_CHECK(core->m_Matrix._23 == 0 && std::abs(core->m_Matrix._33 - 1) <= 0.00000012f);
        if (robot->m_PosX > 200 && robot->m_PosX < 400) {
            MG_CHECK(std::abs(robot->m_Speed - 0.6f) < 0.00001f);
            ++moving_samples;
            if (robot->m_PosX == previous_x) ++planted_samples;
            if (robot->m_PosX - previous_x > 1.0f) ++stride_samples;
            gait_frames |= 1u << robot->m_Unit[0].m_Graph->GetFrame();
        }
        if (robot->PLIsInPlace() && robot->GetOrdersInPool() == 0) break;
    }
    std::cout << "arrival_ms=" << session.time() << " position=" << robot->m_PosX << ',' << robot->m_PosY
              << " moving_samples=" << moving_samples << " gait_frames=" << gait_frames
              << " planted_samples=" << planted_samples << " stride_samples=" << stride_samples << std::endl;
    MG_CHECK(moving_samples > 0 && robot->PLIsInPlace());
    MG_CHECK(robot->m_PosX == 500 && robot->m_PosY == 200);
    MG_CHECK(robot->GetOrdersInPool() == 0);
    if (chassis == RUK_CHASSIS_PNEUMATIC) MG_CHECK(gait_frames == 15 && planted_samples > 0 && stride_samples > 0);
    // This no-draw profile refreshes the pneumatic foot's cached transform
    // through the 100 ms bounds cache, not once per rendered frame.
    const int expected_arrival = chassis == RUK_CHASSIS_PNEUMATIC ? (step == 10 ? 14220 : 15712) : 7520;
    MG_CHECK(session.time() == expected_arrival);
    // Arrival must remain stable after the scheduler and movement effects tick.
    for (int frame = 0; frame < 20; ++frame) {
        session.advance();
        MG_CHECK(robot->PLIsInPlace() && robot->m_PosX == 500 && robot->m_PosY == 200);
        MG_CHECK(robot->GetOrdersInPool() == 0);
        MG_CHECK(group.Order() == mpo_MoveTo && group.m_To == CPoint(48, 18));
    }
}
}
void pneumatic_cleanup() {
    BattleCombatAssets assets;
    battle::Session session(battle::Profile{});
    PneumaticCatalog pneumatic;
    assets.install();
    MG_CHECK(PneumaticAccess::m_Pneumaic == nullptr);
    auto *robot = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 200, RUK_CHASSIS_PNEUMATIC);
    // Build real gait data twice; each retired catalog must permit both repeated
    // cleanup and fresh initialization without accessing the released buffer.
    for (int lifetime = 0; lifetime < 2; ++lifetime) {
        pneumatic.install(robot->m_Unit[0].m_Graph->VO());
        MG_CHECK(PneumaticAccess::m_Pneumaic != nullptr && g_fcnt == 4);
        CMatrixRobot::DestroyPneumaticData();
        pneumatic.installed = false;
        const bool retired = PneumaticAccess::m_Pneumaic == nullptr;
        // Restore the fixture alias even when the original implementation fails
        // the assertion, so reporting the regression does not itself double-free.
        PneumaticAccess::m_Pneumaic = pneumatic.previous;
        g_fcnt = pneumatic.previous_count;
        MG_CHECK(retired);
        CMatrixRobot::DestroyPneumaticData();
        MG_CHECK(PneumaticAccess::m_Pneumaic == nullptr);
    }
}
void chassis_journeys() {
    for (auto chassis : {RUK_CHASSIS_PNEUMATIC, RUK_CHASSIS_WHEEL, RUK_CHASSIS_TRACK,
                         RUK_CHASSIS_HOVERCRAFT, RUK_CHASSIS_ANTIGRAVITY})
        for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
            for (int step : {10, 16}) journey(chassis, mode, step);
}
}
