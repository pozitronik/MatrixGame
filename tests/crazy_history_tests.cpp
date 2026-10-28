// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "MatrixRobot.hpp"
#include "test_support.hpp"
#include <array>
#include <exception>
#include <iostream>

namespace {
using namespace tests::battle;

CMatrixRobotAI *registered(const Session &session, EntityId id) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (object->IsRobot() && session.matches(id, object->AsRobot())) return object->AsRobot();
    }
    throw std::runtime_error("Missing registered history actor");
}

// Drive the actual robot boundary without side/world ticks. Keeping the clock,
// perception and initially empty orders fixed isolates the legacy stuck history.
// This is not a movement, deployment or full-map scheduling fixture.
struct Scene {
    Session session;
    int step;
    std::array<CMatrixRobotAI *, 2> actors;

    Scene(random::Mode mode, int step_ms)
        : session({mode, 80, step_ms, 1000, false, false}), step(step_ms),
          actors{registered(session, 1), registered(session, 2)} {
        for (auto *robot : actors) {
            MG_CHECK(robot->GetGroupLogic() == -1 && !robot->PLIsInPlace());
            MG_CHECK(robot->GetEnv()->GetEnemyCnt() == 0 && robot->GetOrdersInPool() == 0);
        }
        MG_CHECK(actors[0]->GetMapPosY() != actors[1]->GetMapPosY());
        std::cout << session.metadata("crazy-history-robot-boundary-v1") << '\n';
    }

    void enable(unsigned actor) { actors.at(actor)->MarkCrazy(); }

    bool tick(unsigned actor) {
        auto *robot = actors.at(actor);
        const auto before = session.robot(actor + 1);
        MG_CHECK(before.alive && before.group == -1 && !before.in_place);
        MG_CHECK(robot->IsCrazy() && robot->GetEnv()->GetEnemyCnt() == 0);
        robot->LogicTakt(step);
        const auto after = session.robot(actor + 1);
        MG_CHECK(after.alive && robot->IsCrazy());
        MG_CHECK(session.time() == 0 && session.frame() == 0);
        if (after.group < 0) {
            MG_CHECK(after.x == before.x && after.y == before.y);
            MG_CHECK(robot->GetOrdersInPool() == 0 && !robot->PLIsInPlace());
            return false;
        }
        const auto &order = g_MatrixMap->GetPlayerSide()->m_PlayerGroup[after.group];
        MG_CHECK(order.Order() == mpo_Attack);
        MG_CHECK(order.m_Obj == actors[1 - actor]);
        MG_CHECK(after.target == int(2 - actor) && !after.target_is_null);
        std::cout << "retarget actor=" << actor + 1 << " group=" << after.group
                  << " target=" << after.target << " rng_draws=" << random::draw_count() << '\n';
        return true;
    }

    void quiet(unsigned actor, int calls) {
        for (int i = 0; i < calls; ++i) MG_CHECK(!tick(actor));
    }

    void first_attack(unsigned actor, int expected) {
        quiet(actor, expected - 1);
        MG_CHECK(tick(actor));
        std::cout << "first_attack_call=" << expected << '\n';
    }
};

template <random::Mode Mode, int Step> void fresh() {
    Scene scene(Mode, Step);
    scene.enable(0);
    // First visit changes (-1,-1), then 101 equal-coordinate visits cross >100.
    scene.first_attack(0, 102);
}

template <random::Mode Mode, int Step> void cross_session() {
    {
        Scene prime(Mode, Step);
        prime.enable(0);
        prime.quiet(0, 37); // One coordinate change followed by 36 increments.
    }
    {
        Scene next(Mode, Step);
        next.enable(0);
        next.first_attack(0, 65); // The next Session inherits both coordinates and count.
    }
    {
        Scene after_attack(Mode, Step);
        after_attack.enable(0);
        after_attack.first_attack(0, 101); // Retarget clears count, not saved coordinates.
    }
}

template <random::Mode Mode, int Step> void changed_coordinates() {
    {
        Scene prime(Mode, Step);
        prime.enable(0);
        prime.quiet(0, 37);
    }
    Scene next(Mode, Step);
    next.enable(1);
    // Changing coordinates does not clear the inherited 36 increments.
    next.first_attack(1, 66);
}

template <random::Mode Mode, int Step> void shared_robots() {
    Scene scene(Mode, Step);
    scene.enable(0);
    scene.enable(1);
    scene.quiet(0, 37);
    scene.first_attack(1, 66);
    MG_CHECK(scene.actors[0]->GetGroupLogic() == -1);
}

template <random::Mode Mode, int Step> void alternating_robots() {
    Scene scene(Mode, Step);
    scene.enable(0);
    scene.enable(1);
    for (int i = 0; i < 120; ++i) {
        MG_CHECK(!scene.tick(0));
        MG_CHECK(!scene.tick(1));
    }
    // Alternating distinct positions prevents increments; removing that
    // interleaving restores the fresh-position threshold for the first actor.
    scene.first_attack(0, 102);
    MG_CHECK(scene.actors[1]->GetGroupLogic() == -1);
}

#define HISTORY_CASES(mode, step) \
    {"game.player.crazy_history.fresh." #mode "_" #step, fresh<random::Mode::mode, step>}, \
    {"game.player.crazy_history.cross_session." #mode "_" #step, cross_session<random::Mode::mode, step>}, \
    {"game.player.crazy_history.changed_coordinates." #mode "_" #step, changed_coordinates<random::Mode::mode, step>}, \
    {"game.player.crazy_history.shared_robots." #mode "_" #step, shared_robots<random::Mode::mode, step>}, \
    {"game.player.crazy_history.alternating_robots." #mode "_" #step, alternating_robots<random::Mode::mode, step>}

constexpr tests::Case cases[]{
    HISTORY_CASES(ParkMiller, 10),
    HISTORY_CASES(ParkMiller, 16),
    HISTORY_CASES(LegacyCRT, 10),
    HISTORY_CASES(LegacyCRT, 16),
};
#undef HISTORY_CASES
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
