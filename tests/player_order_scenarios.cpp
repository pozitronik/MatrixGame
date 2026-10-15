// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_geometry.hpp"
#include "battle_building.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "command_world.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixObjectCannon.hpp"
#include "MatrixMultiSelection.hpp"
#include "Effects/MatrixEffectLandscapeSpot.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceElement.h"
#include "input.hpp"
#include "cheats.hpp"
#include <algorithm>
#include <initializer_list>
#include <csignal>
#include <cstdlib>
#include <vector>
#include <iostream>

// Reuse the existing visibility-fixture friendship without substituting InRect.
// Only the precomputed visible-object order is supplied; mesh picking is real.
struct SelectionFixture {
    SelectionFixture(std::initializer_list<CMatrixMapStatic *> visible) {
        MG_CHECK(CMultiSelection::m_GameSelection == nullptr);
        CMatrixMapStatic::objects_left = 0;
        CMatrixMapStatic::objects_rite = 0;
        for (auto *object : visible) append(object);
        g_IFaceList->m_InFocus = UNKNOWN;
    }
    void append(CMatrixMapStatic *object) {
        MG_CHECK(CMatrixMapStatic::objects_rite < MAX_OBJECTS_PER_SCREEN);
        CMatrixMapStatic::objects[CMatrixMapStatic::objects_rite++] = object;
    }
    ~SelectionFixture() {
        while (CMultiSelection::m_First) {
            auto *selection = CMultiSelection::m_First;
            HDelete(CMultiSelection, selection, g_MatrixHeap);
        }
        for (int i = 0; i < CMatrixMapStatic::objects_rite; ++i) CMatrixMapStatic::objects[i] = nullptr;
        CMatrixMapStatic::objects_left = CMatrixMapStatic::objects_rite = 0;
    }
};

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
void patrol_interruption(bool stop) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (bool returning : {false, true}) {
        Profile profile{mode, 80, 10, 5000, false};
        profile.layout = tests::NavigationLayout::Open;
        Session session(profile);
        CFormMatrixGame form;
        std::cout << session.metadata("player-patrol-interruption-v1") << " stop=" << stop
                  << " returning=" << returning << '\n';
        g_MatrixData->BlockGet(L"Chars")->BlockGet(L"ChassisSounds")->BlockGet(L"2")
            ->ParSetAdd(L"Patrol", L"Sound.Synthetic");
        press(form, 'P');
        ground(180, 300);
        expect_order(session, mpo_Patrol);
        const int group = session.robot(1).group;
        for (int i = 0; i < 2000; ++i) {
            session.advance();
            if (returning ? !side().m_PlayerGroup[group].IsPatrolReturn() : session.time() >= 500) break;
        }
        MG_CHECK(side().m_PlayerGroup[group].IsPatrolReturn() == !returning);
        // Canceling a pending replacement must leave the existing patrol intact.
        const auto before = session.snapshot();
        press(form, 'M');
        press(form, 'X');
        ground(500, 200);
        MG_CHECK(session.snapshot() == before);
        expect_order(session, mpo_Patrol);
        const auto before_stop_first = session.robot(1), before_stop_second = session.robot(2);
        if (stop) press(form, 'S');
        else {
            press(form, 'M');
            ground(500, 200);
        }
        expect_order(session, stop ? mpo_Stop : mpo_MoveTo);
        const auto destination = session.robot(1);
        float settled_x[2]{}, settled_y[2]{};
        for (int i = 0; i < 1800; ++i) {
            session.advance();
            expect_order(session, stop ? mpo_Stop : mpo_MoveTo);
            MG_CHECK(session.robot(1).destination_x == destination.destination_x);
            MG_CHECK(session.robot(1).destination_y == destination.destination_y);
            MG_CHECK(session.robot(1).target_is_null && session.robot(2).target_is_null);
            if (stop && i >= 1299) for (int unit = 0; unit < 2; ++unit) {
                const auto state = session.robot(unit + 1);
                if (i == 1299) { settled_x[unit] = state.x; settled_y[unit] = state.y; }
                else MG_CHECK(state.x == settled_x[unit] && state.y == settled_y[unit]);
            }
        }
        if (stop) {
            for (const auto before : {before_stop_first, before_stop_second}) {
                const auto after = session.robot(before.id);
                const float dx = after.x - before.x, dy = after.y - before.y;
                MG_CHECK(dx * dx + dy * dy <= 100.0f * 100.0f);
            }
            MG_CHECK(!selected(0)->FindOrderLikeThat(ROT_MOVE_TO));
            MG_CHECK(!selected(1)->FindOrderLikeThat(ROT_MOVE_TO));
        } else {
            MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
            MG_CHECK(session.robot(1).x == 500 && session.robot(1).y == 200);
        }
    }
}
void patrol_stop_lifecycle() { patrol_interruption(true); }
void patrol_replacement_lifecycle() { patrol_interruption(false); }

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
    side().CreateGroupFromCurrent(first);
    side().Select(ROBOT, nullptr);
    MG_CHECK(side().GetCurGroup()->GetRobotsCnt() == 1);
    MG_CHECK(side().GetCurGroup()->GetObjectByN(0) == first);
    press(form, 'M');
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
    press(form, '1');
    MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
    MG_CHECK(side().GetCurGroup()->GetRobotsCnt() == 2);
    MG_CHECK(side().GetCurGroup()->GetObjectByN(0) == first);
    MG_CHECK(side().GetCurGroup()->GetObjectByN(1) == second);
    MG_CHECK(session.remove(2));
    side().Select(NOTHING, nullptr);
    MG_CHECK(side().GetCurGroup() == nullptr);
    MG_CHECK(side().GetCurSelGroup()->GetObjectsCnt() == 0);
    press(form, '1');
    MG_CHECK(side().GetCurGroup()->GetRobotsCnt() == 1);
    MG_CHECK(side().GetCurGroup()->GetObjectByN(0) == first);
    session.ground_command(120, 500);
    MG_CHECK(session.robot(1).order == mpo_MoveTo);
    MG_CHECK(!session.robot(2).alive);
}
void empty_control_group() {
    Session session;
    CFormMatrixGame form;
    auto *first = selected(0);
    auto *second = selected(1);
    press(form, 'M');
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
    press(form, '2');
    MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
    MG_CHECK(side().GetCurGroup()->GetRobotsCnt() == 2);
    MG_CHECK(side().GetCurGroup()->GetObjectByN(0) == first);
    MG_CHECK(side().GetCurGroup()->GetObjectByN(1) == second);
    side().Select(NOTHING, nullptr);
    MG_CHECK(side().GetCurGroup() == nullptr);
    press(form, '2');
    MG_CHECK(side().GetCurGroup() == nullptr);
    MG_CHECK(side().GetCurSelGroup()->GetObjectsCnt() == 0);
    MG_CHECK(session.robot(1).alive && session.robot(2).alive);
}
void control_group_camera() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int elapsed : {0, 190, 200, 210}) {
        Profile profile; profile.mode = mode;
        Session session(profile);
        CFormMatrixGame form;
        std::cout << session.metadata() << "\nControl-group recall interval: " << elapsed << " ms\n";
        auto *first = selected(0);
        auto *second = selected(1);
        form.Keyboard(true, VK_CONTROL);
        press(form, '1');
        form.Keyboard(false, VK_CONTROL);
        // Prime the production function's static recall state through another key.
        // This also isolates repetitions that construct a fresh map at time zero.
        press(form, '2');
        auto &camera = g_MatrixMap->m_Camera;
        const D3DXVECTOR2 initial(320, 320);
        camera.SetXYStrategy(initial);
        press(form, '1');
        MG_CHECK(camera.GetXYStrategy().x == initial.x && camera.GetXYStrategy().y == initial.y);
        for (int time = 0; time < elapsed; time += profile.step_ms) session.advance();
        MG_CHECK(session.time() == elapsed);
        camera.SetXYStrategy(initial);
        press(form, '1');
        const D3DXVECTOR2 expected = elapsed < 200
            ? D3DXVECTOR2(first->GetGeoCenter().x, first->GetGeoCenter().y + 100)
            : initial;
        MG_CHECK(camera.GetXYStrategy().x == expected.x && camera.GetXYStrategy().y == expected.y);
        MG_CHECK(side().GetCurGroup()->GetRobotsCnt() == 2);
        MG_CHECK(side().GetCurGroup()->GetObjectByN(0) == first);
        MG_CHECK(side().GetCurGroup()->GetObjectByN(1) == second);
    }
}
void debug_reinforcement_reset() {
    Profile profile; profile.initially_selected = false;
    Session session(profile);
    CFormMatrixGame form;
    for (auto *trace : {TRACE_STOP_LANDSCAPE, static_cast<CMatrixMapStatic *>(nullptr)})
        for (int remaining : {0, 1234}) {
        g_MatrixMap->m_TraceStopObj = trace;
        g_MatrixMap->SetMaintenanceTime(remaining);
        press(form, 'F');
#if defined(_DEBUG) && !defined(_RELDEBUG)
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 0);
#else
        MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == remaining);
