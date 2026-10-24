// MatrixGame - licensed under GPLv2 or any later version.
#include "player_group_capacity_fixture.hpp"
#include "test_support.hpp"
#include <limits>

namespace {
using tests::capacity::FullPlayerGroups;

template <class Function> void profiles(Function run) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        for (int step : {10, 16}) {
            FullPlayerGroups scene(mode, step);
            std::cout << scene.session.metadata("player-group-exhaustion-v1") << '\n';
            try {
                run(scene);
            }
            catch (const Base::CException &error) {
                std::wcerr << error.Info() << '\n';
                throw;
            }
        }
    }
}

void robot_exhausted() {
    profiles([](FullPlayerGroups &scene) {
        MG_CHECK(!scene.side().HasFreePlayerLogicGroup());
        const auto before = scene.snapshot();
        // In the uncorrected Debug trace control ASSERT throws before indexing.
        const int group = scene.side().RobotToLogicGroup(scene.robots[0]);
        MG_CHECK(group == -1);
        scene.side().PGOrderAttack(group, CPoint(48, 48), nullptr);
        MG_CHECK(scene.snapshot() == before);
    });
}

void selection_exhausted() {
    profiles([](FullPlayerGroups &scene) {
        scene.select_pair();
        const auto before = scene.snapshot();
        const int group = scene.side().SelGroupToLogicGroup();
        MG_CHECK(group == -1);
        scene.side().PGOrderMoveTo(group, CPoint(48, 48));
        MG_CHECK(scene.snapshot() == before);
    });
}

void invalid_orders() {
    profiles([](FullPlayerGroups &scene) {
        const auto before = scene.snapshot();
        for (int group : {-1, MAX_LOGIC_GROUP, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
            auto &side = scene.side();
            side.PGOrderStop(group);
            side.PGOrderMoveTo(group, CPoint(48, 48));
            side.PGOrderAttack(group, CPoint(48, 48), nullptr);
            side.PGOrderCapture(group, nullptr);
            side.PGOrderPatrol(group, CPoint(48, 48));
            side.PGOrderRepair(group, nullptr);
            side.PGOrderBomb(group, CPoint(48, 48), nullptr);
            side.PGOrderAutoCapture(group);
            side.PGOrderAutoAttack(group);
            side.PGOrderAutoDefence(group);
            MG_CHECK(scene.snapshot() == before);
        }
    });
}

void availability() {
    profiles([](FullPlayerGroups &scene) {
        for (auto &group : scene.side().m_PlayerGroup) group.m_RobotCnt = 0;
        const auto before = scene.snapshot();
        MG_CHECK(!scene.side().HasFreePlayerLogicGroup());
        MG_CHECK(scene.snapshot() == before); // A query never recounts/mutates the cached fields.
        scene.release_last();
        for (auto &group : scene.side().m_PlayerGroup) group.m_RobotCnt = 7;
        const auto released = scene.snapshot();
        MG_CHECK(scene.side().HasFreePlayerLogicGroup());
        MG_CHECK(scene.snapshot() == released);
    });
}

void recovery() {
    profiles([](FullPlayerGroups &scene) {
        scene.release_last();
        MG_CHECK(scene.side().HasFreePlayerLogicGroup());
        const int group = scene.side().RobotToLogicGroup(scene.robots[0]);
        MG_CHECK(group == MAX_LOGIC_GROUP - 1 && scene.robots[0]->GetGroupLogic() == group);
        MG_CHECK(scene.side().m_PlayerGroup[group].Order() == mpo_Stop);
        MG_CHECK(scene.side().m_PlayerGroup[group].m_Obj == nullptr);
        MG_CHECK(!scene.side().m_PlayerGroup[group].IsWar());
        MG_CHECK(scene.side().m_PlayerGroup[group].m_RoadPath->m_ListCnt == 0);
        MG_CHECK(scene.side().HasFreePlayerLogicGroup()); // The first robot vacated slot zero.
        MG_CHECK(scene.side().RobotToLogicGroup(scene.robots[1]) == 0);
    });
    profiles([](FullPlayerGroups &scene) {
        scene.release_last();
        scene.select_pair();
        const int group = scene.side().SelGroupToLogicGroup();
        MG_CHECK(group == MAX_LOGIC_GROUP - 1);
        MG_CHECK(scene.robots[0]->GetGroupLogic() == group && scene.robots[1]->GetGroupLogic() == group);
        MG_CHECK(scene.side().m_PlayerGroup[group].m_RobotCnt == 2);
        MG_CHECK(scene.side().m_PlayerGroup[group].Order() == mpo_Stop);
        MG_CHECK(scene.side().m_PlayerGroup[group].m_RoadPath->m_ListCnt == 0);
    });
}

constexpr tests::Case cases[]{
    {"game.player.group_capacity.robot_exhausted", robot_exhausted},
    {"game.player.group_capacity.selection_exhausted", selection_exhausted},
    {"game.player.group_capacity.invalid_orders", invalid_orders},
    {"game.player.group_capacity.availability", availability},
    {"game.player.group_capacity.recovery", recovery},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
