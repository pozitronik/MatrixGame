// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "battle_device.hpp"
#include "3g.hpp"
#include "LoopPlatform.hpp"
#include "loop_script.hpp"
#include <chrono>
#include <vector>

LRESULT CALLBACK L3G_WndProc(HWND, UINT, WPARAM, LPARAM);

namespace {
using tests::loop::Script;
using tests::loop::speed_message;
struct RecordingForm : CForm {
    DWORD old_flags = g_Flags;
    CForm *old_form = g_FormCur;
    int old_memory = g_AvailableTexMem, old_fps = g_DrawFPS;
    std::vector<int> ticks;
    std::vector<ESysEvent> events;
    unsigned draws{};
    bool exit_in_tick{};
    RecordingForm() { g_FormCur = this; g_Flags = GFLAG_APPACTIVE; }
    ~RecordingForm() {
        g_FormCur = old_form;
        g_Flags = old_flags;
        g_AvailableTexMem = old_memory;
        g_DrawFPS = old_fps;
    }
    void Enter() override {}
    void Leave() override {}
    void Draw() override { ++draws; MG_CHECK(draws == ticks.size()); }
    void Takt(int delta) override {
        ticks.push_back(delta);
        if (exit_in_tick) SETFLAG(g_Flags, GFLAG_EXITLOOP);
    }
    void MouseMove(int, int) override {}
    void MouseKey(ButtonStatus, int, int, int) override {}
    void Keyboard(bool, uint8_t) override {}
    void SystemEvent(ESysEvent event) override { events.push_back(event); }
};
void timing() {
    tests::BattleDevice device;
    RecordingForm form;
    Script script{{{400, {}}, {1600, {}}, {26350, {{speed_message, TRUE}}},
        {51350, {}}, {100350, {}}, {101100, {{speed_message, FALSE}}},
        {200100, {}}, {300100, {}}, {450100, {}}, {500000, {{WM_CLOSE, 0}}}}};
    MG_CHECK(L3GRun(script.platform()) == 1);
    MG_CHECK((form.ticks == std::vector<int>{0, 1, 99, 100, 100, 0, 99, 100, 100}));
    MG_CHECK(form.draws == 9 && script.clock_reads == 10 && script.waits == 0);
    MG_CHECK(script.dispatches == 3 && script.round == script.rounds.size());
    MG_CHECK(FLAG(g_Flags, GFLAG_APPCLOSE) && !FLAG(g_Flags, GFLAG_4SPEED));
    MG_CHECK(device.stats().texture_memory_queries == form.draws && g_AvailableTexMem == 64);
}
void activation() {
    for (bool keepalive : {false, true}) {
        tests::BattleDevice device;
        RecordingForm form;
        INITFLAG(g_Flags, GFLAG_KEEPALIVE, keepalive);
        Script script{{{10000, {}}, {20000, {{WM_ACTIVATEAPP, FALSE}}},
            {250000, {}}, {260000, {{WM_ACTIVATEAPP, TRUE}}}, {270000, {{WM_CLOSE, 0}}}}};
        MG_CHECK(L3GRun(script.platform()) == 1);
        MG_CHECK(form.ticks == (keepalive ? std::vector<int>{10, 10, 100, 10} : std::vector<int>{10, 100}));
        MG_CHECK(form.events == (keepalive ? std::vector<ESysEvent>{SYSEV_INPUT_RESET}
            : std::vector<ESysEvent>{SYSEV_DEACTIVATING, SYSEV_ACTIVATED}));
        MG_CHECK(script.waits == (keepalive ? 0u : 2u));
        MG_CHECK(script.clock_reads == form.ticks.size() + 1);
        MG_CHECK(device.stats().texture_memory_queries == form.draws);
        MG_CHECK(FLAG(g_Flags, GFLAG_APPACTIVE) && FLAG(g_Flags, GFLAG_APPCLOSE));
    }
}
void exit_boundaries() {
    for (bool during_tick : {false, true}) {
        tests::BattleDevice device;
        RecordingForm form;
        form.exit_in_tick = during_tick;
        Script script{{{10000, {}}, {20000, {}}}};
        if (!during_tick) SETFLAG(g_Flags, GFLAG_EXITLOOP);
        MG_CHECK(L3GRun(script.platform()) == 1);
        MG_CHECK(form.ticks == (during_tick ? std::vector<int>{10} : std::vector<int>{}));
        MG_CHECK(form.draws == (during_tick ? 1u : 0u));
        MG_CHECK(script.clock_reads == form.draws + 1 && script.waits == 0);
        MG_CHECK(device.stats().texture_memory_queries == form.draws);
        MG_CHECK(FLAG(g_Flags, GFLAG_EXITLOOP) && !FLAG(g_Flags, GFLAG_APPCLOSE));
    }
}
void missing_form() {
    tests::BattleDevice device;
    RecordingForm form;
    g_FormCur = nullptr;
    Script script{{{10000, {}}, {250000, {}}, {500000, {{WM_CLOSE, 0}}}}};
    MG_CHECK(L3GRun(script.platform()) == 1);
    MG_CHECK(script.waits == 2 && script.clock_reads == 1);
    MG_CHECK(form.ticks.empty() && form.draws == 0);
    MG_CHECK(device.stats().texture_memory_queries == 0 && FLAG(g_Flags, GFLAG_APPCLOSE));
}
void native_exit() {
    tests::BattleDevice device;
    RecordingForm form;
    SETFLAG(g_Flags, GFLAG_EXITLOOP);
    MG_CHECK(L3GRun() == 1);
    MG_CHECK(form.ticks.empty() && form.draws == 0);
    MG_CHECK(device.stats().texture_memory_queries == 0 && FLAG(g_Flags, GFLAG_EXITLOOP));
}
constexpr tests::Case cases[] = {
    {"game.loop.timing", timing},
    {"game.loop.activation", activation},
    {"game.loop.exit_boundaries", exit_boundaries},
    {"game.loop.missing_form", missing_form},
    {"game.loop.native_exit", native_exit},
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
