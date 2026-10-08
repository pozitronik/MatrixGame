// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"

#include "MatrixFormGame.hpp"
#include "input.hpp"

struct ConsoleInputFixture {
    static const std::wstring &text(const CDevConsole &console) { return console.m_Text; }
};

namespace {
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

constexpr tests::Case cases[] = {
    {"game.input.mouse_focus_reset", mouse_camera_focus_reset},
    {"game.input.mouse_fresh_press", mouse_camera_fresh_press},
    {"game.input.console_shift_focus", console_shift_focus_reset},
    {"game.input.console_editing", console_editing},
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
