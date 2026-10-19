// MatrixGame - licensed under GPLv2 or any later version.
#include "native_window_containment.hpp"
#include "native_cursor_state.hpp"
#include "test_support.hpp"

namespace {
void contained_cursor_imports() {
    RECT before{}, after{};
    MG_CHECK(GetClipCursor(&before));
    const auto cursor = GetCursor();
    {
        tests::NativeWindowContainment containment(nullptr, false);
        MG_CHECK(containment.parent() && !IsWindowVisible(containment.parent()));
        const auto handle = reinterpret_cast<HCURSOR>(0x100);
        MG_CHECK(SetCursor(handle) == nullptr);
        MG_CHECK(SetCursor(nullptr) == handle);
        MG_CHECK(ShowCursor(FALSE) == -1 && ShowCursor(TRUE) == 0);
        const RECT synthetic{0, 0, 1, 1};
        MG_CHECK(ClipCursor(&synthetic) && ClipCursor(nullptr));
        MG_CHECK(GetClipCursor(&after) && EqualRect(&before, &after));
        MG_CHECK(GetCursor() == cursor);
    }
    MG_CHECK(GetClipCursor(&after) && EqualRect(&before, &after));
    MG_CHECK(GetCursor() == cursor);
}
void constructor_rollback() {
    // This executable deliberately has no named Documents API import. Strict
    // construction must reject it, restore changed imports and permit reuse.
    for (int repeat = 0; repeat < 2; ++repeat) {
        bool rejected = false;
        try { tests::NativeWindowContainment strict(nullptr); }
        catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).find("Documents") != std::string::npos;
        }
        MG_CHECK(rejected);
        contained_cursor_imports();
    }
}
constexpr tests::Case cases[] = {
    {"game.session.window_cursor_imports", contained_cursor_imports},
    {"game.session.window_constructor_rollback", constructor_rollback},
};
}
int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
