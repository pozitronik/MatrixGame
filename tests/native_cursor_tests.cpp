// MatrixGame - licensed under GPLv2 or any later version.
#include "native_cursor_state.hpp"
#include "test_support.hpp"
#include <array>
#include <thread>

namespace {
void previous_handle() {
    auto &cursor = tests::native_window::cursor;
    MG_CHECK(cursor.set(0x100) == 0);
    MG_CHECK(cursor.set(0x200) == 0x100);
    MG_CHECK(cursor.set(0) == 0x200);
    MG_CHECK(cursor.set(0x300) == 0);
}
void display_balance() {
    auto &cursor = tests::native_window::cursor;
    MG_CHECK(cursor.show(false) == -1);
    MG_CHECK(cursor.show(false) == -2);
    MG_CHECK(cursor.show(true) == -1);
    MG_CHECK(cursor.show(true) == 0);
    MG_CHECK(cursor.show(true) == 1);
    MG_CHECK(cursor.show(true) == 2);
    int retries = 0;
    while (cursor.show(false) >= 0) MG_CHECK(++retries <= 2);
    MG_CHECK(retries == 2);
    MG_CHECK(cursor.show(true) == 0);
}
void thread_isolation() {
    auto &cursor = tests::native_window::cursor;
    MG_CHECK(cursor.set(0x100) == 0 && cursor.show(false) == -1);
    std::array<std::uintptr_t, 3> child{};
    std::thread worker([&] {
        auto &local = tests::native_window::cursor;
        child[0] = local.set(0x200);
        child[1] = local.show(true);
        child[2] = local.set(0x300);
    });
    worker.join();
    MG_CHECK(child == (std::array<std::uintptr_t, 3>{0, 1, 0x200}));
    MG_CHECK(cursor.set(0x400) == 0x100 && cursor.show(true) == 0);
}
constexpr tests::Case cases[] = {
    {"game.session.cursor_previous_handle", previous_handle},
    {"game.session.cursor_display_balance", display_balance},
    {"game.session.cursor_thread_isolation", thread_isolation},
};
}
int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
