// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "DisplaySettings.hpp"
#include "CBlockPar.hpp"
#include "stupid_logger.hpp"

#include <filesystem>
#include <vector>

logger_type lgr{std::cerr};

namespace {
void defaults() {
    Base::CBlockPar options;
    options.ParAdd(L"RandomGenerator", L"LegacyCRT");
    const auto result = Display::resolve(options);
    MG_CHECK(result.valid);
    MG_CHECK(result.settings.width == 1024 && result.settings.height == 768);
    MG_CHECK(result.settings.mode == Display::Mode::Windowed);
    MG_CHECK(result.settings.fullscreen == Display::Mode::Borderless);
}

void explicit_settings() {
    Base::CBlockPar options;
    options.ParAdd(L"ResolutionWidth", L"1920");
    options.ParAdd(L"ResolutionHeight", L"1080");
    options.ParAdd(L"WindowMode", L"  borderless\t");
    options.ParAdd(L"FullscreenMode", L"Exclusive");
    auto result = Display::resolve(options);
    MG_CHECK(result.valid && result.settings.width == 1920 && result.settings.height == 1080);
    MG_CHECK(result.settings.mode == Display::Mode::Borderless);
    MG_CHECK(result.settings.fullscreen == Display::Mode::Borderless);
    options.ParSetAdd(L"WindowMode", L"Windowed");
    result = Display::resolve(options);
    MG_CHECK(result.valid && result.settings.fullscreen == Display::Mode::Exclusive);
    options.ParSetAdd(L"WindowMode", L"EXCLUSIVE");
    result = Display::resolve(options);
    MG_CHECK(result.valid && result.settings.mode == Display::Mode::Exclusive);
    MG_CHECK(result.settings.fullscreen == Display::Mode::Exclusive);
}

void invalid_settings() {
    for (const auto *value : {L"0", L"-1", L"639", L"8193", L"1920oops", L"1920.5", L"99999999999999", L""}) {
        Base::CBlockPar options;
        options.ParAdd(L"ResolutionWidth", value);
        options.ParAdd(L"WindowMode", L"Exclusive");
        const auto result = Display::resolve(options);
        MG_CHECK(!result.valid);
        MG_CHECK(result.settings.width == 1024 && result.settings.mode == Display::Mode::Windowed);
    }
    Base::CBlockPar options;
    options.ParAdd(L"ResolutionHeight", L"479");
    MG_CHECK(!Display::resolve(options).valid);
    options.ParSetAdd(L"ResolutionHeight", L"720");
    MG_CHECK(Display::resolve(options).valid);
    options.ParAdd(L"ResolutionHeight", L"1080");
    MG_CHECK(!Display::resolve(options).valid);
    Base::CBlockPar modes;
    modes.ParAdd(L"FullscreenMode", L"Windowed");
    MG_CHECK(!Display::resolve(modes).valid);
    modes.ParSetAdd(L"FullscreenMode", L"Exclusive");
    modes.ParAdd(L"WindowMode", L"unknown");
    MG_CHECK(!Display::resolve(modes).valid);
    modes.ParSetAdd(L"WindowMode", L"Windowed");
    modes.ParAdd(L"WindowMode", L"Borderless");
    MG_CHECK(!Display::resolve(modes).valid);
}

void configuration_files() {
    struct Fixture {
        std::filesystem::path path = std::filesystem::current_path() /
            ("display-options-" + std::to_string(GetCurrentProcessId()) + ".txt");
        ~Fixture() { std::error_code error; std::filesystem::remove(path, error); }
    } fixture;
    MG_CHECK(!std::filesystem::exists(fixture.path));
    MG_CHECK(Display::load(fixture.path.c_str()).valid);
    Base::CBuf bytes;
    bytes.StrNZ("ResolutionWidth=1280\nResolutionHeight=720\nWindowMode=Borderless\n");
    bytes.SaveInFile(fixture.path.native());
    auto result = Display::load(fixture.path.c_str());
    MG_CHECK(result.valid && result.settings.width == 1280 && result.settings.height == 720);
    MG_CHECK(result.settings.mode == Display::Mode::Borderless);
    bytes.Clear();
    bytes.Add<uint16_t>(0xfeff);
    bytes.WStrNZ(L"WindowMode=Exclusive\r\n");
    bytes.SaveInFile(fixture.path.native());
    MG_CHECK(Display::load(fixture.path.c_str()).settings.mode == Display::Mode::Exclusive);
    bytes.Clear();
    bytes.Add<uint8_t>(0xc3);
    bytes.Add<uint8_t>(0x28);
    bytes.SaveInFile(fixture.path.native());
    MG_CHECK(!Display::load(fixture.path.c_str()).valid);
}

void toggle_pairs() {
    using Display::Mode;
    MG_CHECK(Display::toggle_target(Mode::Windowed, Mode::Borderless) == Mode::Borderless);
    MG_CHECK(Display::toggle_target(Mode::Borderless, Mode::Borderless) == Mode::Windowed);
    MG_CHECK(Display::toggle_target(Mode::Windowed, Mode::Exclusive) == Mode::Exclusive);
    MG_CHECK(Display::toggle_target(Mode::Exclusive, Mode::Exclusive) == Mode::Windowed);
}

void shortcut_consumes_only_alt_enter() {
    Display::Shortcut shortcut;
    bool request = false;
    constexpr LPARAM alt = 1u << 29;
    constexpr LPARAM repeat = 1u << 30;
    MG_CHECK(!shortcut.handle(WM_KEYDOWN, VK_RETURN, 0, request) && !request);
    MG_CHECK(!shortcut.handle(WM_KEYUP, VK_RETURN, 0, request));
    MG_CHECK(!shortcut.handle(WM_SYSKEYDOWN, VK_F4, alt, request));
    MG_CHECK(shortcut.handle(WM_SYSKEYDOWN, VK_RETURN, alt, request) && request);
    for (int i = 0; i < 20; ++i) {
        MG_CHECK(shortcut.handle(WM_SYSKEYDOWN, VK_RETURN, alt | repeat, request) && !request);
    }
    MG_CHECK(shortcut.handle(WM_KEYDOWN, VK_RETURN, repeat, request) && !request);
    MG_CHECK(shortcut.handle(WM_KEYUP, VK_RETURN, 0, request) && !request);
    MG_CHECK(!shortcut.handle(WM_KEYDOWN, VK_RETURN, 0, request));
    MG_CHECK(shortcut.handle(WM_SYSKEYDOWN, VK_RETURN, alt, request) && request);
    shortcut = {}; // Focus loss clears the latch; a held-key repeat must not request a new switch.
    MG_CHECK(shortcut.handle(WM_SYSKEYDOWN, VK_RETURN, alt | repeat, request) && !request);
    MG_CHECK(shortcut.handle(WM_SYSKEYUP, VK_RETURN, alt, request));
}

void scaled_mouse_coordinates() {
    for (const auto extent : {720, 768, 1080, 2160}) {
        MG_CHECK(Display::map_coordinate(0, extent, 768) == 0);
        MG_CHECK(Display::map_coordinate(extent - 1, extent, 768) == 767);
        MG_CHECK(Display::map_coordinate(-1, extent, 768) < 0);
        MG_CHECK(Display::map_coordinate(extent, extent, 768) >= 768);
        MG_CHECK(Display::map_coordinate(extent / 2, extent, 768) >= 383);
        MG_CHECK(Display::map_coordinate(extent / 2, extent, 768) <= 384);
    }
    MG_CHECK(Display::map_coordinate(25, 0, 768) == 0);
    MG_CHECK(Display::map_coordinate(25, 1, 768) == 0);
    MG_CHECK(Display::map_coordinate(32767, 32768, 8192) == 8191);
}

void reset_preserves_parameters() {
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth = 1280;
    parameters.BackBufferHeight = 720;
    parameters.BackBufferCount = 1;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    for (HRESULT result : {D3D_OK, D3DERR_DEVICELOST, D3DERR_INVALIDCALL}) {
        MG_CHECK(Display::reset_copy(parameters, [&](D3DPRESENT_PARAMETERS *attempt) {
            MG_CHECK(attempt->BackBufferWidth == 1280 && attempt->BackBufferHeight == 720);
            *attempt = {};
            return result;
        }) == result);
        MG_CHECK(parameters.BackBufferWidth == 1280 && parameters.BackBufferHeight == 720);
        MG_CHECK(parameters.BackBufferCount == 1 && parameters.BackBufferFormat == D3DFMT_X8R8G8B8);
    }
}

void transition_order() {
    std::vector<char> events;
    auto release = [&] { events.push_back('L'); };
    auto restore = [&] { events.push_back('C'); };
    auto rollback = [&](HRESULT result) { MG_CHECK(result == D3DERR_INVALIDCALL); events.push_back('B'); return D3D_OK; };
    auto change = [&] { events.push_back('R'); return D3D_OK; };
    MG_CHECK(Display::transition(true, release, change, rollback, restore) == D3D_OK);
    MG_CHECK((events == std::vector<char>{'L', 'R', 'C'}));
    events.clear();
    MG_CHECK(Display::transition(false, release, change, rollback, restore) == D3D_OK);
    MG_CHECK((events == std::vector<char>{'R', 'C'}));
    events.clear();
    MG_CHECK(Display::transition(true, release, [&] { events.push_back('R'); return D3DERR_INVALIDCALL; }, rollback, restore) == D3D_OK);
    MG_CHECK((events == std::vector<char>{'L', 'R', 'B', 'C'}));
}

void transition_failure() {
    int released = 0, restored = 0, rolled_back = 0;
    auto release = [&] { ++released; };
    auto restore = [&] { ++restored; };
    auto rollback = [&](HRESULT) { ++rolled_back; return D3D_OK; };
    MG_CHECK(Display::transition(true, release, [] { return D3DERR_DEVICELOST; }, rollback, restore) == D3DERR_DEVICELOST);
    MG_CHECK(released == 1 && restored == 0 && rolled_back == 0);
    for (HRESULT result : {D3DERR_DEVICELOST, D3DERR_DRIVERINTERNALERROR}) {
        MG_CHECK(Display::transition(false, release, [] { return D3DERR_INVALIDCALL; },
            [&](HRESULT) { ++rolled_back; return result; }, restore) == result);
    }
    MG_CHECK(released == 1 && restored == 0 && rolled_back == 2);
}

constexpr tests::Case cases[] = {
    {"game.display.defaults", defaults},
    {"game.display.options", explicit_settings},
    {"game.display.invalid_options", invalid_settings},
    {"game.display.files", configuration_files},
    {"game.display.toggle_pairs", toggle_pairs},
    {"game.display.shortcut", shortcut_consumes_only_alt_enter},
    {"game.display.coordinates", scaled_mouse_coordinates},
    {"game.display.reset_parameters", reset_preserves_parameters},
    {"game.display.transition_order", transition_order},
    {"game.display.transition_failure", transition_failure},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    const int result = tests::run(argc, argv, cases);
    Base::CMain::BaseDeInit();
    return result;
}
