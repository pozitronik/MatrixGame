// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_building.hpp"
#include "battle_flyer.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFlyer.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceElement.h"
#include "Effects/MatrixEffectLandscapeSpot.hpp"
#include "input.hpp"
#include <bit>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

namespace {
using tests::battle::Session;
CMatrixSideUnit &side() { return *g_MatrixMap->GetPlayerSide(); }
void press(CFormMatrixGame &form, uint8_t key) {
    form.Keyboard(true, key);
    form.Keyboard(false, key);
    MG_CHECK(!Input::isVKeyPressed(key));
}
void button(const wchar_t *name) {
    auto element = tests::make_engine_object<CIFaceElement>();
    element->m_strName = name;
    g_IFaceList->PlayerAction(element.get());
}
void select(std::initializer_list<CMatrixRobotAI *> robots) {
    // Selection is a declared precondition; the command itself uses real input.
    side().Select(NOTHING, nullptr);
    side().ResetSystemSelection();
    auto *group = side().GetCurSelGroup();
    for (auto *robot : robots) group->AddObject(robot, -4);
    side().SetCurGroup(group);
    side().m_CurrSel = robots.size() == 1 ? ROBOT_SELECTED : GROUP_SELECTED;
}
CMatrixRobotAI *find(const Session &session, unsigned id) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && session.matches(id, object->AsRobot())) return object->AsRobot();
    throw std::runtime_error("Missing lifecycle robot");
}
tests::battle::Profile profile(random::Mode mode, int step) {
    tests::battle::Profile p{mode, 79, step, 6000, false, false};
    p.layout = tests::NavigationLayout::Open;
    p.unavailable_icon_targets = true;
    return p;
}
void move(Session &session, CFormMatrixGame &form, int route, float x, float y) {
    if (!route) session.ground_command(x, y);
    else {
        if (route == 1) press(form, 'M');
        else button(IF_ORDER_MOVE);
        MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_MOVE));
        g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
        g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(x, y, 0);
        side().OnLButtonDown(CPoint(0, 0));
        MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE | PREORDER_MOVE));
    }
}
void stop(CFormMatrixGame &form, bool ui) {
    if (ui) button(IF_ORDER_STOP);
    else press(form, 'S');
}
void advance(Session &session, int duration) {
    const int until = session.time() + duration;
    while (session.time() < until) session.advance();
}
void intent(CMatrixRobotAI &robot, int order) {
    MG_CHECK(robot.GetGroupLogic() >= 0);
    auto &group = side().m_PlayerGroup[robot.GetGroupLogic()];
    MG_CHECK(group.Order() == order && group.m_Obj == nullptr);
}
struct EffectAccess : CMatrixEffect {
    static SSpotProperties &track() { return m_SpotProperties[SPOT_SOLE_TRACK]; }
    static SSpotProperties wheel() { return m_SpotProperties[SPOT_SOLE_WHEEL]; }
    static SSpotProperties &crater() { return m_SpotProperties[SPOT_VORONKA]; }
};
struct Effects {
    SSpotProperties track = EffectAccess::track(), crater = EffectAccess::crater();
    ~Effects() { EffectAccess::track() = track; EffectAccess::crater() = crater; }
    void install() {
        EffectAccess::track() = EffectAccess::wheel();
        auto &spot = EffectAccess::crater();
        spot = {};
        spot.color = 0xffffffff;
        spot.func = SpotTaktConstant;
        spot.texture = static_cast<CTextureManaged *>(g_Cache->Get(CacheClass::TextureManaged, L"synthetic-combat"));
        spot.ttl = 30000;
    }
};
void track_configuration() {
    g_Config.m_ItemChars[CHASSIS3_MOVE_SPEED] = .5f;
    g_Config.m_ItemChars[CHASSIS3_ROTATION_SPEED] = .04f;
    g_Config.m_ItemChars[CHASSIS3_STRUCTURE] = 1000;
    g_Config.m_ItemChars[CHASSIS3_MOVE_WATER_CORR] = .5f;
    g_Config.m_ItemChars[CHASSIS3_MOVE_SLOPE_CORR_DOWN] = 2.5f;
    g_Config.m_ItemChars[CHASSIS3_MOVE_SLOPE_CORR_UP] = .7f;
}
std::string observation(Session &session, CMatrixRobotAI &track) {
    std::ostringstream out;
    out << session.snapshot() << " track=" << std::bit_cast<uint32_t>(track.m_PosX) << ','
        << std::bit_cast<uint32_t>(track.m_PosY) << ',' << track.GetGroupLogic() << ','
        << track.GetOrdersInPool() << ',' << track.GetEnv()->m_Place << ',' << track.PLIsInPlace();
    return out.str();
}
std::vector<std::string> mixed_run(random::Mode mode, int step, int route, bool ui_stop) {
    Effects effects;
    Session session(profile(mode, step));
    effects.install();
    track_configuration();
    auto *wheel = find(session, 1), *unrelated = find(session, 2);
    auto *track = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 320, RUK_CHASSIS_TRACK);
    MG_CHECK(wheel->m_Unit[0].u1.s1.m_Kind == RUK_CHASSIS_WHEEL && wheel->GetMaxSpeed() == .6f);
    MG_CHECK(track->m_Unit[0].u1.s1.m_Kind == RUK_CHASSIS_TRACK && track->GetMaxSpeed() == .5f);
    CFormMatrixGame form;
    select({unrelated});
    session.ground_command(100, 500);
    const int other_group = unrelated->GetGroupLogic();
    const CPoint other_destination = side().m_PlayerGroup[other_group].m_To;
    select({wheel, track});
    move(session, form, route, 500, 300);
    intent(*wheel, mpo_MoveTo);
    MG_CHECK(wheel->GetGroupLogic() == track->GetGroupLogic());
    MG_CHECK(side().m_PlayerGroup[wheel->GetGroupLogic()].m_To == CPoint(48, 28));
    std::vector<std::string> result;
    while (session.time() < 1000) {
        result.push_back(observation(session, *track));
        session.advance();
    }
    MG_CHECK(wheel->m_PosX > 110 && track->m_PosX > 110);
    stop(form, ui_stop);
    intent(*wheel, mpo_Stop);
    intent(*track, mpo_Stop);
    if (!ui_stop) MG_CHECK(wheel->GetOrdersInPool() == 0 && track->GetOrdersInPool() == 0);
    else MG_CHECK(wheel->GetOrdersInPool() + track->GetOrdersInPool() > 0);
    advance(session, 6000);
    intent(*wheel, mpo_Stop);
    intent(*track, mpo_Stop);
    MG_CHECK(wheel->m_PosX < 400 && track->m_PosX < 400);
    const float wx = wheel->m_PosX, wy = wheel->m_PosY, tx = track->m_PosX, ty = track->m_PosY;
    advance(session, 1000);
    MG_CHECK(wheel->m_PosX == wx && wheel->m_PosY == wy && track->m_PosX == tx && track->m_PosY == ty);
    MG_CHECK(unrelated->GetGroupLogic() == other_group);
    MG_CHECK(side().m_PlayerGroup[other_group].Order() == mpo_MoveTo);
    MG_CHECK(side().m_PlayerGroup[other_group].m_To == other_destination && unrelated->m_PosY > 450);
    move(session, form, route, 500, 100);
    advance(session, 16000);
    intent(*wheel, mpo_MoveTo);
    intent(*track, mpo_MoveTo);
    MG_CHECK(wheel->PLIsInPlace() && track->PLIsInPlace());
    MG_CHECK(wheel->m_PosX > 400 && track->m_PosX > 400);
    MG_CHECK(wheel->m_PosY < 200 && track->m_PosY < 200);
    std::cout << session.metadata("mixed-command-lifecycle-v1") << " route=" << route
              << " button_stop=" << ui_stop << '\n';
    return result;
}
void mixed_commands() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16}) for (bool ui_stop : {false, true}) {
            const auto context = mixed_run(mode, step, 0, ui_stop);
            MG_CHECK(context == mixed_run(mode, step, 1, ui_stop));
            MG_CHECK(context == mixed_run(mode, step, 2, ui_stop));
        }
}
// Full transition from production RobotSpawn/OrderFlyer; no state is changed to
// make a rejected command executable. The original Move is either retained or
// explicitly replaced with Stop before the lifecycle finishes.
void nonready_commands(bool carried) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
      for (int step : {10, 16}) for (int route = 0; route < 3; ++route)
      for (bool cancel : {false, true}) {
        tests::BattleMaterialProfile materials;
        Session session(profile(mode, step));
        CFormMatrixGame form;
        // This fixture isolates player commands, not enemy base capture. Keep
        // the two unarmed opposition units stationary using ordinary speeds.
        for (unsigned id : {3u, 4u}) {
            auto *enemy = find(session, id);
            enemy->SetMaxSpeed(0);
            enemy->m_GroupSpeed = 0;
        }
        auto *unrelated = find(session, 2);
        CMatrixRobotAI *robot = nullptr;
        if (carried) {
            tests::install_battle_transport();
            const CPoint landing = g_MatrixMap->m_RN.m_Place[3].m_Pos;
            side().OrderFlyer(D3DXVECTOR2(float(landing.x * GLOBAL_SCALE_MOVE), float(landing.y * GLOBAL_SCALE_MOVE)),
                FO_GIVE_BOT, 0, 3, landing, 0);
            MG_CHECK(g_MatrixMap->m_AD_Obj_cnt == 1);
            auto *carrier = static_cast<CMatrixFlyer *>(g_MatrixMap->m_AD_Obj[0]);
            robot = static_cast<CMatrixRobotAI *>(carrier->GetCarryingRobot());
            MG_CHECK(robot && robot->m_CurrState == ROBOT_CARRYING);
        } else {
            auto base = tests::make_battle_building(*unrelated->m_Unit[0].m_Graph->VO(), PLAYER_SIDE,
                BUILDING_BASE, 100, 100);
            auto *factory = base.get();
            g_MatrixMap->AddObject(factory, true);
            base.release();
            g_Config.m_Timings[RESOURCE_BASE] = 100000;
            robot = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 100);
            robot->m_Forward = robot->m_HullForward = D3DXVECTOR3(0, 1, 0);
            robot->RobotSpawn(factory);
            MG_CHECK(robot->m_CurrState == ROBOT_IN_SPAWN && factory->IsSpawningBot());
        }
        select({unrelated});
        session.ground_command(100, 500);
        const int other_group = unrelated->GetGroupLogic();
        const CPoint other_destination = side().m_PlayerGroup[other_group].m_To;
        select({robot});
        move(session, form, route, 500, 100);
        intent(*robot, mpo_MoveTo);
        MG_CHECK(side().m_PlayerGroup[robot->GetGroupLogic()].m_To == CPoint(48, 8));
        MG_CHECK(robot->m_CurrState == (carried ? ROBOT_CARRYING : ROBOT_IN_SPAWN));
        const CPoint before_stop(robot->GetMapPosX(), robot->GetMapPosY());
        if (cancel) {
            stop(form, route == 2);
            intent(*robot, mpo_Stop);
            MG_CHECK(side().m_PlayerGroup[robot->GetGroupLogic()].m_To == before_stop);
            if (carried) MG_CHECK(before_stop == CPoint(0, 0));
        }
        const auto canceled_order_stays_canceled = [&] {
            intent(*robot, mpo_Stop);
            MG_CHECK(side().m_PlayerGroup[robot->GetGroupLogic()].m_To == before_stop);
            // Use the actual canceled destination cell, not a broad quadrant:
            // legitimate movement toward the Stop place can cross that quadrant.
            MG_CHECK(CPoint(robot->GetMapPosX(), robot->GetMapPosY()) != CPoint(48, 8));
            CPoint unit_destination;
            if (robot->GetMoveToCoords(unit_destination)) MG_CHECK(unit_destination != CPoint(48, 8));
        };
        int ready = -1, intermediate = -1;
        while (session.time() < (cancel ? 30000 : 18000)) {
            session.advance();
            if (intermediate < 0 && robot->m_CurrState == (carried ? ROBOT_FALLING : ROBOT_BASE_MOVEOUT))
                intermediate = session.time();
            if (ready < 0 && robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD) ready = session.time();
            intent(*robot, cancel ? mpo_Stop : mpo_MoveTo);
            if (cancel && ready >= 0) canceled_order_stays_canceled();
        }
        MG_CHECK(intermediate > 0 && ready > intermediate);
        MG_CHECK(robot->GetBase() == nullptr);
        intent(*robot, cancel ? mpo_Stop : mpo_MoveTo);
        MG_CHECK(unrelated->GetGroupLogic() == other_group);
        MG_CHECK(side().m_PlayerGroup[other_group].Order() == mpo_MoveTo && unrelated->m_PosY > 450);
        MG_CHECK(side().m_PlayerGroup[other_group].m_To == other_destination);
        if (!cancel) MG_CHECK(robot->m_PosX > 400 && robot->m_PosY < 200 && robot->PLIsInPlace());
        else {
            // Stop cannot prevent required base egress or carrier movement, but
            // the old Move must not resume once the unit can move independently.
            MG_CHECK(robot->m_PosX < 400 || robot->m_PosY > 200);
            const float stopped_x = robot->m_PosX, stopped_y = robot->m_PosY;
            const int stationary_until = session.time() + 1000;
            while (session.time() < stationary_until) {
                session.advance();
                canceled_order_stays_canceled();
                MG_CHECK(robot->m_PosX == stopped_x && robot->m_PosY == stopped_y);
            }
            intent(*robot, mpo_Stop);
            MG_CHECK(side().m_PlayerGroup[robot->GetGroupLogic()].m_To == before_stop);
            if (carried) {
                // The two schedules have different bounded outcomes. Do not
                // turn the 10 ms non-arrival into an assumed successful Stop.
                if (step == 10) MG_CHECK(!robot->PLIsInPlace() && robot->GetMapPosX() == 46 &&
                    robot->GetMapPosY() >= 44 && robot->GetMapPosY() <= 45);
                else MG_CHECK(robot->PLIsInPlace() && robot->m_PosX == 40 && robot->m_PosY == 40);
            }
        }
        // A new command after the lifecycle must replace any earlier place/order.
        move(session, form, route, 120, 500);
        advance(session, 16000);
        intent(*robot, mpo_MoveTo);
        MG_CHECK(side().m_PlayerGroup[robot->GetGroupLogic()].m_To == CPoint(10, 48));
        MG_CHECK(robot->m_PosY > 450 && robot->m_PosX < 250 && robot->PLIsInPlace());
        std::cout << session.metadata(carried ? "carried-command-lifecycle-v1" : "spawn-command-lifecycle-v1")
                  << " route=" << route << " canceled=" << cancel << " intermediate=" << intermediate
                  << " ready=" << ready << '\n';
      }
}
void spawning_commands() { nonready_commands(false); }
void carried_commands() { nonready_commands(true); }
void dying_commands() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        tests::BattleCombatAssets assets;
        Effects effects;
        Session session(profile(mode, step));
        assets.install(); effects.install();
        auto *robot = find(session, 1), *survivor = find(session, 2);
        CFormMatrixGame form;
        select({robot, survivor});
        move(session, form, 0, 500, 100);
        advance(session, 500);
        std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(robot->GetCore(DEBUG_CALL_INFO),
            [](SObjectCore *value) { value->Release(); });
        g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {10000, 10000, 0};
        MG_CHECK(robot->Damage(WEAPON_GUN, robot->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
        MG_CHECK(robot->m_CurrState == ROBOT_DIP && core->m_Object == robot);
        MG_CHECK(side().GetCurGroup() && side().GetCurGroup()->GetObjectsCnt() == 1);
        MG_CHECK(side().GetCurGroup()->GetObjectByN(0) == survivor);
        for (int route = 0; route < 3; ++route) {
            // Commands affect the surviving selection only; the dead member
            // remains a real debris object while these requests are submitted.
            move(session, form, route, 120, 500);
            intent(*survivor, mpo_MoveTo);
            MG_CHECK(side().m_PlayerGroup[survivor->GetGroupLogic()].m_RobotCnt == 1);
            stop(form, route == 2);
            intent(*survivor, mpo_Stop);
            MG_CHECK(robot->m_CurrState == ROBOT_DIP);
        }
        advance(session, 6000);
        MG_CHECK(core->m_Object == nullptr && !session.robot(1).alive);
        select({survivor});
        move(session, form, 2, 120, 500);
        advance(session, 16000);
        MG_CHECK(survivor->PLIsInPlace() && survivor->m_PosY > 450);
        MG_CHECK(!session.robot(1).alive);
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
        std::cout << session.metadata("dying-command-lifecycle-v1") << '\n';
    }
}
constexpr tests::Case cases[] = {
    {"game.player.mixed_command_lifecycle", mixed_commands},
    {"game.player.spawning_command_lifecycle", spawning_commands},
    {"game.player.carried_command_lifecycle", carried_commands},
    {"game.player.dying_command_lifecycle", dying_commands},
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
