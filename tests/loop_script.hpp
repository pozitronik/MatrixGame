// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include "test_support.hpp"
#include "3g.hpp"
#include "LoopPlatform.hpp"
#include <chrono>
#include <functional>
#include <utility>
#include <vector>

LRESULT CALLBACK L3G_WndProc(HWND, UINT, WPARAM, LPARAM);
namespace tests::loop {
constexpr UINT speed_message = WM_APP + 1;
struct Round {
    long long microseconds;
    std::vector<std::pair<UINT, WPARAM>> messages;
    std::function<void()> after_messages{};
};
struct Script {
    std::vector<Round> rounds;
    std::size_t round{}, message{}, clock_reads{}, waits{}, dispatches{};
    Loop::Clock::time_point time{};
    std::function<void()> on_wait{};
    Loop::Platform platform() {
        return {this,
            [](void *context) {
                auto &script = *static_cast<Script *>(context);
                ++script.clock_reads;
                return script.time;
            },
            [](void *context, MSG &message) {
                auto &script = *static_cast<Script *>(context);
                MG_CHECK(script.round < script.rounds.size()); // Bound a missing exit.
                const auto &round = script.rounds[script.round];
                script.time = Loop::Clock::time_point(std::chrono::duration_cast<Loop::Clock::duration>(
                    std::chrono::microseconds(round.microseconds)));
                if (script.message < round.messages.size()) {
                    const auto [kind, value] = round.messages[script.message++];
                    message = {};
                    message.message = kind;
                    message.wParam = value;
                    return true;
                }
                if (round.after_messages) round.after_messages();
                ++script.round;
                script.message = 0;
                return false;
            },
            [](void *context, const MSG &message) {
                auto &script = *static_cast<Script *>(context);
                ++script.dispatches;
                if (message.message == speed_message) {
                    INITFLAG(g_Flags, GFLAG_4SPEED, message.wParam != 0);
                } else L3G_WndProc(nullptr, message.message, message.wParam, message.lParam);
            },
            [](void *context) {
                auto &script = *static_cast<Script *>(context);
                ++script.waits;
                if (script.on_wait) script.on_wait();
            }
        };
    }
};
}
