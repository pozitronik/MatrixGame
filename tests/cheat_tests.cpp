// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"

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

constexpr tests::Case cases[] = {
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