#endif
        MG_CHECK(session.time() == 0 && session.frame() == 0);
        MG_CHECK(session.robot(1).alive && session.robot(2).alive);
    }
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
CPoint screen_center(CMatrixRobotAI &robot) {
    D3DXMATRIX identity;
    D3DXMatrixIdentity(&identity);
    const auto p = g_MatrixMap->m_Camera.Project(robot.GetGeoCenter(), identity);
    return CPoint(static_cast<int>(p.x), static_cast<int>(p.y));
}
void drag(CFormMatrixGame &form, CPoint from, CPoint to) {
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
    form.MouseMove(from.x, from.y);
    form.MouseKey(B_DOWN, VK_LBUTTON, from.x, from.y);
    MG_CHECK(CMultiSelection::m_GameSelection != nullptr);
    form.MouseMove(to.x, to.y);
    form.MouseKey(B_UP, VK_LBUTTON, to.x, to.y);
    MG_CHECK(CMultiSelection::m_GameSelection == nullptr);
    MG_CHECK(!Input::isVKeyPressed(VK_LBUTTON));
}
void mesh_selection() {
    Session session;
    CFormMatrixGame form;
    auto *first = selected(0), *second = selected(1);
    auto *enemy = tests::BattleGeometry::create_robot(*g_MatrixMap, 2, 100, 200);
    enemy->SetTeam(0);
    auto flyer = tests::make_engine_object<CMatrixFlyer>();
    flyer->SetSide(PLAYER_SIDE);
    SelectionFixture visible{enemy, flyer.get(), first, second};
    const auto a = screen_center(*first), b = screen_center(*second);
    MG_CHECK(first->InRect(CRect(a.x - 2, a.y - 2, a.x + 2, a.y + 2)));
    MG_CHECK(enemy->InRect(CRect(a.x - 2, a.y - 2, a.x + 2, a.y + 2)));
    side().Select(NOTHING, nullptr);
    side().ResetSystemSelection();
    drag(form, CPoint(a.x - 2, a.y - 2), CPoint(a.x + 2, a.y + 2));
    MG_CHECK(side().m_CurrSel == ROBOT_SELECTED);
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 1 && selected(0) == first);
    form.Keyboard(true, g_Config.m_KeyActions[KA_SHIFT]);
    drag(form, CPoint(b.x - 2, b.y - 2), CPoint(b.x + 2, b.y + 2));
    form.Keyboard(false, g_Config.m_KeyActions[KA_SHIFT]);
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 2);
    MG_CHECK(selected(0) == first && selected(1) == second);
    form.Keyboard(true, g_Config.m_KeyActions[KA_SHIFT]);
    drag(form, CPoint(a.x - 2, a.y - 2), CPoint(a.x + 2, a.y + 2));
    form.Keyboard(false, g_Config.m_KeyActions[KA_SHIFT]);
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 1 && selected(0) == second);
    // A reversed rectangle finds both meshes in visibility order.
    drag(form, CPoint(std::max(a.x, b.x) + 20, std::max(a.y, b.y) + 20),
               CPoint(std::min(a.x, b.x) - 20, std::min(a.y, b.y) - 20));
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 2);
    MG_CHECK(selected(0) == first && selected(1) == second);
    drag(form, CPoint(0, 0), CPoint(2, 2));
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 2); // Empty drag retains the selected group.
    MG_CHECK(selected(0) == first && selected(1) == second);
    session.ground_command(500, 300);
    expect_order(session, mpo_MoveTo);
    for (int i = 0; i < 1500; ++i) session.advance();
    MG_CHECK(session.robot(1).x > 400 && session.robot(2).x > 400);
}
void shift_rectangle_group_cap() {
    Session session;
    CFormMatrixGame form;
    auto *first = selected(0), *second = selected(1);
    SelectionFixture visible{first, second};
    std::vector<CMatrixRobotAI *> overlap;
    for (int i = 0; i < 9; ++i) {
        auto *robot = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 200);
        overlap.push_back(robot);
        visible.append(robot);
    }
    const auto a = screen_center(*first), b = screen_center(*second);
    side().Select(NOTHING, nullptr);
    side().ResetSystemSelection();
    // A real form selection avoids the session's initial group alias.
    drag(form, CPoint(b.x - 2, b.y - 2), CPoint(b.x + 2, b.y + 2));
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 1 && selected(0) == second);
    auto shift_drag = [&] {
        form.Keyboard(true, g_Config.m_KeyActions[KA_SHIFT]);
        drag(form, CPoint(a.x - 10, a.y - 10), CPoint(a.x + 10, a.y + 10));
        form.Keyboard(false, g_Config.m_KeyActions[KA_SHIFT]);
    };
    shift_drag();
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 9);
    MG_CHECK(selected(0) == second && selected(1) == first);
    for (int i = 0; i < 7; ++i) MG_CHECK(selected(i + 2) == overlap[i]);
    MG_CHECK(!side().GetCurGroup()->FindObject(overlap[7]));
    MG_CHECK(!side().GetCurGroup()->FindObject(overlap[8]));
    shift_drag();
    // Previously hit members toggle off; the two excluded by the cap toggle on.
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 3);
    MG_CHECK(selected(0) == second && selected(1) == overlap[7] && selected(2) == overlap[8]);
    MG_CHECK(!side().GetCurGroup()->FindObject(first));
    for (int i = 0; i < 7; ++i) MG_CHECK(!side().GetCurGroup()->FindObject(overlap[i]));
}
void overlapping_selection_limit() {
    Session session;
    CFormMatrixGame form;
    auto *first = selected(0), *second = selected(1);
    auto *overlap = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 200);
    SelectionFixture visible{overlap, first, second};
    const auto p = screen_center(*first);
    side().Select(NOTHING, nullptr);
    side().ResetSystemSelection();
    drag(form, p, CPoint(p.x + 1, p.y + 1));
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 1 && selected(0) == overlap);
    // Area 9 is no longer the single-hit fast path (whose condition is < 9).
    drag(form, CPoint(p.x - 1, p.y - 1), CPoint(p.x + 2, p.y + 2));
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 2);
    MG_CHECK(selected(0) == overlap && selected(1) == first);
    std::vector<CMatrixRobotAI *> extras;
    for (int i = 0; i < 30; ++i) {
        auto *robot = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 200);
        visible.append(robot);
        extras.push_back(robot);
    }
    drag(form, CPoint(p.x - 10, p.y - 10), CPoint(p.x + 10, p.y + 10));
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 30);
    MG_CHECK(selected(0) == overlap && selected(1) == first);
    for (int i = 0; i < 28; ++i) MG_CHECK(selected(i + 2) == extras[i]);
    MG_CHECK(!side().GetCurGroup()->FindObject(extras[28]));
    MG_CHECK(!side().GetCurGroup()->FindObject(extras[29]));
}
void mixed_building_selection() {
    for (bool building_first : {false, true}) {
        Session session;
        CFormMatrixGame form;
        auto *robot = selected(0);
        auto building = tests::make_battle_building(*robot->m_Unit[0].m_Graph->VO(),
            PLAYER_SIDE, BUILDING_TITAN, 100, 200);
        SelectionFixture visible{};
        visible.append(building_first ? static_cast<CMatrixMapStatic *>(building.get()) : robot);
        visible.append(building_first ? static_cast<CMatrixMapStatic *>(robot) : building.get());
        const auto point = screen_center(*robot);
        const CRect area(point.x - 30, point.y - 30, point.x + 30, point.y + 30);
        MG_CHECK(robot->InRect(area) && building->InRect(area));
        side().Select(NOTHING, nullptr);
        side().ResetSystemSelection();
        drag(form, CPoint(area.left, area.top), CPoint(area.right, area.bottom));
        MG_CHECK(side().m_CurrSel == ROBOT_SELECTED);
        MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 1 && selected(0) == robot);
        MG_CHECK(!side().GetCurGroup()->FindObject(building.get()));
    }
}

