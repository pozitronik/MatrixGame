// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"

#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "Interface/CInterface.h"
#include "input.hpp"

namespace {
using tests::EnginePtr;

struct InterfaceGlobalGuard {
    CIFaceList *previous = g_IFaceList;
    ~InterfaceGlobalGuard() { g_IFaceList = previous; }
};

EnginePtr<CIFaceList> make_interface() {
    auto ui = tests::make_engine_object<CIFaceList>();
    g_IFaceList = ui.get();
    return ui;
}

struct SelectionGuard {
    CMatrixSideUnit &side;
    ~SelectionGuard() {
        Input::onFocusLost();
        if (side.GetCurSelGroup()) side.GetCurSelGroup()->RemoveAll();
        side.SetCurGroup(nullptr);
    }
};

enum class SetupFailure { None, BeforeSide, AfterSelection };

// This probe starts with a selected, mesh-free robot. It does not test picking,
// movement or autonomous execution; command dispatch below is all production code.
struct PlayerCommandWorld {
    tests::CommandWorld world;
    InterfaceGlobalGuard interfaceGuard; // Restore after ui is destroyed, including constructor failure.
    EnginePtr<CIFaceList> ui = make_interface();
    EnginePtr<CMatrixRobotAI> robot{HNew(g_MatrixHeap) CMatrixRobotAI};
    SelectionGuard selectionGuard{*world.map->GetPlayerSide()};
    CFormMatrixGame form;

    explicit PlayerCommandWorld(SetupFailure failure = SetupFailure::None) {
        if (failure == SetupFailure::BeforeSide) throw std::runtime_error("fixture before side setup");
        g_Config.SetDefaults();
        Input::onFocusLost();
        side().InitPlayerSide();
        robot->m_CurrState = ROBOT_SUCCESSFULLY_BUILD;
        robot->m_Side = PLAYER_SIDE;
        robot->SetGroupLogic(-1);
        robot->AddLT();
        side().GetCurSelGroup()->AddObject(robot.get(), -4);
        side().SetCurGroup(side().GetCurSelGroup());
        side().m_CurrSel = ROBOT_SELECTED;
        if (failure == SetupFailure::AfterSelection) throw std::runtime_error("fixture after selection setup");
    }

    CMatrixSideUnit &side() { return *world.map->GetPlayerSide(); }

    void press(uint8_t key) {
        form.Keyboard(true, key);
        form.Keyboard(false, key);
        MG_CHECK(!Input::isVKeyPressed(key));
    }

    void expect(EMatrixPlayerOrder order, DWORD flag) {
        const int group = robot->GetGroupLogic();
        MG_CHECK(group >= 0 && group < MAX_LOGIC_GROUP);
        const auto &state = side().m_PlayerGroup[group];
        MG_CHECK(state.Order() == order);
        MG_CHECK(state.m_RobotCnt == 1);
        MG_CHECK(state.m_Obj == nullptr);
        MG_CHECK(state.m_To.x == -1 && state.m_To.y == -1);
        MG_CHECK((ui->m_IfListFlags & (AUTO_FROBOT_ON | AUTO_CAPTURE_ON | AUTO_PROTECT_ON)) == flag);
        MG_CHECK(side().GetCurGroup()->GetObjectsCnt() == 1);
        MG_CHECK(side().GetCurGroup()->GetObjectByN(0) == robot.get());
        MG_CHECK(robot->GetOrdersInPool() == 0); // Acceptance has not run a battle tick.
        MG_CHECK(g_D3DD == nullptr);
        for (auto *cached : g_Cache->_data) MG_CHECK(!cached->IsLoaded());
    }
};

void command_replacement() {
    PlayerCommandWorld fixture;
    fixture.press('U');
    fixture.expect(mpo_AutoAttack, AUTO_FROBOT_ON);
    MG_CHECK(fixture.robot->GetGroupLogic() == 0);
    fixture.press('C');
    fixture.expect(mpo_AutoCapture, AUTO_CAPTURE_ON);
    MG_CHECK(fixture.robot->GetGroupLogic() == 1);
    const auto &oldSlot = fixture.side().m_PlayerGroup[0];
    MG_CHECK(oldSlot.Order() == mpo_AutoAttack);
    MG_CHECK(oldSlot.m_RobotCnt == 1); // Recount predates moving the robot to slot 1.
    fixture.press('U');
    fixture.expect(mpo_AutoAttack, AUTO_FROBOT_ON);
    MG_CHECK(fixture.robot->GetGroupLogic() == 0);
}

void active_auto_order_stops() {
    PlayerCommandWorld fixture;
    fixture.press('U');
    fixture.expect(mpo_AutoAttack, AUTO_FROBOT_ON);
    fixture.press('U');
    const int group = fixture.robot->GetGroupLogic();
    MG_CHECK(group == 1);
    MG_CHECK(fixture.side().m_PlayerGroup[group].Order() == mpo_Stop);
    MG_CHECK(fixture.side().m_PlayerGroup[group].m_RobotCnt == 1);
    MG_CHECK(fixture.side().m_PlayerGroup[0].m_RobotCnt == 0); // Stop runs TaktPL's recount.
    MG_CHECK((fixture.ui->m_IfListFlags & (AUTO_FROBOT_ON | AUTO_CAPTURE_ON | AUTO_PROTECT_ON)) == 0);
    MG_CHECK(fixture.robot->GetOrdersInPool() == 0);
    MG_CHECK(g_D3DD == nullptr);
}

void partial_setup_restores_globals() {
    for (auto failure : {SetupFailure::BeforeSide, SetupFailure::AfterSelection}) {
        bool caught = false;
        try { PlayerCommandWorld fixture(failure); }
        catch (const std::runtime_error &) { caught = true; }
        MG_CHECK(caught);
        MG_CHECK(g_IFaceList == nullptr);
        MG_CHECK(g_MatrixMap == nullptr);
        MG_CHECK(g_MatrixHeap == nullptr);
        MG_CHECK(g_MatrixData == nullptr);
        MG_CHECK(CMatrixMapStatic::GetFirstLogic() == nullptr);
        command_replacement();
    }
}

void preorder_suppresses_auto_command() {
    PlayerCommandWorld fixture;
    fixture.press('M');
    MG_CHECK(FLAG(fixture.ui->m_IfListFlags, PREORDER_MOVE));
    MG_CHECK(FLAG(fixture.ui->m_IfListFlags, ORDERING_MODE));
    fixture.press('U');
    MG_CHECK(fixture.robot->GetGroupLogic() == -1);
    MG_CHECK(!FLAG(fixture.ui->m_IfListFlags, AUTO_FROBOT_ON));
    MG_CHECK(FLAG(fixture.ui->m_IfListFlags, PREORDER_MOVE));
}

void repeated_command_sessions() {
    for (int i = 0; i < 3; ++i) {
        command_replacement();
        MG_CHECK(g_MatrixMap == nullptr);
        MG_CHECK(g_MatrixHeap == nullptr);
        MG_CHECK(g_MatrixData == nullptr);
        MG_CHECK(g_IFaceList == nullptr);
        MG_CHECK(CMatrixMapStatic::GetFirstLogic() == nullptr);
    }
}

constexpr tests::Case cases[] = {
    {"game.orders.replacement", command_replacement},
    {"game.orders.toggle_stop", active_auto_order_stops},
    {"game.orders.partial_setup", partial_setup_restores_globals},
    {"game.orders.preorder", preorder_suppresses_auto_command},
    {"game.orders.repeated_sessions", repeated_command_sessions},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Player command probe leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
