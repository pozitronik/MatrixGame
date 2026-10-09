// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"

#include "DeviceRecovery.hpp"
#include "CMain.hpp"
#include "stupid_logger.hpp"

#include <vector>

logger_type lgr{std::cerr};

namespace {
struct RecoveryFixture {
    bool ready = true;
    HRESULT resetResult = D3D_OK;
    std::vector<char> events;
    int waits = 0;

    bool poll(HRESULT status) {
        return Rendering::RecoverDevice(status, ready,
            [this] { events.push_back('L'); ready = false; },
            [this] { events.push_back('R'); MG_CHECK(!ready); return resetResult; },
            [this] { events.push_back('C'); ready = true; },
            [this](std::chrono::milliseconds delay) {
                MG_CHECK(delay == std::chrono::milliseconds(50));
                MG_CHECK(!ready);
                events.push_back('W');
                ++waits;
            });
    }
};

void operational_device() {
    RecoveryFixture fixture;
    MG_CHECK(!fixture.poll(D3D_OK));
    MG_CHECK(fixture.events.empty());
    fixture.ready = false;
    MG_CHECK(!fixture.poll(D3D_OK));
    MG_CHECK(fixture.ready);
    MG_CHECK((fixture.events == std::vector<char>{'C'}));
}

void lost_device_waits_and_releases_once() {
    RecoveryFixture fixture;
    MG_CHECK(fixture.poll(D3DERR_DEVICELOST));
    MG_CHECK(fixture.poll(D3DERR_DEVICELOST));
    MG_CHECK(!fixture.ready);
    MG_CHECK(fixture.waits == 2);
    MG_CHECK((fixture.events == std::vector<char>{'L', 'W', 'W'}));
    MG_CHECK(!fixture.poll(D3D_OK));
    MG_CHECK(fixture.ready);
    MG_CHECK(fixture.events.back() == 'C');
}

void reset_restores_in_order() {
    RecoveryFixture fixture;
    for (int cycle = 0; cycle < 3; ++cycle) {
        fixture.events.clear();
        MG_CHECK(!fixture.poll(D3DERR_DEVICENOTRESET));
        MG_CHECK(fixture.ready);
        MG_CHECK((fixture.events == std::vector<char>{'L', 'R', 'C'}));
    }
    MG_CHECK(fixture.waits == 0);
}

void transient_reset_failure() {
    RecoveryFixture fixture;
    fixture.resetResult = D3DERR_DEVICELOST;
    MG_CHECK(fixture.poll(D3DERR_DEVICENOTRESET));
    MG_CHECK(!fixture.ready);
    MG_CHECK((fixture.events == std::vector<char>{'L', 'R', 'W'}));
    fixture.events.clear();
    fixture.resetResult = D3D_OK;
    MG_CHECK(!fixture.poll(D3DERR_DEVICENOTRESET));
    MG_CHECK((fixture.events == std::vector<char>{'R', 'C'}));
}

void permanent_errors_are_diagnosed() {
    RecoveryFixture fixture;
    bool caught = false;
    try { fixture.poll(D3DERR_DRIVERINTERNALERROR); }
    catch (const Base::CException &error) {
        const auto message = error.Info();
        caught = message.find(L"TestCooperativeLevel") != message.npos && message.find(L"88760827") != message.npos;
    }
    MG_CHECK(caught);
    MG_CHECK(fixture.events.empty());
    fixture.resetResult = D3DERR_INVALIDCALL;
    caught = false;
    try { fixture.poll(D3DERR_DEVICENOTRESET); }
    catch (const Base::CException &error) {
        const auto message = error.Info();
        caught = message.find(L"Reset") != message.npos && message.find(L"8876086C") != message.npos;
    }
    MG_CHECK(caught);
    MG_CHECK(!fixture.ready);
    MG_CHECK(fixture.waits == 0);
    MG_CHECK((fixture.events == std::vector<char>{'L', 'R'}));
}

void present_loss_is_recoverable() {
    Rendering::CheckPresentResult(D3D_OK);
    Rendering::CheckPresentResult(D3DERR_DEVICELOST);
    bool caught = false;
    try { Rendering::CheckPresentResult(D3DERR_DRIVERINTERNALERROR); }
    catch (const Base::CException &error) {
        caught = error.Info().find(L"Present") != std::wstring::npos;
    }
    MG_CHECK(caught);
}

constexpr tests::Case cases[] = {
    {"game.device.operational", operational_device},
    {"game.device.lost_wait", lost_device_waits_and_releases_once},
    {"game.device.reset_order", reset_restores_in_order},
    {"game.device.transient_reset", transient_reset_failure},
    {"game.device.permanent_error", permanent_errors_are_diagnosed},
    {"game.device.present_loss", present_loss_is_recoverable},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    const int result = tests::run(argc, argv, cases);
    Base::CMain::BaseDeInit();
    return result;
}