void inactive_selection() {
    for (auto state : {ROBOT_DIP, ROBOT_IN_SPAWN, ROBOT_BASE_MOVEOUT, ROBOT_CARRYING}) {
        Session session;
        CFormMatrixGame form;
        auto *robot = selected(0);
        auto *candidate = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 200);
        const auto original = candidate->m_CurrState;
        struct RestoreState {
            CMatrixRobotAI &robot;
            ERobotState state;
            ~RestoreState() { robot.m_CurrState = state; }
        } restore{*candidate, original};
        candidate->m_CurrState = state;
        SelectionFixture visible{candidate, robot};
        const auto point = screen_center(*robot);
        side().Select(NOTHING, nullptr);
        side().ResetSystemSelection();
        drag(form, CPoint(point.x - 4, point.y - 4), CPoint(point.x + 4, point.y + 4));
        // Isolate eligibility without ticking an incomplete death/carrier state.
        const bool candidate_selected = side().GetCurGroup()->FindObject(candidate);
        candidate->m_CurrState = original;
        const bool expected = state != ROBOT_DIP;
        MG_CHECK(candidate_selected == expected);
        MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == (expected ? 2 : 1));
        MG_CHECK(selected(expected ? 1 : 0) == robot);
    }
    Session session;
    CFormMatrixGame form;
    auto *robot = selected(0);
    auto *crazy = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 200);
    crazy->MarkCrazy();
    SelectionFixture visible{crazy, robot};
    const auto point = screen_center(*robot);
    side().Select(NOTHING, nullptr);
    side().ResetSystemSelection();
    drag(form, CPoint(point.x - 4, point.y - 4), CPoint(point.x + 4, point.y + 4));
    MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 1 && selected(0) == robot);
}

