// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"
#include "battle_device.hpp"
#include "diagnostic_capture.hpp"

#include "cheats.hpp"
#include "input.hpp"

namespace {
bool type(std::string_view text) {
    bool matched = false;
    for (size_t index = 0; index < text.size(); ++index) {
        const uint8_t key = text[index] == '~' ? VK_TILDA : static_cast<uint8_t>(text[index]);
        matched = Cheats::processInput(key);
        MG_CHECK(!matched || index + 1 == text.size());
    }
    return matched;
}

void suffix_matching() {
    tests::CommandWorld world;
    MG_CHECK(!type("ZZZZZZZZZZZZZZZZZZZZ"));
    MG_CHECK(!type("AUT"));
    MG_CHECK(!Cheats::processInput(VK_F1));
    MG_CHECK(!type("O"));
    const DWORD before = world.map->m_Flags;
    MG_CHECK(type("AUTO"));
    MG_CHECK(world.map->m_Flags == (before ^ MMFLAG_AUTOMATIC_MODE));
    MG_CHECK(type("AUTO"));
    MG_CHECK(world.map->m_Flags == before);
    MG_CHECK(type("FLYCAM"));
    MG_CHECK(FLAG(world.map->m_Flags, MMFLAG_FLYCAM));
}

void developer_flag_toggles() {
    tests::CommandWorld world;
    MG_CHECK(type("NEED4SPEED"));
    MG_CHECK(FLAG(g_Flags, GFLAG_4SPEED));
    MG_CHECK(type("NEED4SPEED"));
    MG_CHECK(!FLAG(g_Flags, GFLAG_4SPEED));
    MG_CHECK(type("KEEPALIVE"));
    MG_CHECK(FLAG(g_Flags, GFLAG_KEEPALIVE));
    MG_CHECK(type("KEEPALIVE"));
    MG_CHECK(!FLAG(g_Flags, GFLAG_KEEPALIVE));
    g_Config.m_DIFlags = 0;
    MG_CHECK(type("IAMTESTER"));
    MG_CHECK(FLAG(g_Flags, GFLAG_4SPEED));
    MG_CHECK(FLAG(g_Flags, GFLAG_KEEPALIVE));
    MG_CHECK(FLAG(g_Config.m_DIFlags, DI_SIDEINFO));
    MG_CHECK(FLAG(g_Config.m_DIFlags, DI_DRAWFPS));
    MG_CHECK(FLAG(world.map->m_Flags, MMFLAG_AUTOMATIC_MODE));
    MG_CHECK(g_D3DD == nullptr);
}

void resource_limit() {
    tests::CommandWorld world;
    for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
        world.map->GetPlayerSide()->SetResourceAmount(static_cast<ERes>(resource), resource * 100);
    }
    MG_CHECK(type("RICHIERICH"));
    MG_CHECK(type("RICHIERICH"));
    for (int resource = 0; resource < MAX_RESOURCES; ++resource) {
        MG_CHECK(world.map->GetPlayerSide()->GetResourcesAmount(static_cast<ERes>(resource)) == 9000);
    }
}

void console_activation() {
    tests::CommandWorld world;
    MG_CHECK(!world.map->m_Console.IsActive());
    MG_CHECK(type("DEVCON"));
    MG_CHECK(world.map->m_Console.IsActive());
    world.map->m_Console.SetActive(false);
    MG_CHECK(type("~"));
    MG_CHECK(world.map->m_Console.IsActive());
    world.map->m_Console.SetActive(false);
    MG_CHECK(type("SHOWFPS"));
    MG_CHECK(world.map->m_Console.IsActive());
}

void diagnostic_registry() {
    tests::CommandWorld world;
    const DWORD mask = DI_TMEM | DI_TARGETCOORD | DI_VISOBJ | DI_ACTIVESOUNDS | DI_FRUSTUMCENTER;
    g_Config.m_DIFlags = DI_SIDEINFO;
    MG_CHECK(!type("INFO"));
    MG_CHECK(g_Config.m_DIFlags == DI_SIDEINFO);
    MG_CHECK(type(" ") && g_Config.m_DIFlags == (DI_SIDEINFO | mask));
    MG_CHECK(type("INFO ") && g_Config.m_DIFlags == DI_SIDEINFO);
    tests::DiagnosticCapture capture;
    bool rejected_nested_capture = false;
    try { tests::DiagnosticCapture nested; }
    catch (const std::logic_error &) { rejected_nested_capture = true; }
    MG_CHECK(rejected_nested_capture);
    MG_CHECK(!type("BUBUBU") && capture.events.empty());
    MG_CHECK(type(" ") && !capture.events.empty());
    MG_CHECK(capture.events.front().item.key == L"3D Robots game information....");
    MG_CHECK(capture.events.front().item.val.empty() && capture.events.front().item.ttl == 3000);
    MG_CHECK(capture.events.front().item.bttl == 0);
    int previous_delay = -1;
    bool later = false, closing = false;
    for (const auto &event : capture.events) {
        MG_CHECK(event.append && event.item.ttl > 0 && event.item.bttl >= previous_delay);
        later |= event.item.bttl > 0;
        closing |= event.item.key == L"Thats all folks :)";
        previous_delay = event.item.bttl;
    }
    MG_CHECK(later && closing);
    const auto flags = g_Config.m_DIFlags;
    world.map->m_Console.SetActive(false);
    MG_CHECK(type("SHOWFPS") && world.map->m_Console.IsActive());
    MG_CHECK(g_Config.m_DIFlags == flags); // This alias opens the console; it does not toggle FPS.
}

