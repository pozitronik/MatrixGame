// MatrixGame - licensed under GPLv2 or any later version.
#include "native_window_containment.hpp"
#include "native_cursor_state.hpp"
#include "test_support.hpp"

namespace {
int leave_messages{};
LRESULT CALLBACK window_messages(HWND window, UINT message, WPARAM word, LPARAM value) {
    if (message == WM_MOUSELEAVE) ++leave_messages;
    return DefWindowProcW(window, message, word, value);
}
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
void explicit_mouse_leave() {
    tests::NativeWindowContainment containment(nullptr, false);
    struct Child {
        WNDCLASSW type{};
        ATOM registered{};
        HWND window{};
        Child() {
            type.lpfnWndProc = window_messages;
            type.hInstance = GetModuleHandleW(nullptr);
            type.lpszClassName = L"MatrixGame_wc";
            registered = RegisterClassW(&type);
            if (registered) window = CreateWindowExW(0, type.lpszClassName, L"Contained input", WS_POPUP,
                0, 0, 32, 32, nullptr, nullptr, type.hInstance, nullptr);
        }
        ~Child() {
            if (window) DestroyWindow(window);
            if (registered) UnregisterClassW(type.lpszClassName, type.hInstance);
        }
    } child;
    MG_CHECK(child.registered && child.window && GetParent(child.window) == containment.parent());
    ShowWindow(child.window, SW_SHOW);
    MG_CHECK(!IsWindowVisible(child.window));
    leave_messages = 0;
    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, child.window, 0};
    MG_CHECK(TrackMouseEvent(&tracking));
    TRACKMOUSEEVENT query{sizeof(query), TME_QUERY, child.window, 0};
    MG_CHECK(TrackMouseEvent(&query) && !(query.dwFlags & TME_LEAVE));
    const auto pump = [] {
        MSG message{};
        const auto start = GetTickCount();
        do {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            Sleep(1);
        } while (GetTickCount() - start < 100);
    };
    pump();
    MG_CHECK(leave_messages == 0);
    MG_CHECK(PostMessageW(child.window, WM_MOUSELEAVE, 0, 0));
    pump();
    MG_CHECK(leave_messages == 1);
    TRACKMOUSEEVENT invalid{};
    MG_CHECK(!TrackMouseEvent(&invalid)); // Other requests retain native validation.
}
constexpr tests::Case cases[] = {
    {"game.session.window_cursor_imports", contained_cursor_imports},
    {"game.session.window_constructor_rollback", constructor_rollback},
    {"game.session.window_explicit_mouse_leave", explicit_mouse_leave},
};
}
int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