void independent_group_commands() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session({mode});
        CFormMatrixGame form;
        auto *first = selected(0), *second = selected(1);
        side().CreateGroupFromCurrent(first);
        side().Select(ROBOT, nullptr);
        session.ground_command(500, 180);
        const auto first_group = session.robot(1).group;
        side().CreateGroupFromCurrent(second);
        side().Select(ROBOT, nullptr);
        session.ground_command(120, 500);
        MG_CHECK(session.robot(2).group != first_group);
        const auto first_destination = side().m_PlayerGroup[first_group].m_To;
        for (int i = 0; i < 100; ++i) session.advance();
        press(form, 'S');
        MG_CHECK(session.robot(2).order == mpo_Stop);
        MG_CHECK(session.robot(1).order == mpo_MoveTo);
        MG_CHECK(side().m_PlayerGroup[first_group].m_To == first_destination);
        for (int i = 0; i < 1900; ++i) session.advance();
        MG_CHECK(session.robot(1).x > 400 && session.robot(1).in_place);
        MG_CHECK(session.robot(2).y < 450);
    }
}
void minimap_move_routes() {
    for (bool explicit_order : {false, true}) {
        Session session;
        CFormMatrixGame form;
        auto minimap = tests::make_engine_object<CInterface>();
        minimap->m_strName = IF_MINI_MAP;
        g_IFaceList->m_InFocus = INTERFACE;
        g_IFaceList->m_FocusedInterface = minimap.get();
        g_MatrixMap->m_Minimap.SetOutParams(10, 20, 320, 320, D3DXVECTOR2(320, 320), 1, 0xffffffff);
        g_MatrixMap->m_Minimap.SetAngle(0);
        g_MatrixMap->m_Minimap.BeforeDraw();
        g_MatrixMap->m_Cursor.SetPos(260, 170); // The 320-pixel map represents 640 world units.
        g_MatrixMap->m_TraceStopObj = TRACE_STOP_WATER;
        g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(120, 500, 10); // The world trace must be superseded.
        if (explicit_order) {
            press(form, 'M');
            side().OnLButtonDown(CPoint(260, 170));
        } else side().OnRButtonDown(CPoint(260, 170));
        expect_order(session, mpo_MoveTo);
        MG_CHECK(session.robot(1).destination_x == 48 && session.robot(1).destination_y == 28);
        MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
        g_IFaceList->m_InFocus = UNKNOWN;
        g_IFaceList->m_FocusedInterface = nullptr;
        for (int i = 0; i < 1500; ++i) session.advance();
        MG_CHECK(session.robot(1).x > 400 && session.robot(2).x > 400);
    }
}
void edge_and_repeated_move() {
    for (const CPoint target : {CPoint(0, 0), CPoint(640, 640)}) {
        Session session;
        CFormMatrixGame form;
        for (int repeat = 0; repeat < 3; ++repeat) {
            session.ground_command(float(target.x), float(target.y));
            expect_order(session, mpo_MoveTo);
            MG_CHECK(session.robot(1).destination_x == target.x / 10 - 2);
            MG_CHECK(session.robot(1).destination_y == target.y / 10 - 2);
            for (int i = 0; i < 10; ++i) session.advance();
        }
        for (int i = 0; i < 2000; ++i) session.advance();
        for (auto id : {1u, 2u}) {
            MG_CHECK(session.robot(id).in_place);
            MG_CHECK(session.robot(id).x >= 20 && session.robot(id).x <= 620);
            MG_CHECK(session.robot(id).y >= 20 && session.robot(id).y <= 620);
            if (!target.x) MG_CHECK(session.robot(id).x < 100 && session.robot(id).y < 100);
            else MG_CHECK(session.robot(id).x > 500 && session.robot(id).y > 500);
        }
        side().Select(NOTHING, nullptr);
        side().ResetSystemSelection();
        const auto before = session.snapshot();
        session.ground_command(120, 300);
        press(form, 'M');
        MG_CHECK(session.snapshot() == before);
        MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
    }
}
void auto_target_retirement() {
    for (char key : {'U', 'D'}) {
        for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
            Session session({mode});
            CFormMatrixGame form;
            press(form, key);
            session.advance();
            MG_CHECK(session.robot(1).target == 3);
            for (int i = 0; i < 29; ++i) session.advance();
            MG_CHECK(session.robot(1).x != 100 || session.robot(1).y != 200);
            MG_CHECK(session.remove(3));
            for (int i = 0; i < 30; ++i) session.advance();
            MG_CHECK(session.robot(1).target == 4);
            MG_CHECK(session.remove(4));
            for (int i = 0; i < 30; ++i) session.advance();
            expect_order(session, key == 'U' ? mpo_AutoAttack : mpo_AutoDefence);
            MG_CHECK(session.robot(1).target_is_null);
            MG_CHECK(session.robot(2).target_is_null);
            // Keep below the first result poll: this case isolates tactical target replacement.
            MG_CHECK(session.time() == 900);
        }
    }
}
void mixed_auto_selection() {
    Session session;
    CFormMatrixGame form;
    auto *first = selected(0), *second = selected(1);
    side().CreateGroupFromCurrent(first);
    side().Select(ROBOT, nullptr);
    press(form, 'U');
    MG_CHECK(session.robot(1).order == mpo_AutoAttack);
    side().CreateGroupFromCurrent(second);
    side().Select(ROBOT, nullptr);
    press(form, 'D');
    MG_CHECK(session.robot(2).order == mpo_AutoDefence);
    side().GetCurSelGroup()->AddObject(first, -4);
    side().AddToCurrentGroup();
    side().Select(GROUP, nullptr);
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, AUTO_FROBOT_ON));
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, AUTO_PROTECT_ON));
    press(form, 'U'); // Active for one member: the first press stops the whole mixed selection.
    expect_order(session, mpo_Stop);
    MG_CHECK(!(g_IFaceList->m_IfListFlags & (AUTO_FROBOT_ON | AUTO_CAPTURE_ON | AUTO_PROTECT_ON)));
    press(form, 'U');
    expect_order(session, mpo_AutoAttack);
    for (int i = 0; i < 30; ++i) session.advance();
    MG_CHECK(session.robot(1).target == 3);
    MG_CHECK(session.remove(1));
    MG_CHECK(session.remove(2));
    session.advance();
    MG_CHECK(side().GetCurGroup() == nullptr);
    MG_CHECK(side().m_CurrSel == NOTHING_SELECTED);
    press(form, 'U');
    MG_CHECK(!session.robot(1).alive && !session.robot(2).alive);
}
void capture_target_validation() {
    for (int owner : {0, 1, 2}) for (bool explicit_order : {false, true}) {
        Session session;
        CFormMatrixGame form;
        auto building = tests::make_engine_object<CMatrixBuilding>();
        building->m_Kind = BUILDING_TITAN;
        building->m_State = BASE_CLOSED;
        building->m_Side = owner;
        building->m_Pos = D3DXVECTOR2(180, 400);
        building->InitMaxHitpoint(1000);
        building->AddLT();
        g_MatrixMap->m_TraceStopObj = building.get();
        g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(180, 400, 10);
        if (explicit_order) {
            press(form, 'K');
            side().OnLButtonDown(CPoint(0, 0));
        } else side().OnRButtonDown(CPoint(0, 0));
        if (owner != PLAYER_SIDE) {
            expect_order(session, mpo_Capture);
            MG_CHECK(side().m_PlayerGroup[session.robot(1).group].m_Obj == building.get());
            MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
            const int capturing = int(selected(0)->FindOrderLikeThat(ROT_CAPTURE_FACTORY)) +
                                  int(selected(1)->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
            MG_CHECK(capturing == 1);
            MG_CHECK(building->GetSide() == owner); // Acceptance does not instantly transfer ownership.
            press(form, 'S');
            for (int i = 0; i < 2; ++i) MG_CHECK(!selected(i)->FindOrderLikeThat(ROT_CAPTURE_FACTORY));
        } else if (explicit_order) {
            MG_CHECK((g_IFaceList->m_IfListFlags & (PREORDER_CAPTURE | ORDERING_MODE)) ==
                     (PREORDER_CAPTURE | ORDERING_MODE));
            MG_CHECK(session.robot(1).group == -1 && session.robot(2).group == -1);
            press(form, 'X');
        } else {
            expect_order(session, mpo_MoveTo);
            MG_CHECK(session.robot(1).target_is_null);
            MG_CHECK(session.robot(1).destination_x == 16 && session.robot(1).destination_y == 38);
            press(form, 'S');
        }
        g_MatrixMap->m_TraceStopObj = TRACE_STOP_NONE;
        // This mesh-free target is used only for synchronous validation/order transfer.
    }
}
void cannon_and_invalid_targets() {
    Session session;
    CFormMatrixGame form;
    auto cannon = tests::make_engine_object<CMatrixCannon>();
    cannon->SetSide(2);
    cannon->m_Num = 1;
    cannon->AddLT();
    cannon->m_Pos = D3DXVECTOR2(180, 400);
    g_MatrixMap->m_TraceStopObj = cannon.get();
    g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(180, 400, 10);
    side().OnRButtonDown(CPoint(0, 0));
    expect_order(session, mpo_Attack);
    MG_CHECK(side().m_PlayerGroup[session.robot(1).group].m_Obj == cannon.get());
    press(form, 'S');
    cannon->SetSide(PLAYER_SIDE);
    side().OnRButtonDown(CPoint(0, 0));
    expect_order(session, mpo_MoveTo);
    MG_CHECK(session.robot(1).destination_x == 16 && session.robot(1).destination_y == 38);
    press(form, 'A');
    side().OnLButtonDown(CPoint(0, 0));
    expect_order(session, mpo_Attack);
    MG_CHECK(side().m_PlayerGroup[session.robot(1).group].m_Obj == cannon.get());
    press(form, 'S');
    cannon->m_CurrState = CANNON_DIP;
    press(form, 'A');
    side().OnLButtonDown(CPoint(0, 0));
    expect_order(session, mpo_Attack);
    MG_CHECK(session.robot(1).target_is_null); // A non-live object is treated as ground.
    MG_CHECK(session.robot(1).destination_x == 16 && session.robot(1).destination_y == 38);
    press(form, 'K');
    side().OnLButtonDown(CPoint(0, 0));
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_CAPTURE));
    press(form, 'X');
    press(form, 'S');
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_NONE;
    const auto before = session.snapshot();
    side().OnRButtonDown(CPoint(0, 0));
    MG_CHECK(session.snapshot() == before);
}
void manual_aim_and_fire_input() {
    Session session;
    CFormMatrixGame form;
    auto *robot = selected(0);
    press(form, VK_RETURN);
    g_IFaceList->m_InFocus = UNKNOWN;
    g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
    g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(180, 240, 35);
    MG_CHECK(g_Config.m_KeyActions[KA_FIRE] == VK_LBUTTON);
    form.MouseKey(B_DOWN, VK_LBUTTON, 640, 360);
    session.advance();
    MG_CHECK(robot->FindOrderLikeThat(ROT_FIRE));
    float x = 0, y = 0, z = 0;
    int fire_type = -1;
    robot->GetOrder(0)->GetParams(&x, &y, &z, &fire_type);
    MG_CHECK(x == 180 && y == 240 && z == 35 && fire_type == 0);
    MG_CHECK(CMultiSelection::m_GameSelection == nullptr);
    g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(140, 270, 20);
    session.advance();
    MG_CHECK(robot->FindOrderLikeThat(ROT_FIRE));
    form.MouseKey(B_UP, VK_LBUTTON, 640, 360);
    session.advance();
    MG_CHECK(!robot->FindOrderLikeThat(ROT_FIRE));
    g_IFaceList->m_InFocus = INTERFACE;
    form.MouseKey(B_DOWN, VK_LBUTTON, 640, 360);
    // Isolate the unit's focus guard: a full map tick recomputes hover from UI artwork.
    robot->LogicTakt(10);
    MG_CHECK(!robot->FindOrderLikeThat(ROT_FIRE));
    g_IFaceList->m_InFocus = UNKNOWN;
    session.advance();
    MG_CHECK(robot->FindOrderLikeThat(ROT_FIRE)); // Still held when the pointer returns to the world.
    form.SystemEvent(SYSEV_INPUT_RESET);
    session.advance();
    MG_CHECK(!robot->FindOrderLikeThat(ROT_FIRE));
    MG_CHECK(!Input::isVKeyPressed(VK_LBUTTON));
    const auto direction = robot->m_Forward;
    form.MouseKey(B_DOWN, VK_MBUTTON, 640, 360);
    form.MouseMove(660, 360);
    session.advance();
    MG_CHECK(robot->m_Forward != direction);
    MG_CHECK(robot->m_PosX == 100 && robot->m_PosY == 200);
    form.MouseKey(B_UP, VK_MBUTTON, 660, 360);
    MG_CHECK(!g_MatrixMap->IsMouseCam());
    press(form, VK_RETURN);
    MG_CHECK(!side().IsArcadeMode());
}
void manual_armed_fire() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        tests::BattleCombatAssets assets;
        Session session({mode, 80, 10, 1000, false, true});
        std::cout << session.metadata("player-manual-laser-v1") << '\n';
        assets.install();
        CFormMatrixGame form;
        auto *robot = selected(0);
        MG_CHECK(session.remove(2) && session.remove(4));
        CMatrixRobotAI *target = nullptr;
        for (auto *o = CMatrixMapStatic::GetFirstLogic(); o; o = o->GetNextLogic())
            if (o->IsLiveRobot() && o->GetSide() == 2) target = o->AsRobot();
        MG_CHECK(target && session.matches(3, target));
        target->SetMaxSpeed(0);
        target->InitMaxHitpoint(1000);
        g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 600;
        g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {2, 0, 10};
        g_Config.m_Overheat[WEAPON_LASER_HEAT] = 10;
        g_Config.m_Overheat[WEAPON_LASER_COOL] = 10;
        g_Config.m_Overheat[WCP_LASER] = 100;
        g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
        tests::arm_battle_robot(*robot, RUK_WEAPON_LASER);
        press(form, VK_RETURN);
        MG_CHECK(side().GetArcadedObject() == robot);
        g_IFaceList->m_InFocus = UNKNOWN;
        g_MatrixMap->m_TraceStopObj = target;
        g_MatrixMap->m_TraceStopPos = target->GetGeoCenter();
        const auto before = target->GetHitPoint();
        form.MouseKey(B_DOWN, VK_LBUTTON, 640, 360);
        for (int i = 0; i < 100; ++i) session.advance();
        MG_CHECK(robot->FindOrderLikeThat(ROT_FIRE));
        const auto after = target->GetHitPoint();
        std::cout << "Manual fire mode=" << int(mode) << " health=" << before << "->" << after << '\n';
        MG_CHECK(after < before);
        form.MouseKey(B_UP, VK_LBUTTON, 640, 360);
        for (int i = 0; i < 20; ++i) session.advance();
        MG_CHECK(!robot->FindOrderLikeThat(ROT_FIRE));
        MG_CHECK(target->GetHitPoint() == after);
        form.MouseKey(B_DOWN, VK_LBUTTON, 640, 360);
        for (int i = 0; i < 20; ++i) session.advance();
        MG_CHECK(target->GetHitPoint() < after);
        form.SystemEvent(SYSEV_INPUT_RESET);
        const auto at_reset = target->GetHitPoint();
        for (int i = 0; i < 20; ++i) session.advance();
        MG_CHECK(!robot->FindOrderLikeThat(ROT_FIRE) && target->GetHitPoint() == at_reset);
        press(form, VK_RETURN);
        MG_CHECK(!side().IsArcadeMode());
    }
}

