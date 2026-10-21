// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "loop_script.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "input.hpp"
#include <cmath>
#include <float.h>
#include <string_view>

namespace {
using namespace tests::battle;
using tests::loop::Round;
using tests::loop::Script;
constexpr bool cheats = MATRIXGAME_SCENARIO_CHEATS != 0;
void type(Round &round, std::string_view text) {
    for (unsigned char key : text) {
        round.messages.emplace_back(WM_KEYDOWN, key);
        round.messages.emplace_back(WM_KEYUP, key);
    }
}
struct LoopForm : CFormMatrixGame {
    Session &session;
    CForm *previous = g_FormCur;
    std::vector<int> ticks;
    std::vector<ESysEvent> events;
    std::vector<bool> held_before_event;
    std::vector<bool> held_after_event;
    std::vector<std::string> trace;
    unsigned draws{};
    explicit LoopForm(Session &value) : session(value) {
        g_FormCur = this;
        SETFLAG(g_Flags, GFLAG_APPACTIVE);
        SETFLAG(g_MatrixMap->m_Flags, MMFLAG_VIDEO_RESOURCES_READY);
    }
    ~LoopForm() { g_FormCur = previous; }
    void Takt(int delta) override {
        MG_CHECK(draws == ticks.size());
#if defined(__MINGW32__)
        MG_CHECK((_controlfp(0, 0) & _MCW_PC) == _PC_24);
#endif
        const int before = session.time();
        ticks.push_back(delta);
        CFormMatrixGame::Takt(delta);
        MG_CHECK(session.time() == before + delta);
        for (EntityId id : {1u, 2u}) {
            const auto robot = session.robot(id);
            MG_CHECK(robot.alive && std::isfinite(robot.x) && std::isfinite(robot.y));
            MG_CHECK(robot.order == mpo_MoveTo && robot.destination_x == 48 && robot.destination_y == 28);
        }
#if defined(__MINGW32__)
        MG_CHECK((_controlfp(0, 0) & _MCW_PC) == _PC_24);
#endif
    }
    void Draw() override {
        MG_CHECK(draws + 1 == ticks.size() && !FLAG(g_Flags, GFLAG_APPCLOSE));
        ++draws;
        g_MatrixMap->m_Camera.BeforeDraw(); // CPU view update; no native presentation.
        trace.push_back(std::to_string(draws) + ":" + session.snapshot());
    }
    void SystemEvent(ESysEvent event) override {
        held_before_event.push_back(Input::isVKeyPressed(VK_UP));
        // This no-render fixture has no default-pool presentation resources.
        // Exercise the actual input reset while leaving GPU release/recreation
        // to the device contracts and native focus validation.
        if (event == SYSEV_DEACTIVATING) RESETFLAG(g_MatrixMap->m_Flags, MMFLAG_VIDEO_RESOURCES_READY);
        CFormMatrixGame::SystemEvent(event);
        if (event == SYSEV_DEACTIVATING) SETFLAG(g_MatrixMap->m_Flags, MMFLAG_VIDEO_RESOURCES_READY);
        MG_CHECK(g_MatrixMap->m_VKeyDown == 0);
        events.push_back(event);
        held_after_event.push_back(Input::isVKeyPressed(VK_UP));
    }
};
void begin_move(Session &session) {
    auto &side = *g_MatrixMap->GetPlayerSide();
    side.GetCurSelGroup()->RemoveAll();
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot() && (session.matches(1, object->AsRobot()) || session.matches(2, object->AsRobot())))
            side.GetCurSelGroup()->AddObject(object, -4);
    side.CreateGroupFromCurrent();
    side.m_CurrSel = GROUP_SELECTED;
    session.ground_command(500, 300);
    MG_CHECK(session.robot(1).order == mpo_MoveTo && session.robot(2).order == mpo_MoveTo);
    // Deselect through production before typing developer words: ordinary letter
    // shortcuts would otherwise also act on the selected units while typing.
    g_MatrixMap->GetPlayerSide()->PLDropAllActions();
    MG_CHECK(g_MatrixMap->GetPlayerSide()->GetCurGroup() == nullptr);
}
void check_movement(Session &session, LoopForm &form, Script &script, int time) {
    MG_CHECK(session.time() == time && FLAG(g_Flags, GFLAG_APPCLOSE));
    MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP) && !g_MatrixMap->IsPaused());
    MG_CHECK(form.draws == form.ticks.size() && form.trace.size() == form.draws);
    MG_CHECK(script.clock_reads == form.ticks.size() + 1 && script.round == script.rounds.size());
    for (EntityId id : {1u, 2u}) {
        const auto robot = session.robot(id);
        std::cout << "moved id=" << id << " x=" << robot.x << " y=" << robot.y
                  << " order=" << robot.order << " destination=" << robot.destination_x << ',' << robot.destination_y << '\n';
        MG_CHECK(robot.alive && robot.x > 100);
        MG_CHECK(robot.order == mpo_MoveTo && robot.destination_x == 48 && robot.destination_y == 28);
    }
}
std::vector<std::string> speed_run(random::Mode mode, int step) {
    Session session({mode, 85, step, 1000, false});
    std::cout << session.metadata("loop-moving-speed-v2") << '\n';
    LoopForm form(session);
    begin_move(session);
    Script script{{{step * 1000LL, {}}, {step * 1000LL + 24750, {}},
        {step * 1000LL + 49750, {}}, {step * 1000LL + 99750, {}},
        {step * 1000LL + 100500, {}}, {step * 1000LL + 200500, {}},
        {step * 1000LL + 400500, {}}, {step * 1000LL + 600500, {{WM_CLOSE, 0}}}}};
    type(script.rounds[1], "NEED4SPEED");
    type(script.rounds[4], "NEED4SPEED");
    // Prefix letters still invoke ordinary shortcuts. Submit a fresh context
    // Move after each typed toggle to isolate the subsequent scheduler interval.
    script.rounds[1].after_messages = [&] { begin_move(session); };
    script.rounds[4].after_messages = [&] { begin_move(session); };
    std::string close_state;
    script.rounds.back().after_messages = [&] {
        MG_CHECK(FLAG(g_Flags, GFLAG_APPCLOSE));
        close_state = session.snapshot();
    };
    MG_CHECK(L3GRun(script.platform()) == 1);
    MG_CHECK(!close_state.empty() && session.snapshot() == close_state);
    const std::vector<int> expected = cheats ? std::vector<int>{step, 99, 100, 100, 0, 100, 100}
        : std::vector<int>{step, 24, 25, 50, 0, 100, 100};
    MG_CHECK(form.ticks == expected && !FLAG(g_Flags, GFLAG_4SPEED));
    MG_CHECK(script.waits == 0 && form.events.empty());
    check_movement(session, form, script, step + (cheats ? 499 : 299));
    return form.trace;
}
void speed_movement() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const auto first = speed_run(mode, step);
        MG_CHECK(first == speed_run(mode, step));
    }
}
std::vector<std::string> focus_run(random::Mode mode, int step, bool request_keepalive) {
    Session session({mode, 85, step, 1000, false});
    std::cout << session.metadata("loop-moving-focus-v1") << " request_keepalive=" << request_keepalive << '\n';
    LoopForm form(session);
    begin_move(session);
    Script script{{{step * 1000LL, {}}, {step * 2000LL, {{WM_ACTIVATEAPP, FALSE}}},
        {250000, {}}, {260000, {{WM_ACTIVATEAPP, TRUE}, {WM_KEYDOWN, VK_UP}}},
        {270000, {{WM_KEYUP, VK_UP}, {WM_CLOSE, 0}}}}};
    if (request_keepalive) type(script.rounds[0], "KEEPALIVE");
    script.rounds[0].after_messages = [&] {
        begin_move(session);
        MG_CHECK(Input::isVKeyPressed(VK_UP) && g_MatrixMap->m_VKeyDown == VK_UP);
    };
    script.rounds[0].messages.emplace_back(WM_KEYDOWN, VK_UP);
    script.rounds[3].after_messages = [&] {
        MG_CHECK(Input::isVKeyPressed(VK_UP) && g_MatrixMap->m_VKeyDown == VK_UP);
    };
    std::vector<std::string> inactive;
    script.on_wait = [&] {
        MG_CHECK(session.time() == step && !Input::isVKeyPressed(VK_UP));
        inactive.push_back(session.snapshot());
        MG_CHECK(inactive.back() == inactive.front());
    };
    std::string close_state;
    script.rounds.back().after_messages = [&] {
        MG_CHECK(FLAG(g_Flags, GFLAG_APPCLOSE));
        close_state = session.snapshot();
    };
    MG_CHECK(L3GRun(script.platform()) == 1);
    MG_CHECK(!close_state.empty() && session.snapshot() == close_state);
    const bool keepalive = request_keepalive && cheats;
    MG_CHECK(form.ticks == (keepalive ? std::vector<int>{step, step, 100, 10} : std::vector<int>{step, 100}));
    MG_CHECK(form.events == (keepalive ? std::vector<ESysEvent>{SYSEV_INPUT_RESET}
        : std::vector<ESysEvent>{SYSEV_DEACTIVATING, SYSEV_ACTIVATED}));
    MG_CHECK(form.held_before_event == (keepalive ? std::vector<bool>{true} : std::vector<bool>{true, false}));
    for (bool held : form.held_after_event) MG_CHECK(!held);
    MG_CHECK(!Input::isVKeyPressed(VK_UP) && g_MatrixMap->m_VKeyDown == 0);
    MG_CHECK(bool(FLAG(g_Flags, GFLAG_KEEPALIVE)) == keepalive);
    MG_CHECK(script.waits == (keepalive ? 0u : 2u));
    MG_CHECK(inactive.size() == script.waits);
    check_movement(session, form, script, keepalive ? 2 * step + 110 : step + 100);
    return form.trace;
}
void focus_movement() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16})
    for (bool keepalive : {false, true}) {
        const auto first = focus_run(mode, step, keepalive);
        MG_CHECK(first == focus_run(mode, step, keepalive));
    }
}
constexpr tests::Case cases[] = {
    {"game.loop.speed_movement", speed_movement},
    {"game.loop.focus_movement", focus_movement},
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
