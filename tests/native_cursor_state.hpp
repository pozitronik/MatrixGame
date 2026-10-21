// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <cstdint>
#include <utility>

namespace tests::native_window {
// Test-only cursor presentation. No native cursor API is called by this state.
class CursorState {
    std::uintptr_t handle{};
    int display_count{};
public:
    std::uintptr_t set(std::uintptr_t next) { return std::exchange(handle, next); }
    int show(bool visible) { return display_count += visible ? 1 : -1; }
};
inline thread_local CursorState cursor;
}