std::vector<std::string> repair_command_run(random::Mode mode, bool button_route, bool retire) {
    tests::BattleCombatAssets assets;
    Session session({mode, 80, 10, 1000, false, true});
    std::cout << session.metadata("player-repair-routes-v1") << "\nbutton_route=" << button_route
              << " retire=" << retire << '\n';
    assets.install();
    CFormMatrixGame form;
    auto *healer = selected(0);
    auto *patient = selected(1);
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_REPAIR)] = 600;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_REPAIR)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 0};
    g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {20, 20, 0};
    g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
    tests::arm_battle_robot(*healer, RUK_WEAPON_REPAIR);
    patient->InitMaxHitpoint(50);
    patient->SetMaxSpeed(0);
    MG_CHECK(!patient->Damage(WEAPON_GUN, patient->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
    MG_CHECK(patient->GetHitPoint() == 3.0f && side().GetCurGroup()->GetRepairsCnt() == 1);
    if (button_route) {
        auto button = tests::make_engine_object<CIFaceElement>();
        button->m_strName = IF_ORDER_REPAIR;
        g_IFaceList->PlayerAction(button.get());
    } else press(form, 'R');
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_REPAIR));
    const auto before = session.snapshot();
    ground(500, 300);
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_REPAIR));
    MG_CHECK(session.snapshot() == before);
    CMatrixRobotAI *enemy = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && object->GetSide() == 2) enemy = object->AsRobot();
    MG_CHECK(enemy);
    g_MatrixMap->m_TraceStopObj = enemy;
    g_MatrixMap->m_TraceStopPos = enemy->GetGeoCenter();
    side().OnLButtonDown(CPoint(0, 0));
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_REPAIR) && session.snapshot() == before);
    g_MatrixMap->m_TraceStopObj = patient;
    g_MatrixMap->m_TraceStopPos = patient->GetGeoCenter();
    side().OnLButtonDown(CPoint(0, 0));
    MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, PREORDER_REPAIR | ORDERING_MODE));
    MG_CHECK(session.robot(1).order == mpo_Repair && session.robot(1).target == 2);
    std::vector<std::string> states;
    int heals = 0;
    float health = patient->GetHitPoint();
    for (int frame = 0; frame < 200; ++frame) {
        if (retire && frame == 30) {
            MG_CHECK(session.remove(2));
            patient = nullptr;
            g_MatrixMap->m_TraceStopObj = TRACE_STOP_NONE;
        }
        session.advance();
        if (patient && patient->GetHitPoint() != health) {
            health = patient->GetHitPoint();
            ++heals;
            MG_CHECK(session.time() == 10 + 100 * heals);
            MG_CHECK(health == float(30 + 4 * heals) / 10.0f);
        }
        if (retire && session.time() >= 400)
            MG_CHECK(session.robot(1).target_is_null && !healer->FindOrderLikeThat(ROT_FIRE));
        states.push_back(session.snapshot());
    }
    MG_CHECK(heals == (retire ? 2 : 5));
    MG_CHECK(session.robot(1).order == mpo_Stop);
    if (patient) MG_CHECK(!patient->NeedRepair() && patient->GetHitPoint() == 5.0f);
    return states;
}
void repair_command_routes() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (bool retire : {false, true}) {
            const auto keyboard = repair_command_run(mode, false, retire);
            MG_CHECK(keyboard == repair_command_run(mode, true, retire));
        }
}