void video_registry() {
    for (const auto &[format, label] : {std::pair{D3DFMT_X8R8G8B8, L"X8R8G8B8"},
        {D3DFMT_A8R8G8B8, L"A8R8G8B8"}, {D3DFMT_R8G8B8, L"R8G8B8"},
        {D3DFMT_R5G6B5, L"R5G6B5"}, {D3DFMT_A1R5G5B5, L"25"}}) for (bool stencil : {false, true}) {
        tests::CommandWorld world;
        tests::BattleDevice device;
        const D3DDISPLAYMODE mode{1280, 720, 60, format};
        device.display_mode(mode);
        struct CapsGuard {
            D3DCAPS9 saved = g_D3DDCaps;
            ~CapsGuard() { g_D3DDCaps = saved; }
        } caps_guard;
        g_D3DDCaps.MaxSimultaneousTextures = 7;
        INITFLAG(g_Flags, GFLAG_STENCILAVAILABLE, stencil);
        tests::DiagnosticCapture capture;
        MG_CHECK(type("VIDEO"));
        MG_CHECK(device.stats().display_mode_queries == 2 && capture.events.size() == 5);
        MG_CHECK(capture.events[1].item.key == L"Sim textures" && capture.events[1].item.val == L"7");
        MG_CHECK(capture.events[2].item.key == L"Stencil available" && capture.events[2].item.val == (stencil ? L"Yes" : L"No"));
        for (int i = 0; i < 2; ++i) {
            const auto &event = capture.events[3 + i];
            MG_CHECK(event.item.key == L"Buffer " + std::to_wstring(i) + L" mode" && event.item.val == label);
        }
        for (const auto &event : capture.events)
            MG_CHECK(event.item.ttl == 10000 && event.item.bttl == 0 && !event.append);
        D3DDISPLAYMODE observed{};
        MG_CHECK(device.get()->GetDisplayMode(1, &observed) == D3DERR_INVALIDCALL);
        MG_CHECK(device.get()->GetDisplayMode(0, nullptr) == D3DERR_INVALIDCALL);
        MG_CHECK(device.get()->GetDisplayMode(0, &observed) == D3D_OK);
        MG_CHECK(observed.Width == 1280 && observed.Height == 720 && observed.RefreshRate == 60 && observed.Format == format);
    }
}

struct MaintenanceInput : CMatrixMap {
    static void percentage(CMatrixMap &map, int percent) { map.*&MaintenanceInput::m_MaintenancePRC = percent; }
};
void hurry_boundaries() {
    for (int percent : {0, 100}) for (int remaining : {-1, 0, 1, 1234}) {
        tests::CommandWorld world;
        MaintenanceInput::percentage(*world.map, percent);
        world.map->SetMaintenanceTime(remaining);
        const auto flags = world.map->m_Flags;
        MG_CHECK(type("HURRY"));
        MG_CHECK(world.map->MaintenanceDisabled() == (percent == 0));
        MG_CHECK(world.map->BeforeMaintenanceTime() == (percent && remaining > 0 ? 1 : remaining));
        MG_CHECK(world.map->m_Flags == flags && world.map->MaintenanceDisabled() == (percent == 0));
    }
}

constexpr tests::Case cases[] = {
    {"game.cheats.diagnostic_registry", diagnostic_registry},
    {"game.cheats.video_registry", video_registry},
    {"game.cheats.hurry_boundaries", hurry_boundaries},
    {"game.cheats.suffix_matching", suffix_matching},
    {"game.cheats.flag_toggles", developer_flag_toggles},
    {"game.cheats.resource_limit", resource_limit},
    {"game.cheats.console", console_activation},
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
        std::cerr << "Cheat fixture leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
