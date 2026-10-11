// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"

#include "MatrixFormGame.hpp"
#include "input.hpp"
#include "WindowDisplay.hpp"

LRESULT CALLBACK L3G_WndProc(HWND, UINT, WPARAM, LPARAM);

struct ConsoleInputFixture {
    static const std::wstring &text(const CDevConsole &console) { return console.m_Text; }
};

namespace {
void display_shortcut_routing() {
    struct RecordingForm : CForm {
        int keys = 0;
        void Enter() override {}
        void Leave() override {}
        void Draw() override {}
        void Takt(int) override {}
        void MouseMove(int, int) override {}
        void MouseKey(ButtonStatus, int, int, int) override {}
        void Keyboard(bool, uint8_t) override { ++keys; }
        void SystemEvent(ESysEvent) override {}
    } form;
    struct Restore {
        CForm *previous = g_FormCur;
        DWORD flags = g_Flags;
        ~Restore() { g_FormCur = previous; g_Flags = flags; Display::shutdown(); }
    } restore;
    g_FormCur = &form;
    g_Flags = GFLAG_APPACTIVE;
    Display::initialize({});
    L3G_WndProc(nullptr, WM_SYSKEYDOWN, VK_RETURN, 1u << 29);
    L3G_WndProc(nullptr, WM_SYSKEYDOWN, VK_RETURN, (1u << 29) | (1u << 30));
    L3G_WndProc(nullptr, WM_KEYUP, VK_RETURN, 0);
    MG_CHECK(form.keys == 0);
    L3G_WndProc(nullptr, WM_KEYDOWN, VK_RETURN, 0);
    L3G_WndProc(nullptr, WM_KEYUP, VK_RETURN, 0);
    MG_CHECK(form.keys == 2);
    MG_CHECK(g_D3DD == nullptr); // The shortcut only queues work; dispatch never touches a device.
    Display::shutdown(); // DLL sessions do not initialize standalone display handling.
    L3G_WndProc(nullptr, WM_SYSKEYDOWN, VK_RETURN, 1u << 29);
    L3G_WndProc(nullptr, WM_SYSKEYUP, VK_RETURN, 0);
    MG_CHECK(form.keys == 4);
}

void mouse_camera_focus_reset() {
    tests::CommandWorld world;
    CFormMatrixGame form;
    form.MouseKey(B_DOWN, VK_MBUTTON, 8, 8);
    MG_CHECK(world.map->IsMouseCam());
    Input::onKeyDown(VK_W);
    world.map->m_VKeyDown = VK_W;
    form.SystemEvent(SYSEV_DEACTIVATING);
    MG_CHECK(!world.map->IsMouseCam());
    MG_CHECK(!Input::isVKeyPressed(VK_W));
    MG_CHECK(world.map->m_VKeyDown == 0);
    MG_CHECK(g_D3DD == nullptr);
}

void pointer_leaves_window() {
    tests::CommandWorld world;
    CFormMatrixGame form;
    form.MouseKey(B_DOWN, VK_MBUTTON, 8, 8);
    Input::onKeyDown(VK_LEFT);
    form.SystemEvent(SYSEV_POINTER_LEFT);
    MG_CHECK(world.map->m_Cursor.GetPosX() == -1 && world.map->m_Cursor.GetPosY() == -1);
    MG_CHECK(!world.map->IsMouseCam());
    MG_CHECK(Input::isVKeyPressed(VK_LEFT)); // Leaving the client does not lose keyboard focus.
    Input::onFocusLost();
}

void standalone_screenshot_waits_for_frame() {
    tests::CommandWorld world;
    CFormMatrixGame form;
    struct DisplaySession {
        DisplaySession() { Display::initialize({}); }
        ~DisplaySession() { Display::shutdown(); Input::onFocusLost(); }
    } session;
    MG_CHECK(g_D3DD == nullptr);
    form.Keyboard(true, VK_F9); // Must not capture a discarded or lost render target from input dispatch.
    form.Keyboard(false, VK_F9);
    form.Draw(); // Resources are unavailable: the request must wait for a drawable frame.
    MG_CHECK(!FLAG(world.map->m_Flags, MMFLAG_VIDEO_RESOURCES_READY));
}

void mouse_camera_fresh_press() {
    tests::CommandWorld world;
    CFormMatrixGame form;
    form.MouseKey(B_DOWN, VK_MBUTTON, 8, 8);
    form.SystemEvent(SYSEV_DEACTIVATING);
    form.SystemEvent(SYSEV_ACTIVATED);
    MG_CHECK(!world.map->IsMouseCam());
    form.MouseKey(B_UP, VK_MBUTTON, 8, 8);
    MG_CHECK(!world.map->IsMouseCam());
    form.MouseKey(B_DOWN, VK_MBUTTON, 8, 8);
    MG_CHECK(world.map->IsMouseCam());
    form.MouseKey(B_UP, VK_MBUTTON, 8, 8);
    MG_CHECK(!world.map->IsMouseCam());
}

void console_shift_focus_reset() {
    tests::CommandWorld world;
    CFormMatrixGame form;
    world.map->m_Console.SetActive(true);
    form.Keyboard(true, VK_SHIFT);
    form.Keyboard(true, VK_A);
    MG_CHECK(ConsoleInputFixture::text(world.map->m_Console) == L"A");
    form.SystemEvent(SYSEV_DEACTIVATING);
    form.SystemEvent(SYSEV_ACTIVATED);
    form.Keyboard(true, VK_B);
    MG_CHECK(ConsoleInputFixture::text(world.map->m_Console) == L"Ab");
    MG_CHECK(world.map->m_Console.IsActive());
}

void console_editing() {
    tests::CommandWorld world;
    CFormMatrixGame form;
    auto &console = world.map->m_Console;
    console.SetActive(true);
    form.Keyboard(true, VK_A);
    form.Keyboard(true, VK_C);
    form.Keyboard(true, VK_LEFT);
    form.Keyboard(true, VK_B);
    MG_CHECK(ConsoleInputFixture::text(console) == L"abc");
    form.Keyboard(true, VK_BACK);
    MG_CHECK(ConsoleInputFixture::text(console) == L"ac");
    form.Keyboard(true, VK_DELETE);
    MG_CHECK(ConsoleInputFixture::text(console) == L"a");
    form.Keyboard(true, VK_ESCAPE);
    MG_CHECK(ConsoleInputFixture::text(console).empty());
    MG_CHECK(console.IsActive());
    form.Keyboard(true, VK_ESCAPE);
    MG_CHECK(!console.IsActive());
}

void input_only_mouse_reset() {
    tests::CommandWorld world;
    CFormMatrixGame form;
    g_Flags = GFLAG_KEEPALIVE | GFLAG_APPACTIVE;
    form.MouseKey(B_DOWN, VK_MBUTTON, 8, 8);
    MG_CHECK(world.map->IsMouseCam());
    world.map->m_Flags |= MMFLAG_VIDEO_RESOURCES_READY;
    const DWORD map_flags = world.map->m_Flags & ~MMFLAG_MOUSECAM;
    const DWORD app_flags = g_Flags;
    Input::onKeyDown(VK_W);
    world.map->m_VKeyDown = VK_W;
    form.SystemEvent(SYSEV_INPUT_RESET);
    MG_CHECK(!world.map->IsMouseCam() && !Input::isVKeyPressed(VK_W));
    MG_CHECK(world.map->m_VKeyDown == 0);
    MG_CHECK(world.map->m_Flags == map_flags && g_Flags == app_flags);
    MG_CHECK(g_D3DD == nullptr);
    form.SystemEvent(SYSEV_INPUT_RESET);
    form.MouseKey(B_DOWN, VK_MBUTTON, 8, 8);
    MG_CHECK(world.map->IsMouseCam());
    form.MouseKey(B_UP, VK_MBUTTON, 8, 8);
    MG_CHECK(!world.map->IsMouseCam());
}

void input_only_console_reset() {
    tests::CommandWorld world;
    CFormMatrixGame form;
    g_Flags = GFLAG_KEEPALIVE | GFLAG_APPACTIVE;
    world.map->m_Console.SetActive(true);
    form.Keyboard(true, VK_SHIFT);
    form.Keyboard(true, VK_A);
    MG_CHECK(ConsoleInputFixture::text(world.map->m_Console) == L"A");
    world.map->m_Flags |= MMFLAG_VIDEO_RESOURCES_READY;
    const DWORD map_flags = world.map->m_Flags;
    const DWORD app_flags = g_Flags;
    form.SystemEvent(SYSEV_INPUT_RESET);
    form.Keyboard(true, VK_B);
    MG_CHECK(ConsoleInputFixture::text(world.map->m_Console) == L"Ab");
    MG_CHECK(world.map->m_Console.IsActive());
    MG_CHECK(world.map->m_Flags == map_flags && g_Flags == app_flags);
    MG_CHECK(g_D3DD == nullptr);
}

constexpr tests::Case cases[] = {
    {"game.input.display_shortcut", display_shortcut_routing},
    {"game.input.pointer_left", pointer_leaves_window},
    {"game.input.screenshot_deferred", standalone_screenshot_waits_for_frame},
    {"game.input.mouse_focus_reset", mouse_camera_focus_reset},
    {"game.input.mouse_fresh_press", mouse_camera_fresh_press},
    {"game.input.console_shift_focus", console_shift_focus_reset},
    {"game.input.console_editing", console_editing},
    {"game.input.input_only_mouse_reset", input_only_mouse_reset},
    {"game.input.input_only_console_reset", input_only_console_reset},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Form input fixture leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