struct PlayerEffectAccess : CMatrixEffect { using CMatrixEffect::m_SpotProperties; };
struct PlayerCraterAssets {
    SSpotProperties previous = PlayerEffectAccess::m_SpotProperties[SPOT_VORONKA];
    void install() {
        PlayerEffectAccess::m_SpotProperties[SPOT_VORONKA] = PlayerEffectAccess::m_SpotProperties[SPOT_PLASMA_HIT];
        PlayerEffectAccess::m_SpotProperties[SPOT_VORONKA].func = SpotTaktVoronka;
    }
    ~PlayerCraterAssets() { PlayerEffectAccess::m_SpotProperties[SPOT_VORONKA] = previous; }
};
std::vector<std::string> bomb_command_run(random::Mode mode, bool button_route, bool targeted) {
    tests::BattleCombatAssets assets;
    PlayerCraterAssets crater;
    Session session({mode, 80, 10, 4000, false, true});
    std::cout << session.metadata("player-bomb-routes-v1") << "\nbutton_route=" << button_route
              << " targeted=" << targeted << '\n';
    assets.install();
    crater.install();
    CFormMatrixGame form;
    auto *bomber = selected(0);
    CMatrixRobotAI *target = nullptr;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsLiveRobot() && session.matches(3, object->AsRobot())) target = object->AsRobot();
    MG_CHECK(target);
    target->InitMaxHitpoint(50);
    target->SetMaxSpeed(0);
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 150;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {10, 4, 0};
    tests::arm_battle_robot(*bomber, RUK_WEAPON_BOMB);
    MG_CHECK(side().GetCurGroup()->GetBombersCnt() == 1);
    std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(bomber->GetCore(DEBUG_CALL_INFO),
        [](SObjectCore *value) { value->Release(); });
    const auto initial = session.snapshot();
    press(form, 'E');
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_BOMB));
    press(form, 'X');
    ground(500, 500);
    MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, PREORDER_BOMB | ORDERING_MODE));
    MG_CHECK(session.snapshot() == initial && !bomber->IsMustDie());
    if (button_route) {
        auto button = tests::make_engine_object<CIFaceElement>();
        button->m_strName = IF_ORDER_BOMB;
        g_IFaceList->PlayerAction(button.get());
    } else press(form, 'E');
    MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, PREORDER_BOMB));
    if (targeted) {
        g_MatrixMap->m_TraceStopObj = target;
        g_MatrixMap->m_TraceStopPos = target->GetGeoCenter();
        side().OnLButtonDown(CPoint(0, 0));
    } else ground(500, 500);
    MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, PREORDER_BOMB | ORDERING_MODE));
    MG_CHECK(session.robot(1).order == mpo_Bomb);
    MG_CHECK(session.robot(1).target == (targeted ? 3u : 0u));
    std::vector<std::string> states;
    int dying_at = 0, removed_at = 0, damaged_at = 0;
    int damage_events = 0;
    float health = target->GetHitPoint();
    for (int frame = 0; frame < 3000; ++frame) {
        session.advance();
        if (core->m_Object && bomber->m_CurrState == ROBOT_DIP && !dying_at) dying_at = session.time();
        if (!core->m_Object && !removed_at) removed_at = session.time();
        if (target->GetHitPoint() != health) {
            ++damage_events;
            MG_CHECK(health == 5.0f && target->GetHitPoint() == 4.0f);
            health = target->GetHitPoint();
            damaged_at = session.time();
        }
        states.push_back(session.snapshot());
    }
    std::cout << "Bomb route targeted=" << targeted << " mode=" << int(mode)
              << " dying=" << dying_at << " removed=" << removed_at << " damage=" << damaged_at << '\n';
    MG_CHECK(dying_at == (targeted ? 7920 : 10920));
    MG_CHECK(damage_events == 1);
    MG_CHECK(damaged_at == dying_at + 290);
    MG_CHECK(removed_at == dying_at + (mode == random::Mode::ParkMiller ? 4680 : 4810));
    MG_CHECK(!core->m_Object && !session.robot(1).alive);
    MG_CHECK(target->GetHitPoint() == 4.0f);
    MG_CHECK(side().GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
    return states;
}
void bomb_command_routes() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (bool targeted : {false, true}) {
            const auto keyboard = bomb_command_run(mode, false, targeted);
            MG_CHECK(keyboard == bomb_command_run(mode, true, targeted));
        }
}

