// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"

#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "Interface/CInterface.h"
#include "input.hpp"

namespace {
template<class T>
struct EngineDelete {
    void operator()(T *value) const { HDelete(T, value, g_MatrixHeap); }
};

template<class T>
using EnginePtr = std::unique_ptr<T, EngineDelete<T>>;

// This probe starts with a selected, mesh-free robot. It does not test picking,
// movement or autonomous execution; command dispatch below is all production code.
struct PlayerCommandWorld {
    tests::CommandWorld world;
    EnginePtr<CIFaceList> ui{HNew(g_MatrixHeap) CIFaceList};
    EnginePtr<CMatrixRobotAI> robot{HNew(g_MatrixHeap) CMatrixRobotAI};
    CFormMatrixGame form;
    CIFaceList *previousInterface = g_IFaceList;

    PlayerCommandWorld() {
        g_IFaceList = ui.get();
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
    }

    ~PlayerCommandWorld() {
        Input::onFocusLost();
        side().GetCurSelGroup()->RemoveAll();
        side().SetCurGroup(nullptr);
        robot.reset();
        ui.reset();
        g_IFaceList = previousInterface;
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
    fixture.press('C');
    fixture.expect(mpo_AutoCapture, AUTO_CAPTURE_ON);
    fixture.press('U');
    fixture.expect(mpo_AutoAttack, AUTO_FROBOT_ON);
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
    {"game.command_probe.replacement", command_replacement},
    {"game.command_probe.preorder", preorder_suppresses_auto_command},
    {"game.command_probe.repeated_sessions", repeated_command_sessions},
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
