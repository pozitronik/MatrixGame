// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_combat_geometry.hpp"
#include "battle_flyer.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixRobot.hpp"
#include "cheats.hpp"
#include "test_support.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <sstream>
#include <vector>

namespace {
using namespace tests::battle;
void type_command(std::string_view command) {
    for (int i = 0; i < 16; ++i) MG_CHECK(!Cheats::processInput('Z'));
    for (unsigned i = 0; i < command.size(); ++i)
        MG_CHECK(Cheats::processInput(command[i]) == (i + 1 == command.size()));
}
Profile auto_profile(random::Mode mode, int step) {
    Profile p{mode, 84, step, 2000, false, true, true, tests::NavigationLayout::Open};
    return p;
}
std::vector<std::string> auto_activation_run(random::Mode mode, int step, bool eligible) {
    tests::BattleMaterialProfile materials;
    Session session(auto_profile(mode, step));
    tests::install_battle_transport();
    auto &side = *g_MatrixMap->GetPlayerSide();
    auto *player = side.GetCurGroup()->GetObjectByN(0)->AsRobot();
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot()) {
            object->AsRobot()->GetEnv()->m_Place = -1;
            object->AsRobot()->SetTeam(0);
        }
    auto owned = tests::make_battle_building(*player->m_Unit[0].m_Graph->VO(), eligible ? PLAYER_SIDE : 2,
        BUILDING_TITAN, 100, 100);
    auto *factory = owned.get();
    g_MatrixMap->AddObject(owned.release(), true);
    for (int resource : {RESOURCE_BASE, RESOURCE_TITAN, RESOURCE_PLASMA, RESOURCE_ELECTRONICS, RESOURCE_ENERGY})
        g_Config.m_Timings[resource] = 100000;
    g_Config.m_MaintenanceTime = 20000;
    g_MatrixMap->m_Difficulty.k_time_before_maintenance = 1;
    g_MatrixMap->SetMaintenanceTime(0);
    MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_AUTOMATIC_MODE | MMFLAG_FULLAUTO | MMFLAG_FLYCAM)));
    std::vector<std::string> trace;
    auto record = [&] {
        std::ostringstream out;
        out << session.snapshot() << ":flags=" << (g_MatrixMap->m_Flags & (MMFLAG_AUTOMATIC_MODE | MMFLAG_FULLAUTO | MMFLAG_FLYCAM))
            << ":cooldown=" << g_MatrixMap->BeforeMaintenanceTime() << ":carrier=" << g_MatrixMap->m_AD_Obj_cnt
            << ":factory=" << factory->GetSide() << ':' << factory->m_TrueColor.m_ColoredCnt
            << ":capture=" << (player->GetCaptureFactory() == factory) << ":break=" << player->CanBreakOrder();
        if (player->GetGroupLogic() >= 0) {
            const auto &logic = side.m_LogicGroup[player->GetGroupLogic()];
            out << ":logic=" << logic.m_Action.m_Type << ':' << logic.RobotsCnt();
        }
        out << ":team=" << side.m_Team[0].m_Action.m_Type << ':' << side.m_Team[0].m_RobotCnt;
        if (g_MatrixMap->m_AD_Obj_cnt) {
            auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
            out << ":carrying=" << carrier->CarryingRobot();
            if (carrier->CarryingRobot()) {
                auto *cargo = carrier->GetCarryingRobot()->AsRobot();
                out << ':' << cargo->GetSide() << ':' << cargo->m_CurrState << ':' << cargo->GetHitPoint();
            }
        }
        trace.push_back(out.str());
    };
    record();
    session.ground_command(500, 200);
    record();
    while (session.time() < 200) { session.advance(); record(); }
    std::cout << session.metadata("ordinary-auto-v1") << " eligible=" << eligible << " decision_seed=79" << std::endl;
    std::cout << "before=" << session.time() << " player=" << player->m_PosX << ',' << player->m_PosY
        << " group=" << player->GetGroupLogic() << " order=" << session.robot(1).order << std::endl;
    MG_CHECK(session.time() == (step == 10 ? 200 : 208));
    MG_CHECK(session.robot(1).order == mpo_MoveTo && player->m_PosX > 110 && player->m_PosY == 200);
    MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 0 && g_MatrixMap->BeforeMaintenanceTime() == 0);
    // Replay starts with this declared decision seed after the ordinary-control prelude.
    random::seed(79, mode);
    type_command("AUTO");
    MG_CHECK(random::draw_count() == 0);
    MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_AUTOMATIC_MODE));
    MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_FULLAUTO | MMFLAG_FLYCAM)));
    record();
    int request = 0;
    while (session.time() < 1200) {
        session.advance();
        if (!request && g_MatrixMap->m_AD_Obj_cnt) {
            request = session.time();
            auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
            MG_CHECK(carrier->CarryingRobot());
            auto *cargo = carrier->GetCarryingRobot()->AsRobot();
            MG_CHECK(cargo->GetSide() == PLAYER_SIDE && cargo->m_CurrState == ROBOT_CARRYING && cargo->GetHitPoint() == 150);
            MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 20000);
        }
        MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_FULLAUTO | MMFLAG_FLYCAM)));
        record();
    }
    const auto &group = side.m_LogicGroup[player->GetGroupLogic()];
    std::cout << "after=" << session.time() << " request=" << request << " action=" << group.m_Action.m_Type
        << " team=" << side.m_Team[0].m_Action.m_Type << " group=" << player->GetGroupLogic()
        << " count=" << group.RobotsCnt() << " player=" << player->m_PosX << ',' << player->m_PosY
        << " capture=" << (player->GetCaptureFactory() == factory) << " break=" << player->CanBreakOrder()
        << " cooldown=" << g_MatrixMap->BeforeMaintenanceTime() << " rng=" << random::draw_count() << std::endl;
    MG_CHECK(request == (eligible ? (step == 10 ? 210 : 224) : 0));
    MG_CHECK(group.m_Action.m_Type == (eligible ? mlat_Defence : mlat_Capture));
    MG_CHECK(side.m_Team[0].m_Action.m_Type == group.m_Action.m_Type);
    MG_CHECK(group.RobotsCnt() == (eligible ? 3 : 2));
    MG_CHECK((player->GetCaptureFactory() == factory) == !eligible && player->CanBreakOrder());
    MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == (eligible ? (step == 10 ? 19010 : 19024) : 0));
    MG_CHECK(random::draw_count() == (eligible ? (step == 10 ? 113u : 111u) : (step == 10 ? 110u : 108u)));
    const auto decision_draws = random::draw_count();
    type_command("AUTO");
    MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_AUTOMATIC_MODE | MMFLAG_FULLAUTO | MMFLAG_FLYCAM)));
    MG_CHECK(random::draw_count() == decision_draws);
    record();
    session.ground_command(120, 500);
    MG_CHECK(session.robot(1).order == mpo_MoveTo);
    int released = player->GetCaptureFactory() == nullptr ? session.time() : 0;
    record();
    while (session.time() < 4000) {
        session.advance();
        if (!released && player->GetCaptureFactory() == nullptr) released = session.time();
        record();
    }
    std::cout << "retake=" << session.time() << " released=" << released << " order=" << session.robot(1).order
        << " capture=" << (player->GetCaptureFactory() == factory) << " factory=" << factory->GetSide()
        << " player=" << player->m_PosX << ',' << player->m_PosY << " rng=" << random::draw_count() << std::endl;
    MG_CHECK(released == 1200 && player->GetCaptureFactory() == nullptr);
    MG_CHECK(session.robot(1).order == mpo_MoveTo && player->m_PosY > 300);
    MG_CHECK(factory->GetSide() == (eligible ? PLAYER_SIDE : 2));
    MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
    MG_CHECK(random::draw_count() == (eligible ? (mode == random::Mode::ParkMiller ? (step == 10 ? 372u : 371u)
        : (step == 10 ? 392u : 390u)) : (step == 10 ? 139u : 134u)));
    return trace;
}
void auto_activation() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool eligible : {false, true}) {
        const auto first = auto_activation_run(mode, step, eligible);
        MG_CHECK(first == auto_activation_run(mode, step, eligible));
    }
}
struct AutoCoreRelease { void operator()(SObjectCore *core) const { core->Release(); } };
using AutoCoreLease = std::unique_ptr<SObjectCore, AutoCoreRelease>;
struct CameraTrace {
    std::vector<std::string> state;
    std::vector<std::array<uint32_t, 2>> angles;
};
CameraTrace auto_camera_run(random::Mode mode, int step, bool reenable = false) {
    tests::BattleMaterialProfile materials;
    tests::BattleCombatAssets assets;
    Session session(auto_profile(mode, step));
    assets.install();
    std::array<CMatrixRobotAI *, 4> robots{};
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot()) {
            auto *robot = object->AsRobot();
            robot->SetMaxSpeed(0);
            robot->SetTeam(0);
            for (unsigned id = 1; id <= 4; ++id) if (session.matches(id, robot)) robots[id - 1] = robot;
        }
    for (auto *robot : robots) MG_CHECK(robot);
    g_MatrixMap->SetMaintenanceTime(100000);
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {1, 0, 0};
    std::array<AutoCoreLease, 4> cores;
    for (unsigned i = 0; i < cores.size(); ++i) cores[i].reset(robots[i]->GetCore(DEBUG_CALL_INFO));
    auto &camera = g_MatrixMap->m_Camera;
    const auto first_midpoint = (robots[0]->GetGeoCenter() + robots[2]->GetGeoCenter()) * 0.5f;
    const auto second_midpoint = (robots[0]->GetGeoCenter() + robots[3]->GetGeoCenter()) * 0.5f;
    CameraTrace trace;
    auto record = [&] {
        const auto &point = camera.GetLinkPoint();
        std::ostringstream out;
        out << session.snapshot() << ":flags=" << (g_MatrixMap->m_Flags & (MMFLAG_AUTOMATIC_MODE | MMFLAG_FULLAUTO | MMFLAG_FLYCAM))
            << ":camera=" << std::bit_cast<uint32_t>(point.x) << ':' << std::bit_cast<uint32_t>(point.y) << ':' << std::bit_cast<uint32_t>(point.z);
        for (const auto &core : cores) out << ":core=" << (core->m_Object != nullptr) << ':' << core->m_Ref;
        trace.state.push_back(out.str());
        trace.angles.push_back({std::bit_cast<uint32_t>(camera.GetAngleZ()), std::bit_cast<uint32_t>(camera.GetAngleX())});
        MG_CHECK(std::isfinite(camera.GetAngleZ()) && std::isfinite(camera.GetAngleX()));
    };
    std::cout << session.metadata("ordinary-auto-camera-v1") << std::endl;
    MG_CHECK(!(g_MatrixMap->m_Flags & (MMFLAG_AUTOMATIC_MODE | MMFLAG_FULLAUTO | MMFLAG_FLYCAM)));
    for (const auto &core : cores) MG_CHECK(core->m_Ref == 2);
    record();
    type_command("AUTO");
    record();
    type_command("FLYCAM");
    record();
    MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_AUTOMATIC_MODE) && !FLAG(g_MatrixMap->m_Flags, MMFLAG_FULLAUTO));
    session.advance(); record();
    const auto first_damage_rng = random::draw_count();
    MG_CHECK(!robots[2]->Damage(WEAPON_LASER, robots[2]->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), PLAYER_SIDE, robots[0]));
    MG_CHECK(cores[0]->m_Ref == 3 && cores[2]->m_Ref == 3 && cores[3]->m_Ref == 2);
    MG_CHECK(robots[2]->GetHitPoint() == 99.9f);
    MG_CHECK(random::draw_count() - first_damage_rng == 28);
    std::cout << "first damage draws=" << random::draw_count() - first_damage_rng << std::endl;
    record();
    while (session.time() < 2000) { session.advance(); record(); }
    std::cout << "first midpoint=" << first_midpoint.x << ',' << first_midpoint.y << ',' << first_midpoint.z
        << " camera=" << camera.GetLinkPoint().x << ',' << camera.GetLinkPoint().y << ',' << camera.GetLinkPoint().z
        << " refs=" << cores[0]->m_Ref << ',' << cores[2]->m_Ref << ',' << cores[3]->m_Ref << std::endl;
    MG_CHECK(cores[0]->m_Ref == 3 && cores[2]->m_Ref == 3 && cores[3]->m_Ref == 2);
    // The selected fight outlives the 1000 ms pending-pair TTL.
    const auto first_distance = camera.GetLinkPoint() - first_midpoint;
    MG_CHECK(D3DXVec3Length(&first_distance) < 32);
    const auto second_damage_rng = random::draw_count();
    MG_CHECK(!robots[3]->Damage(WEAPON_LASER, robots[3]->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), PLAYER_SIDE, robots[0]));
    MG_CHECK(cores[0]->m_Ref == 4 && cores[2]->m_Ref == 3 && cores[3]->m_Ref == 3);
    MG_CHECK(robots[3]->GetHitPoint() == 99.9f);
    MG_CHECK(random::draw_count() - second_damage_rng == 28);
    std::cout << "second damage draws=" << random::draw_count() - second_damage_rng << std::endl;
    record();
    int expired = 0;
    while (session.time() < 3200) {
        session.advance(); record();
        if (!expired && cores[3]->m_Ref == 2) expired = session.time();
    }
    MG_CHECK(expired == (step == 10 ? 3010 : 3008));
    MG_CHECK(cores[0]->m_Ref == 3 && cores[2]->m_Ref == 3 && cores[3]->m_Ref == 2);
    // A second live pair stays pending while the original pair still exists.
    const auto before_switch = camera.GetLinkPoint() - first_midpoint;
    std::cout << "before switch distance=" << D3DXVec3Length(&before_switch) << std::endl;
    MG_CHECK(D3DXVec3Length(&before_switch) < 10);
    // Re-register the expired alternative, then retire the current target.
    const auto third_damage_rng = random::draw_count();
    MG_CHECK(!robots[3]->Damage(WEAPON_LASER, robots[3]->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), PLAYER_SIDE, robots[0]));
    MG_CHECK(cores[0]->m_Ref == 4 && cores[3]->m_Ref == 3);
    MG_CHECK(random::draw_count() - third_damage_rng == 28 && robots[3]->GetHitPoint() == 99.8f);
    record();
    MG_CHECK(session.remove(3));
    MG_CHECK(cores[2]->m_Object == nullptr && cores[2]->m_Ref == 2);
    record();
    session.advance(); record();
    MG_CHECK(cores[2]->m_Ref == 1 && cores[0]->m_Ref == 3 && cores[3]->m_Ref == 3);
    while (session.time() < 6000) { session.advance(); record(); }
    std::cout << "second midpoint=" << second_midpoint.x << ',' << second_midpoint.y << ',' << second_midpoint.z
        << " camera=" << camera.GetLinkPoint().x << ',' << camera.GetLinkPoint().y << ',' << camera.GetLinkPoint().z
        << " refs=" << cores[0]->m_Ref << ',' << cores[2]->m_Ref << ',' << cores[3]->m_Ref
        << " angles=" << camera.GetAngleZ() << ',' << camera.GetAngleX() << " rng=" << random::draw_count() << std::endl;
    const auto second_distance = camera.GetLinkPoint() - second_midpoint;
    const auto previous_distance = camera.GetLinkPoint() - first_midpoint;
    MG_CHECK(D3DXVec3Length(&second_distance) < 2 && D3DXVec3Length(&previous_distance) > 26);
    MG_CHECK(random::draw_count() == 342 && cores[0]->m_Ref == 3 && cores[2]->m_Ref == 1 && cores[3]->m_Ref == 3);
    MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
    type_command("FLYCAM");
    session.advance(); record();
    MG_CHECK(cores[0]->m_Ref == 2 && cores[2]->m_Ref == 1 && cores[3]->m_Ref == 2);
    MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_FLYCAM) && FLAG(g_MatrixMap->m_Flags, MMFLAG_AUTOMATIC_MODE));
    std::cout << "disabled refs=" << cores[0]->m_Ref << ',' << cores[2]->m_Ref << ',' << cores[3]->m_Ref << std::endl;
    if (reenable) {
        type_command("FLYCAM");
        for (int i = 0; i < 50; ++i) { session.advance(); record(); }
        MG_CHECK(FLAG(g_MatrixMap->m_Flags, MMFLAG_FLYCAM));
        type_command("FLYCAM");
        session.advance(); record();
        MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_FLYCAM));
        MG_CHECK(cores[0]->m_Ref == 2 && cores[2]->m_Ref == 1 && cores[3]->m_Ref == 2);
    }
    return trace;
}
void auto_camera_fights() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const auto first = auto_camera_run(mode, step);
        const auto second = auto_camera_run(mode, step);
        MG_CHECK(first.state == second.state);
        MG_CHECK(first.angles == second.angles);
    }
}
void auto_camera_replay() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const auto first = auto_camera_run(mode, step, true);
        const auto second = auto_camera_run(mode, step, true);
        MG_CHECK(first.state == second.state);
        for (unsigned i = 0; i < first.angles.size(); ++i) if (first.angles[i] != second.angles[i]) {
            std::cout << "angle replay differs at snapshot=" << i << " first_yaw=" << std::bit_cast<float>(first.angles[i][0])
                << " repeated_yaw=" << std::bit_cast<float>(second.angles[i][0]) << std::endl;
            break;
        }
        MG_CHECK(first.angles == second.angles);
    }
}
template<random::Mode Mode, int Step> void camera_first_flight_reference() {
    const auto trace = auto_camera_run(Mode, Step);
    for (size_t i = 0; i < trace.state.size(); ++i)
        std::cout << "camera-reference " << i << ' ' << trace.angles[i][0] << ' ' << trace.angles[i][1]
            << ' ' << trace.state[i] << '\n';
}
std::vector<std::array<uint32_t, 5>> camera_overview_run(int step,
        random::Mode mode = random::Mode::ParkMiller, bool start_in_overview = false, bool warm_first = false) {
    Session session(auto_profile(mode, step));
    const auto draws = random::draw_count();
    CMatrixRobotAI *player = nullptr, *enemy = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot()) {
            if (session.matches(1, object->AsRobot())) player = object->AsRobot();
            if (session.matches(3, object->AsRobot())) enemy = object->AsRobot();
        }
    MG_CHECK(player && enemy);
    const auto run_flight = [&](bool immediate_overview) {
        SAutoFlyData flight{};
        struct ReleaseFlight { SAutoFlyData &flight; ~ReleaseFlight() { flight.Release(); } } release{flight};
        flight.m_Cur = flight.m_New = D3DXVECTOR3(200, 200, 1000);
        flight.m_CurDist = flight.m_NewDist = 100;
        flight.m_CurAngZ = 0.5f; flight.m_NewAngZ = 1.5f;
        flight.m_CurAngX = 0.4f; flight.m_NewAngX = 0.5f;
        flight.m_CalcDestAngZTime = 100000;
        flight.AddWarPair(enemy, player);
        std::vector<std::array<uint32_t, 5>> trace;
        const auto advance = [&] {
            flight.Takt(float(step));
            g_MatrixMap->m_Camera.BeforeDraw();
            trace.push_back({std::bit_cast<uint32_t>(flight.m_CurAngZ), std::bit_cast<uint32_t>(flight.m_CurAngX),
                std::bit_cast<uint32_t>(flight.m_Cur.x), std::bit_cast<uint32_t>(flight.m_Cur.y),
                std::bit_cast<uint32_t>(flight.m_Cur.z)});
        };
        if (!immediate_overview) {
            for (int i = 0; i < 20; ++i) advance();
            MG_CHECK(flight.m_CurAngZ > 0.5f && flight.m_CurAngZ < 1.5f);
        }
        // Explicit component entry, either on the first update or after live
        // smoothing. The random 15-second overview trigger is not selected.
        flight.m_ObzorTime = 500;
        flight.m_BeforeObzorAngX = flight.m_CurAngX;
        float previous_step = 0;
        for (int i = 0; i < 20; ++i) {
            const auto before = flight.m_CurAngZ;
            advance();
            MG_CHECK(flight.m_ObzorTime == 500 - (i + 1) * step);
            if (immediate_overview) {
                // A new flight has no carried yaw, even if an earlier flight
                // in this same session ended with a nonzero last delta.
                MG_CHECK(flight.m_CurAngZ == 0.5f);
            } else {
                const float delta = flight.m_CurAngZ - before;
                MG_CHECK(delta > 0);
                if (i) MG_CHECK(delta < previous_step);
                previous_step = delta;
            }
        }
        return trace;
    };
    if (warm_first) {
        MG_CHECK(start_in_overview);
        run_flight(false); // Retire a nonzero history without rebuilding the camera/session.
    }
    auto trace = run_flight(start_in_overview);
    MG_CHECK(random::draw_count() == draws);
    return trace;
}
void auto_camera_overview_replay() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const auto first = camera_overview_run(step, mode);
        MG_CHECK(first == camera_overview_run(step, mode));
        const auto fresh = camera_overview_run(step, mode, true);
        MG_CHECK(fresh == camera_overview_run(step, mode, true, true));
    }
}
template<int Step> void camera_first_overview_reference() {
    const auto trace = camera_overview_run(Step);
    for (size_t i = 0; i < trace.size(); ++i) {
        std::cout << "camera-reference " << i;
        for (auto value : trace[i]) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
constexpr tests::Case cases[] = {
    {"game.ai.auto_activation", auto_activation},
    {"game.ai.auto_camera_fights", auto_camera_fights},
    {"game.ai.auto_camera_replay", auto_camera_replay},
    {"game.ai.auto_camera_overview_replay", auto_camera_overview_replay},
    {"probe.ai.camera_first_pm10", camera_first_flight_reference<random::Mode::ParkMiller, 10>},
    {"probe.ai.camera_first_pm16", camera_first_flight_reference<random::Mode::ParkMiller, 16>},
    {"probe.ai.camera_first_legacy10", camera_first_flight_reference<random::Mode::LegacyCRT, 10>},
    {"probe.ai.camera_first_legacy16", camera_first_flight_reference<random::Mode::LegacyCRT, 16>},
    {"probe.ai.camera_first_overview10", camera_first_overview_reference<10>},
    {"probe.ai.camera_first_overview16", camera_first_overview_reference<16>},
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