void manual_bomb_key() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        for (bool armed : {false, true}) {
            tests::BattleCombatAssets assets;
            PlayerCraterAssets crater;
            Session session({mode, 80, 10, 1000, false, true});
            std::cout << session.metadata("player-manual-bomb-v1") << "\narmed=" << armed << '\n';
            assets.install();
            crater.install();
            CFormMatrixGame form;
            auto *robot = selected(0);
            auto *neighbour = selected(1);
            neighbour->InitMaxHitpoint(1000);
            neighbour->SetMaxSpeed(0);
            g_Config.m_WeaponRadius[Weap2Index(WEAPON_BIGBOOM)] = 150;
            g_Config.m_WeaponCooldown[Weap2Index(WEAPON_BIGBOOM)] = 100;
            g_Config.m_RobotDamages[Weap2Index(WEAPON_BIGBOOM)] = {10, 4, 0};
            if (armed) tests::arm_battle_robot(*robot, RUK_WEAPON_BOMB);
            std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(robot->GetCore(DEBUG_CALL_INFO),
                [](SObjectCore *value) { value->Release(); });
            press(form, VK_RETURN);
            MG_CHECK(side().GetArcadedObject() == robot);
            const auto health = neighbour->GetHitPoint();
            press(form, static_cast<uint8_t>(g_Config.m_KeyActions[KA_UNIT_BOOM]));
            MG_CHECK(robot->IsMustDie() == armed);
            MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, PREORDER_BOMB | ORDERING_MODE));
            int damage_events = 0;
            float previous_health = health;
            for (int i = 0; i < 600; ++i) {
                session.advance();
                if (neighbour->GetHitPoint() != previous_health) {
                    ++damage_events;
                    previous_health = neighbour->GetHitPoint();
                }
            }
            MG_CHECK(damage_events == (armed ? 1 : 0));
            MG_CHECK(neighbour->GetHitPoint() == (armed ? health - 0.4f : health));
            MG_CHECK(bool(core->m_Object) == !armed);
            MG_CHECK(side().IsArcadeMode() == !armed);
            MG_CHECK(side().GetArcadedObject() == (armed ? nullptr : robot));
            MG_CHECK(!neighbour->IsMustDie() && !neighbour->FindOrderLikeThat(ROT_FIRE));
            MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 0);
            if (!armed) press(form, VK_RETURN);
        }
    }
}

void manual_combat_death() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        tests::BattleCombatAssets assets;
        PlayerCraterAssets crater;
        Session session({mode, 80, 10, 1000, false, true});
        std::cout << session.metadata("player-manual-combat-death-v1") << '\n';
        assets.install();
        crater.install();
        CFormMatrixGame form;
        auto *robot = selected(0);
        auto *survivor = selected(1);
        robot->InitMaxHitpoint(100);
        const float speed = robot->GetMaxSpeed();
        std::unique_ptr<SObjectCore, void (*)(SObjectCore *)> core(robot->GetCore(DEBUG_CALL_INFO),
            [](SObjectCore *value) { value->Release(); });
        press(form, VK_RETURN);
        const auto forward = static_cast<uint8_t>(g_Config.m_KeyActions[KA_UNIT_FORWARD]);
        form.Keyboard(true, forward);
        g_IFaceList->m_InFocus = UNKNOWN;
        g_MatrixMap->m_TraceStopObj = TRACE_STOP_LANDSCAPE;
        g_MatrixMap->m_TraceStopPos = D3DXVECTOR3(180, 240, 35);
        form.MouseKey(B_DOWN, VK_LBUTTON, 640, 360);
        session.advance();
        MG_CHECK(robot->FindOrderLikeThat(ROT_FIRE) && robot->FindOrderLikeThat(ROT_MOVE_TO));
        g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {20, 20, 0};
        MG_CHECK(!robot->Damage(WEAPON_GUN, robot->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
        MG_CHECK(robot->GetHitPoint() == 8.0f);
        MG_CHECK(side().GetArcadedObject() == robot && side().IsArcadeMode());
        g_Config.m_RobotDamages[Weap2Index(WEAPON_GUN)] = {1000, 1000, 0};
        MG_CHECK(robot->Damage(WEAPON_GUN, robot->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
        MG_CHECK(robot->m_CurrState == ROBOT_DIP && core->m_Object == robot);
        MG_CHECK(side().GetArcadedObject() == nullptr && !side().IsArcadeMode());
        MG_CHECK(robot->GetMaxSpeed() == speed);
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
        MG_CHECK(Input::isVKeyPressed(forward) && Input::isVKeyPressed(VK_LBUTTON));
        // Input release arrives after loss of control but before debris retirement.
        form.Keyboard(false, forward);
        form.MouseKey(B_UP, VK_LBUTTON, 640, 360);
        MG_CHECK(!Input::isVKeyPressed(forward) && !Input::isVKeyPressed(VK_LBUTTON));
        MG_CHECK(CMultiSelection::m_GameSelection == nullptr);
        const float x = survivor->m_PosX, y = survivor->m_PosY;
        for (int i = 0; i < 600; ++i) session.advance();
        MG_CHECK(!core->m_Object && !session.robot(1).alive);
        MG_CHECK(survivor->m_PosX == x && survivor->m_PosY == y);
        MG_CHECK(!survivor->FindOrderLikeThat(ROT_FIRE) && !survivor->FindOrderLikeThat(ROT_MOVE_TO));
        MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
        side().CreateGroupFromCurrent(survivor);
        side().Select(ROBOT, nullptr);
        press(form, VK_RETURN);
        MG_CHECK(side().GetArcadedObject() == survivor);
        press(form, VK_RETURN);
        MG_CHECK(!side().IsArcadeMode());
    }
}

void manual_exit_keys() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        for (uint8_t key : {uint8_t(VK_RETURN), uint8_t(VK_SPACE), uint8_t(VK_ESCAPE)}) {
            Session session({mode});
            CFormMatrixGame form;
            auto *robot = selected(0);
            const float speed = robot->GetMaxSpeed();
            press(form, VK_SPACE);
            MG_CHECK(side().GetArcadedObject() == robot && robot->GetMaxSpeed() == speed * 1.1f);
            press(form, key);
            MG_CHECK(!side().IsArcadeMode() && robot->GetMaxSpeed() == speed);
            MG_CHECK(!FLAG(g_MatrixMap->m_Flags, MMFLAG_DIALOG_MODE));
            MG_CHECK(!g_MatrixMap->IsPaused());
            MG_CHECK(session.robot(1).order == mpo_Attack && session.robot(1).target_is_null);
            session.ground_command(120, 500);
            for (int i = 0; i < 1500; ++i) session.advance();
            MG_CHECK(session.robot(1).y > 450 && session.robot(1).in_place);
        }
    }
}

