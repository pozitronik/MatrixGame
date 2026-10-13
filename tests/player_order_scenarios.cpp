// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_geometry.hpp"
#include "test_support.hpp"
#include "command_world.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceElement.h"
#include "input.hpp"
#include "cheats.hpp"
#include <csignal>
#include <vector>
#include <iostream>

namespace {
using namespace tests::battle;
CMatrixSideUnit &side() { return *g_MatrixMap->GetPlayerSide(); }
void press(CFormMatrixGame &form, uint8_t key) {
    form.Keyboard(true, key);
    form.Keyboard(false, key);
    MG_CHECK(!Input::isVKeyPressed(key));
}
void ground(float x, float y) {
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
    g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(x, y, 0);
    side().OnLButtonDown(CPoint(0, 0));
}
void expect_order(const Session &session, int order) {
    for (auto id : {1u, 2u}) MG_CHECK(session.robot(id).order == order);
    MG_CHECK(session.robot(1).group == session.robot(2).group);
    MG_CHECK(side().m_PlayerGroup[session.robot(1).group].m_RobotCnt == 2);
}
CMatrixRobotAI *selected(int index) {
    auto *object = side().GetCurGroup()->GetObjectByN(index);
    MG_CHECK(object && object->IsLiveRobot());
    return object->AsRobot();
}
std::vector<std::string> move_run(random::Mode mode, int route) {
    Session session({mode});
    CFormMatrixGame form;
    std::cerr << session.metadata() << '\n';
    if (route) {
        if (route == 1) press(form, 'M');
        else {
            auto button = tests::make_engine_object<CIFaceElement>();
            button->m_strName = IF_ORDER_MOVE;
            g_IFaceList->PlayerAction(button.get());
        }
        MG_CHECK((g_IFaceList->m_IfListFlags & (PREORDER_MOVE | ORDERING_MODE)) == (PREORDER_MOVE | ORDERING_MODE));
        ground(500, 300);
        MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, PREORDER_MOVE | ORDERING_MODE));
    } else session.ground_command(500, 300);
    expect_order(session, mpo_MoveTo);
    MG_CHECK(session.robot(1).destination_x == 48 && session.robot(1).destination_y == 28);
    std::vector<std::string> result;
    for (int frame = 0; frame < 1500; ++frame) {
        result.push_back(session.snapshot());
        session.advance();
    }
    result.push_back(session.snapshot());
    for (auto id : {1u, 2u}) {
        MG_CHECK(session.robot(id).x > 400);
        MG_CHECK(session.robot(id).in_place);
    }
    return result;
}
void move_routes() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const auto context = move_run(mode, false);
        const auto explicit_order = move_run(mode, 1);
        const auto button_order = move_run(mode, 2);
        MG_CHECK(explicit_order == button_order);
        MG_CHECK(context.size() == explicit_order.size());
        for (size_t i = 0; i < context.size(); ++i)
            if (context[i] != explicit_order[i])
                throw std::runtime_error("Move route divergence at boundary " + std::to_string(i) +
                                         "\nContext: " + context[i] + "\nExplicit: " + explicit_order[i]);
    }
}
void preorder_cancellation() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CFormMatrixGame form;
        session.ground_command(500, 300);
        for (int i = 0; i < 50; ++i) session.advance();
        for (auto key : {'M', 'A', 'K', 'P'}) {
            const auto before = session.snapshot();
            press(form, key);
            MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
            press(form, 'X');
            MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
            ground(120, 500); // A late click after cancellation cannot submit the abandoned order.
            MG_CHECK(session.snapshot() == before);
        }
        for (int i = 0; i < 1450; ++i) session.advance();
        MG_CHECK(session.robot(1).x > 400 && session.robot(2).x > 400);
    }
}
void capability_rejection() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CFormMatrixGame form;
        MG_CHECK(side().GetCurGroup()->GetBombersCnt() == 0);
        MG_CHECK(side().GetCurGroup()->GetRepairsCnt() == 0);
        const auto before = session.snapshot();
        for (auto key : {'E', 'R'}) {
            press(form, key);
            MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE | PREORDER_BOMB | PREORDER_REPAIR));
            MG_CHECK(session.snapshot() == before);
        }
        press(form, 'K');
        ground(500, 300); // Ground is not a capturable building: keep the pending order.
        MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_CAPTURE));
        MG_CHECK(session.snapshot() == before);
        press(form, 'X');
        MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
    }
}
void stop_key() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CFormMatrixGame form;
        session.ground_command(500, 300);
        for (int i = 0; i < 100; ++i) session.advance();
        press(form, 'S');
        expect_order(session, mpo_Stop);
        for (int i = 0; i < 2; ++i) MG_CHECK(selected(i)->GetOrdersInPool() == 0);
        for (int i = 0; i < 500; ++i) session.advance();
        expect_order(session, mpo_Stop);
        MG_CHECK(session.robot(1).x < 400 && session.robot(2).x < 400);
        MG_CHECK(session.robot(1).target_is_null && session.robot(2).target_is_null);
    }
}
void stop_button() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        session.ground_command(500, 300);
        for (int i = 0; i < 100; ++i) session.advance();
        auto button = tests::make_engine_object<CIFaceElement>();
        button->m_strName = IF_ORDER_STOP;
        g_IFaceList->PlayerAction(button.get());
        expect_order(session, mpo_Stop);
        const int pending = selected(0)->GetOrdersInPool() + selected(1)->GetOrdersInPool();
        std::cerr << "Stop button pending unit orders: " << pending << '\n';
        MG_CHECK(pending > 0); // Unlike the keyboard, the button retains TaktPL's unit orders.
        for (int i = 0; i < 500; ++i) session.advance();
        expect_order(session, mpo_Stop);
        MG_CHECK(session.robot(1).x < 400 && session.robot(2).x < 400);
    }
}
void attack_routes() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CFormMatrixGame form;
        press(form, 'A');
        ground(500, 300);
        expect_order(session, mpo_Attack);
        MG_CHECK(session.robot(1).target_is_null);
        MG_CHECK(session.robot(1).destination_x == 48 && session.robot(1).destination_y == 28);
        press(form, 'A');
        auto *friendly = selected(1);
        g_MatrixMap->m_TraceStopObj = friendly;
        side().OnLButtonDown(CPoint(0, 0));
        expect_order(session, mpo_Attack);
        MG_CHECK(session.robot(1).target == 2); // Explicit attack accepts a friendly live robot.
        MG_CHECK(session.target_command(3));
        expect_order(session, mpo_Attack);
        MG_CHECK(session.robot(1).target == 3);
        session.ground_command(120, 500);
        expect_order(session, mpo_MoveTo);
        MG_CHECK(session.robot(1).target_is_null);
        for (int i = 0; i < 100; ++i) session.advance();
        MG_CHECK(session.robot(1).target_is_null);
    }
}
void patrol_reversal() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CFormMatrixGame form;
        g_MatrixData->BlockGet(L"Chars")->BlockGet(L"ChassisSounds")->BlockGet(L"2")
            ->ParSetAdd(L"Patrol", L"Sound.Synthetic");
        press(form, 'P');
        ground(180, 300);
        expect_order(session, mpo_Patrol);
        const auto group = session.robot(1).group;
        MG_CHECK(side().m_PlayerGroup[group].IsPatrolReturn()); // The synchronous first TaktPL starts the outbound leg.
        bool returned = false;
        for (int i = 0; i < 2000; ++i) {
            session.advance();
            expect_order(session, mpo_Patrol);
            if (!side().m_PlayerGroup[group].IsPatrolReturn()) returned = true;
        }
        MG_CHECK(returned);
        press(form, 'S');
        expect_order(session, mpo_Stop);
    }
}
void auto_modes() {
    const uint8_t keys[] = {'U', 'C', 'D'};
    const int orders[] = {mpo_AutoAttack, mpo_AutoCapture, mpo_AutoDefence};
    const DWORD flags[] = {AUTO_FROBOT_ON, AUTO_CAPTURE_ON, AUTO_PROTECT_ON};
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        for (int index = 0; index < 3; ++index) {
            Session session({mode});
            CFormMatrixGame form;
            press(form, keys[index]);
            expect_order(session, orders[index]);
            MG_CHECK((g_IFaceList->m_IfListFlags & (AUTO_FROBOT_ON | AUTO_CAPTURE_ON | AUTO_PROTECT_ON)) == flags[index]);
            for (int i = 0; i < 100; ++i) session.advance();
            expect_order(session, index == 1 ? mpo_Stop : orders[index]);
            press(form, keys[index]);
            expect_order(session, mpo_Stop);
            MG_CHECK(!(g_IFaceList->m_IfListFlags & (AUTO_FROBOT_ON | AUTO_CAPTURE_ON | AUTO_PROTECT_ON)));
            for (int i = 0; i < 100; ++i) session.advance();
            expect_order(session, mpo_Stop);
        }
    }
}
void control_groups() {
    Session session;
    CFormMatrixGame form;
    auto *first = selected(0);
    auto *second = selected(1);
    form.Keyboard(true, VK_CONTROL);
    press(form, '1');
    form.Keyboard(false, VK_CONTROL);
    MG_CHECK(first->GetCtrlGroup() == '1' && second->GetCtrlGroup() == '1');
    press(form, 'M');
    press(form, '1');
    MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
    MG_CHECK(side().GetCurGroup()->GetRobotsCnt() == 2);
    MG_CHECK(side().GetCurGroup()->GetObjectByN(0) == first);
    MG_CHECK(side().GetCurGroup()->GetObjectByN(1) == second);
    MG_CHECK(session.remove(2));
    press(form, '2'); // Avoid the double-press camera shortcut on the next recall.
    press(form, '1');
    MG_CHECK(side().GetCurGroup()->GetRobotsCnt() == 1);
    MG_CHECK(side().GetCurGroup()->GetObjectByN(0) == first);
    session.ground_command(120, 500);
    MG_CHECK(session.robot(1).order == mpo_MoveTo);
    MG_CHECK(!session.robot(2).alive);
}
void group_add_cap() {
    Session session;
    auto *first = selected(0);
    auto *second = selected(1);
    std::vector<CMatrixRobotAI *> additional;
    for (int i = 0; i < 8; ++i)
        additional.push_back(tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 300 + i * 30));
    side().CreateGroupFromCurrent();
    side().GetCurSelGroup()->AddObject(first, -4); // Existing members are not duplicated.
    for (auto *robot : additional) side().GetCurSelGroup()->AddObject(robot, -4);
    side().AddToCurrentGroup();
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 9);
    MG_CHECK(selected(0) == first && selected(1) == second);
    for (int i = 0; i < 7; ++i) MG_CHECK(selected(i + 2) == additional[i]);
    MG_CHECK(!side().GetCurGroup()->FindObject(additional.back()));
    MG_CHECK(side().GetCurSelGroup()->GetObjectsCnt() == 0);
    const int group = side().SelGroupToLogicGroup();
    MG_CHECK(side().m_PlayerGroup[group].m_RobotCnt == 9);
    for (int i = 0; i < 9; ++i) MG_CHECK(selected(i)->GetGroupLogic() == group);
    MG_CHECK(additional.back()->GetGroupLogic() == -1);
}
void cheat_dispatch_gate() {
    Profile profile;
    profile.initially_selected = false;
    Session session(profile);
    CFormMatrixGame form;
    const DWORD flags = g_Flags;
    for (char key : std::string("KEEPALIVE")) press(form, key);
#ifdef CHEATS_ON
    MG_CHECK(g_Flags == (flags ^ GFLAG_KEEPALIVE));
#else
    MG_CHECK(g_Flags == flags);
#endif
    for (char key : std::string("KEEPALIVE")) press(form, key);
    MG_CHECK(g_Flags == flags);
    const int resources = side().GetResourcesAmount(TITAN);
    for (char key : std::string("RICHIERICH")) press(form, key);
#ifdef CHEATS_ON
    MG_CHECK(side().GetResourcesAmount(TITAN) == 9000);
#else
    MG_CHECK(side().GetResourcesAmount(TITAN) == resources);
#endif
    MG_CHECK(session.robot(1).group == -1 && session.robot(2).group == -1);
}
void manual_focus() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CFormMatrixGame form;
        auto *robot = selected(0);
        press(form, VK_RETURN);
        const uint8_t forward = g_Config.m_KeyActions[KA_UNIT_FORWARD];
        const uint8_t backward = g_Config.m_KeyActions[KA_UNIT_BACKWARD];
        const float x = robot->m_PosX;
        form.Keyboard(true, forward);
        form.Keyboard(true, backward); // Production gives forward precedence while both are held.
        for (int i = 0; i < 30; ++i) session.advance();
        MG_CHECK(robot->m_PosX > x);
        MG_CHECK(side().GetArcadedObject() == robot);
        form.SystemEvent(SYSEV_INPUT_RESET);
        MG_CHECK(!Input::isVKeyPressed(forward) && !Input::isVKeyPressed(backward));
        session.advance();
        MG_CHECK(!robot->FindOrderLikeThat(ROT_MOVE_TO));
        MG_CHECK(!robot->FindOrderLikeThat(ROT_MOVE_TO_BACK));
        const float stopped_x = robot->m_PosX, stopped_y = robot->m_PosY;
        for (int i = 0; i < 20; ++i) session.advance();
        MG_CHECK(robot->m_PosX == stopped_x && robot->m_PosY == stopped_y);
        MG_CHECK(side().GetArcadedObject() == robot); // Input reset does not exit direct control.
        const uint8_t alternate = g_Config.m_KeyActions[KA_UNIT_FORWARD_ALT];
        form.Keyboard(true, alternate);
        session.advance();
        MG_CHECK(robot->m_PosX > stopped_x);
        form.SystemEvent(SYSEV_DEACTIVATING);
        session.advance();
        MG_CHECK(!Input::isVKeyPressed(alternate));
        MG_CHECK(!robot->FindOrderLikeThat(ROT_MOVE_TO));
        press(form, VK_RETURN);
        MG_CHECK(!side().IsArcadeMode());
        session.ground_command(120, 500);
        MG_CHECK(session.robot(1).order == mpo_MoveTo);
        for (int i = 0; i < 1500; ++i) session.advance();
        MG_CHECK(session.robot(1).y > 450 && session.robot(1).in_place);
    }
}
void controlled_unit_removal() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CFormMatrixGame form;
        press(form, VK_RETURN);
        MG_CHECK(side().GetArcadedObject() != nullptr);
        const uint8_t forward = g_Config.m_KeyActions[KA_UNIT_FORWARD];
        form.Keyboard(true, forward);
        session.advance();
        MG_CHECK(session.remove(1));
        MG_CHECK(!session.robot(1).alive);
        MG_CHECK(side().GetArcadedObject() == nullptr);
        MG_CHECK(!side().IsArcadeMode());
        // Release after retirement must not redirect the held command to another unit.
        form.Keyboard(false, forward);
        MG_CHECK(!Input::isVKeyPressed(forward));
        const auto survivor = session.robot(2);
        for (int i = 0; i < 20; ++i) session.advance();
        MG_CHECK(session.robot(2).x == survivor.x && session.robot(2).y == survivor.y);
        MG_CHECK(session.robot(2).group == survivor.group);
    }
}
void manual_exit_held() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CFormMatrixGame form;
        auto *robot = selected(0);
        const float speed = robot->GetMaxSpeed();
        press(form, VK_RETURN);
        const uint8_t forward = g_Config.m_KeyActions[KA_UNIT_FORWARD];
        form.Keyboard(true, forward);
        session.advance();
        press(form, VK_RETURN);
        MG_CHECK(!side().IsArcadeMode());
        MG_CHECK(Input::isVKeyPressed(forward)); // Leaving the robot does not synthesize a key release.
        MG_CHECK(robot->GetMaxSpeed() == speed);
        MG_CHECK(session.robot(1).order == mpo_Attack && session.robot(1).target_is_null);
        session.ground_command(120, 500);
        MG_CHECK(session.robot(1).order == mpo_MoveTo);
        MG_CHECK(session.robot(2).group == -1);
        for (int i = 0; i < 100; ++i) session.advance();
        form.Keyboard(false, forward);
        MG_CHECK(!Input::isVKeyPressed(forward));
        for (int i = 0; i < 1400; ++i) session.advance();
        MG_CHECK(session.robot(1).y > 450 && session.robot(1).in_place);
        MG_CHECK(session.robot(2).x == 100 && session.robot(2).y == 260);
    }
}
void manual_control() {
    Session session;
    CFormMatrixGame form;
    auto *robot = selected(0);
    const auto speed = robot->GetMaxSpeed();
    press(form, VK_RETURN);
    MG_CHECK(side().GetArcadedObject() == robot);
    MG_CHECK(side().IsRobotMode());
    MG_CHECK(robot->GetMaxSpeed() == speed * 1.1f);
    MG_CHECK(robot->GetOrdersInPool() == 0);
    side().OnForward(true);
    MG_CHECK(robot->FindOrderLikeThat(ROT_MOVE_TO));
    side().OnForward(false);
    MG_CHECK(!robot->FindOrderLikeThat(ROT_MOVE_TO));
    side().OnBackward(true);
    MG_CHECK(robot->FindOrderLikeThat(ROT_MOVE_TO_BACK));
    side().OnBackward(false);
    MG_CHECK(!robot->FindOrderLikeThat(ROT_MOVE_TO_BACK));
    press(form, VK_RETURN);
    MG_CHECK(!side().IsArcadeMode());
    MG_CHECK(robot->GetMaxSpeed() == speed);
    MG_CHECK(side().m_PlayerGroup[robot->GetGroupLogic()].Order() == mpo_Attack);
    MG_CHECK(side().m_PlayerGroup[robot->GetGroupLogic()].m_Obj == nullptr);
}
constexpr tests::Case cases[] = {
    {"game.player.move_routes", move_routes},
    {"game.player.preorder_cancellation", preorder_cancellation},
    {"game.player.capability_rejection", capability_rejection},
    {"game.player.stop_key", stop_key},
    {"game.player.stop_button", stop_button},
    {"game.player.attack_routes", attack_routes},
    {"game.player.control_groups", control_groups},
    {"game.player.group_add_cap", group_add_cap},
    {"game.player.patrol_reversal", patrol_reversal},
    {"game.player.auto_modes", auto_modes},
    {"game.player.manual_control", manual_control},
    {"game.player.controlled_unit_removal", controlled_unit_removal},
    {"game.player.manual_exit_held", manual_exit_held},
    {"game.player.manual_focus", manual_focus},
    {"game.player.cheat_dispatch_gate", cheat_dispatch_gate},
};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    if (argc == 2 && std::string_view(argv[1]) == "probe.intentional_crash") {
        // Convert the intentional abort signal to a unique child-process result.
        // No battle or asset setup can accidentally satisfy this contract.
        std::signal(SIGABRT, [](int) { std::_Exit(86); });
        std::cerr << "Dispatching intentional CRASH command" << std::endl;
        for (char key : std::string("CRASH")) Cheats::processInput(key);
        return 87; // A removed/disabled handler must fail the wrapper test.
    }
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Player order scenarios leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
