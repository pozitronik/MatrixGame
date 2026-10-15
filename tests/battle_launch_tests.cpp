// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "BattleLaunch.hpp"
#include "MenuKeyLatch.hpp"
#include <shellapi.h>
#include <array>

namespace {
void arguments() {
    MG_CHECK(!BattleLaunch::classify({}).selected);
    for (const auto name : {L"Matrix\\Map\\test.cmap", L"--start-selected-battle.cmap", L"path/--start-selected-battle"}) {
        const std::wstring_view values[] = {name, L"ignored"};
        const auto result = BattleLaunch::classify(values);
        MG_CHECK(!result.selected && result.map == name && result.event.empty());
    }
    const std::wstring_view selected[] = {BattleLaunch::selected_argument};
    MG_CHECK(BattleLaunch::classify(selected).selected && BattleLaunch::classify(selected).event.empty());
    const std::wstring_view handoff[] = {BattleLaunch::selected_argument, L"Local\\MatrixGame-Battle-1-2"};
    MG_CHECK(BattleLaunch::classify(handoff).selected && BattleLaunch::classify(handoff).event == handoff[1]);
    const std::wstring_view bad[] = {BattleLaunch::selected_argument, L"event", L"extra"};
    bool caught = false;
    try { (void)BattleLaunch::classify(bad); } catch (const std::runtime_error &) { caught = true; }
    MG_CHECK(caught);
}

void command() {
    const std::wstring executable = L"C:\\Game folder\\\u0418\u0433\u0440\u0430\\MatrixGame.exe";
    const std::wstring event = L"Local\\MatrixGame-Battle-1-2";
    const auto line = BattleLaunch::command(executable, event);
    int count = 0;
    struct Arguments { LPWSTR *value; ~Arguments() { LocalFree(value); } } args{CommandLineToArgvW(line.c_str(), &count)};
    MG_CHECK(args.value && count == 3);
    MG_CHECK(args.value[0] == executable && args.value[1] == BattleLaunch::selected_argument && args.value[2] == event);
}

void action_keys() {
    MenuKeyLatch menu;
    MG_CHECK(menu.accept(VK_RETURN, true));
    MenuKeyLatch selector{true, false}; // Opened by the same Enter press.
    MG_CHECK(!selector.accept(VK_RETURN, true));
    MG_CHECK(!selector.accept(VK_RETURN, false));
    MG_CHECK(selector.accept(VK_RETURN, true));
    MG_CHECK(!selector.accept(VK_RETURN, true));
    MG_CHECK(selector.accept(VK_DOWN, true) && selector.accept(VK_DOWN, true));
    MG_CHECK(menu.accept(VK_ESCAPE, true)); // Parent observes the dialog's Escape too.
    MG_CHECK(selector.accept(VK_ESCAPE, true));
    MG_CHECK(!menu.accept(VK_ESCAPE, true)); // Repeat after closing the selector must not exit.
    MG_CHECK(!menu.accept(VK_ESCAPE, false));
    MG_CHECK(menu.accept(VK_ESCAPE, true));
    MenuKeyLatch held_escape{false, true};
    MG_CHECK(!held_escape.accept(VK_ESCAPE, true));
    MG_CHECK(!held_escape.accept(VK_ESCAPE, false));
    MG_CHECK(held_escape.accept(VK_ESCAPE, true));
}
constexpr tests::Case cases[] = {
    {"game.startup.arguments", arguments}, {"game.startup.relaunch_command", command},
    {"game.startup.action_keys", action_keys},
};
}
int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