void registered_cheat_effects() {
    Profile profile;
    profile.initially_selected = false;
    Session session(profile);
    CFormMatrixGame form;
    auto type = [&](std::string_view value) { for (char key : value) press(form, key); };
    const DWORD flags = g_Flags;
    type("NEED4SPEED");
#if MATRIXGAME_SCENARIO_CHEATS
    MG_CHECK(g_Flags == (flags ^ GFLAG_4SPEED));
#else
    MG_CHECK(g_Flags == flags);
#endif
    type("NEED4SPEED");
    MG_CHECK(g_Flags == flags);
    const DWORD map_flags = g_MatrixMap->m_Flags;
    type("AUTO");
#if MATRIXGAME_SCENARIO_CHEATS
    MG_CHECK(g_MatrixMap->m_Flags == (map_flags ^ MMFLAG_AUTOMATIC_MODE));
#else
    MG_CHECK(g_MatrixMap->m_Flags == map_flags);
#endif
    type("AUTO");
    MG_CHECK(g_MatrixMap->m_Flags == map_flags);
    const DWORD diagnostic = g_Config.m_DIFlags;
    type("BABKI");
#if MATRIXGAME_SCENARIO_CHEATS
    MG_CHECK(g_Config.m_DIFlags == (diagnostic ^ DI_SIDEINFO));
#else
    MG_CHECK(g_Config.m_DIFlags == diagnostic);
#endif
    type("BABKI");
    MG_CHECK(g_Config.m_DIFlags == diagnostic);
    for (int resource = 0; resource < MAX_RESOURCES; ++resource)
        side().SetResourceAmount(static_cast<ERes>(resource), resource * 100);
    type("RICHIERICH");
    type("RICHIERICH");
    for (int resource = 0; resource < MAX_RESOURCES; ++resource)
        MG_CHECK(side().GetResourcesAmount(static_cast<ERes>(resource)) ==
                 (MATRIXGAME_SCENARIO_CHEATS ? 9000 : resource * 100));
    g_MatrixMap->SetMaintenanceTime(1234);
    type("HURRY");
    MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == (MATRIXGAME_SCENARIO_CHEATS ? 1 : 1234));
    g_MatrixMap->SetMaintenanceTime(0);
    type("HURRY");
    MG_CHECK(g_MatrixMap->BeforeMaintenanceTime() == 0);
}
void megabust_cheat() {
    Session session;
    CFormMatrixGame form;
    auto *first = selected(0), *second = selected(1);
    auto *enemy = CMatrixMapStatic::GetFirstLogic();
    while (enemy && enemy->GetSide() == PLAYER_SIDE) enemy = enemy->GetNextLogic();
    MG_CHECK(enemy && enemy->IsLiveRobot());
    const float first_hp = first->GetMaxHitPoint(), second_hp = second->GetMaxHitPoint();
    const float enemy_hp = enemy->AsRobot()->GetMaxHitPoint();
    MG_CHECK(first_hp > 0 && second_hp > 0);
    side().Select(NOTHING, nullptr);
    side().ResetSystemSelection();
    auto type = [&] { for (char key : std::string("MEGABUST")) press(form, key); };
    type();
    // GetMaxHitPoint reports tenths of the units consumed by InitMaxHitpoint: visible HP doubles.
    MG_CHECK(first->GetMaxHitPoint() == first_hp * (MATRIXGAME_SCENARIO_CHEATS ? 2 : 1));
    MG_CHECK(second->GetMaxHitPoint() == second_hp * (MATRIXGAME_SCENARIO_CHEATS ? 2 : 1));
    MG_CHECK(enemy->AsRobot()->GetMaxHitPoint() == enemy_hp);
    struct RestoreState {
        CMatrixRobotAI &robot;
        ERobotState state;
        ~RestoreState() { robot.m_CurrState = state; }
    } restore{*second, second->m_CurrState};
    second->m_CurrState = ROBOT_IN_SPAWN;
    type();
    MG_CHECK(first->IsMustDie() == bool(MATRIXGAME_SCENARIO_CHEATS));
    MG_CHECK(!second->IsMustDie() && !enemy->AsRobot()->IsMustDie());
    MG_CHECK(first->GetMaxHitPoint() == first_hp * (MATRIXGAME_SCENARIO_CHEATS ? 2 : 1));
    // Pending destruction is the cheat's immediate contract; no death-effect tick is simulated here.
}
void cheat_dispatch_gate() {
    Profile profile;
    profile.initially_selected = false;
    Session session(profile);
    CFormMatrixGame form;
    const DWORD flags = g_Flags;
    for (char key : std::string("KEEPALIVE")) press(form, key);
#if MATRIXGAME_SCENARIO_CHEATS
    MG_CHECK(g_Flags == (flags ^ GFLAG_KEEPALIVE));
#else
    MG_CHECK(g_Flags == flags);
#endif
    for (char key : std::string("KEEPALIVE")) press(form, key);
    MG_CHECK(g_Flags == flags);
    const int resources = side().GetResourcesAmount(TITAN);
    for (char key : std::string("RICHIERICH")) press(form, key);
#if MATRIXGAME_SCENARIO_CHEATS
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
    {"game.player.manual_bomb_key", manual_bomb_key},
    {"game.player.manual_combat_death", manual_combat_death},
    {"game.player.manual_exit_keys", manual_exit_keys},
    {"game.player.bomb_command_routes", bomb_command_routes},
    {"game.player.repair_command_routes", repair_command_routes},
    {"game.player.megabust_cheat", megabust_cheat},
    {"game.player.registered_cheat_effects", registered_cheat_effects},
    {"game.player.manual_aim_and_fire_input", manual_aim_and_fire_input},
    {"game.player.manual_armed_fire", manual_armed_fire},
    {"game.player.cannon_and_invalid_targets", cannon_and_invalid_targets},
    {"game.player.capture_target_validation", capture_target_validation},
    {"game.player.mixed_auto_selection", mixed_auto_selection},
    {"game.player.auto_target_retirement", auto_target_retirement},
    {"game.player.edge_and_repeated_move", edge_and_repeated_move},
    {"game.player.minimap_move_routes", minimap_move_routes},
    {"game.player.move_routes", move_routes},
    {"game.player.preorder_cancellation", preorder_cancellation},
    {"game.player.capability_rejection", capability_rejection},
    {"game.player.stop_key", stop_key},
    {"game.player.stop_button", stop_button},
    {"game.player.attack_routes", attack_routes},
    {"game.player.control_groups", control_groups},
    {"game.player.empty_control_group", empty_control_group},
    {"game.player.control_group_camera", control_group_camera},
    {"game.player.debug_reinforcement_reset", debug_reinforcement_reset},
    {"game.player.group_add_cap", group_add_cap},
    {"game.player.shift_rectangle_group_cap", shift_rectangle_group_cap},
    {"game.player.patrol_reversal", patrol_reversal},
    {"game.player.patrol_stop_lifecycle", patrol_stop_lifecycle},
    {"game.player.patrol_replacement_lifecycle", patrol_replacement_lifecycle},
    {"game.player.auto_modes", auto_modes},
    {"game.player.manual_control", manual_control},
    {"game.player.controlled_unit_removal", controlled_unit_removal},
    {"game.player.manual_exit_held", manual_exit_held},
    {"game.player.manual_focus", manual_focus},
    {"game.player.cheat_dispatch_gate", cheat_dispatch_gate},
    {"game.player.mesh_selection", mesh_selection},
    {"game.player.overlapping_selection_limit", overlapping_selection_limit},
    {"game.player.mixed_building_selection", mixed_building_selection},
    {"game.player.inactive_selection", inactive_selection},
    {"game.player.independent_group_commands", independent_group_commands},
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
#ifdef _MSC_VER
        // The CRT abort message can open a modal Debug dialog before SIGABRT.
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        std::signal(SIGABRT, [](int) { std::_Exit(86); });
        std::cerr << "Entered intentional CRASH parser probe" << std::endl;
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
