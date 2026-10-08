// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"

#include "MatrixFormGame.hpp"
#include "input.hpp"

LRESULT CALLBACK L3G_WndProc(HWND, UINT, WPARAM, LPARAM);

namespace {
static_assert(SYSEV_DEACTIVATING == 0 && SYSEV_ACTIVATED == 1);

struct RecordingForm : CForm {
    std::vector<ESysEvent> events;
    RecordingForm() { g_FormCur = this; }
    ~RecordingForm() { g_FormCur = nullptr; }
    void Enter() override {}
    void Leave() override {}
    void Draw() override { throw std::logic_error("Unexpected keep-alive rendering"); }
    void Takt(int) override {}
    void MouseMove(int, int) override {}
    void MouseKey(ButtonStatus, int, int, int) override {}
    void Keyboard(bool, uint8_t) override {}
    void SystemEvent(ESysEvent event) override { events.push_back(event); }
};

struct ActiveForm {
    CFormMatrixGame form;
    ActiveForm() { g_FormCur = &form; }
    ~ActiveForm() { g_FormCur = nullptr; }
};

void keepalive_dispatch() {
    const DWORD original = g_Flags;
    RecordingForm form;
    g_Flags = GFLAG_KEEPALIVE | GFLAG_APPACTIVE | GFLAG_4SPEED;
    const DWORD before = g_Flags;
    L3G_WndProc(nullptr, WM_ACTIVATEAPP, FALSE, 0);
    MG_CHECK((form.events == std::vector<ESysEvent>{SYSEV_INPUT_RESET}));
    MG_CHECK(g_Flags == before);
    L3G_WndProc(nullptr, WM_ACTIVATEAPP, TRUE, 0);
    MG_CHECK(form.events.size() == 1);
    MG_CHECK(g_Flags == before);
    g_Flags = original;
}

void keepalive_releases_keys() {
    tests::CommandWorld world;
    ActiveForm active;
    g_Flags = GFLAG_KEEPALIVE | GFLAG_APPACTIVE;
    world.map->m_Flags |= MMFLAG_VIDEO_RESOURCES_READY | MMFLAG_PAUSE;
    const DWORD mapFlags = world.map->m_Flags;
    const DWORD appFlags = g_Flags;
    for (unsigned key = 0; key < 256; ++key) Input::onKeyDown(static_cast<uint8_t>(key));
    world.map->m_VKeyDown = VK_W;
    L3G_WndProc(nullptr, WM_ACTIVATEAPP, FALSE, 0);
    for (unsigned key = 0; key < 256; ++key) MG_CHECK(!Input::isVKeyPressed(static_cast<uint8_t>(key)));
    MG_CHECK(world.map->m_VKeyDown == 0);
    MG_CHECK(world.map->m_Flags == mapFlags);
    MG_CHECK(g_Flags == appFlags);
    MG_CHECK(g_D3DD == nullptr);
}

void keepalive_fresh_input() {
    tests::CommandWorld world;
    ActiveForm active;
    g_Flags = GFLAG_KEEPALIVE | GFLAG_APPACTIVE;
    Input::onKeyDown(VK_W);
    L3G_WndProc(nullptr, WM_ACTIVATEAPP, FALSE, 0);
    MG_CHECK(!Input::isVKeyPressed(VK_W));
    L3G_WndProc(nullptr, WM_ACTIVATEAPP, FALSE, 0);
    Input::onKeyDown(VK_W);
    MG_CHECK(Input::isVKeyPressed(VK_W));
    L3G_WndProc(nullptr, WM_ACTIVATEAPP, TRUE, 0);
    MG_CHECK(Input::isVKeyPressed(VK_W));
    Input::onKeyUp(VK_W);
    MG_CHECK(!Input::isVKeyPressed(VK_W));
}

void ordinary_deactivation() {
    tests::CommandWorld world;
    ActiveForm active;
    Input::onKeyDown(VK_W);
    world.map->m_VKeyDown = VK_W;
    active.form.SystemEvent(SYSEV_DEACTIVATING);
    MG_CHECK(!Input::isVKeyPressed(VK_W));
    MG_CHECK(world.map->m_VKeyDown == 0);
}

constexpr tests::Case cases[] = {
    {"game.input.keepalive_dispatch", keepalive_dispatch},
    {"game.input.keepalive_keys", keepalive_releases_keys},
    {"game.input.keepalive_fresh", keepalive_fresh_input},
    {"game.input.ordinary_deactivation", ordinary_deactivation},
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
        std::cerr << "Keep-alive fixture leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
