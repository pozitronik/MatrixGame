// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "audio_fixture.hpp"
#include "StandaloneRuntime.hpp"
#include "SoundBridge.hpp"
#include "MatrixConfig.hpp"
#include "MatrixGameDll.hpp"
#include "SettingsWindow.hpp"
#include "WindowDisplay.hpp"

namespace {

using StandaloneSettings::Skybox;
using StandaloneSettings::Values;

Values custom() {
    Values values;
    values.stencil_shadows = false;
    values.projected_shadows = false;
    values.skybox = Skybox::Low;
    values.robot_shadows = false;
    values.sound_volume = 30;
    values.camera_speed = 200;
    return values;
}

void startup_settings() {
    auto settings = SRobotsSettings::generate_default_settings();
    StandaloneSettings::apply_startup(custom(), settings);
    MG_CHECK(!settings.m_ShowStencilShadows && !settings.m_ShowProjShadows);
    MG_CHECK(settings.m_SkyBox == 1 && settings.m_RobotShadow == 0);
    StandaloneSettings::apply_startup(Values{}, settings);
    MG_CHECK(settings.m_ShowStencilShadows && settings.m_ShowProjShadows);
    MG_CHECK(settings.m_SkyBox == 2 && settings.m_RobotShadow == 1);
    Values off;
    off.skybox = Skybox::Off;
    StandaloneSettings::apply_startup(off, settings);
    MG_CHECK(settings.m_SkyBox == 0);
    g_Config.ApplySettings(&settings);
    MG_CHECK(g_Config.m_SkyBox == 0 && g_Config.m_RobotShadow == SHADOW_STENCIL);
}

void immediate_settings() {
    auto device = std::make_shared<audio_fixture::DeviceState>();
    SoundBridge::install(std::make_unique<Audio::Service>(std::make_unique<audio_fixture::FakeDevice>(device),
        Audio::Mapping{{L"Sound.A", L"a.wav"}}, [](const std::wstring &) { return audio_fixture::wave(); },
        [](const std::wstring &, const std::string &) {}));
    wchar_t name[] = L"Sound.A";
    MG_CHECK(SoundBridge::create(name, 0, 0) != 0);

    g_Config.m_CamMoveSpeed = 2.0f;  // Value read from the game data.
    StandaloneSettings::initialize_runtime(Values{});
    MG_CHECK(g_Config.m_CamMoveSpeed == 2.0f && device->voices[0]->volume == 1.0f);
    MG_CHECK(g_Config.m_ShowStencilShadows && g_Config.m_ShowProjShadows);

    g_Config.m_SkyBox = 2;
    g_Config.m_RobotShadow = SHADOW_STENCIL;
    StandaloneSettings::apply_runtime(custom());
    MG_CHECK(g_Config.m_CamMoveSpeed == 4.0f && device->voices[0]->volume == 0.3f);
    MG_CHECK(!g_Config.m_ShowStencilShadows && !g_Config.m_ShowProjShadows);
    MG_CHECK(StandaloneSettings::current() == custom());
    // Restart-only options do not change the running engine.
    MG_CHECK(g_Config.m_SkyBox == 2 && g_Config.m_RobotShadow == SHADOW_STENCIL);

    auto slower = custom();
    slower.camera_speed = 50;
    StandaloneSettings::apply_runtime(slower);
    MG_CHECK(g_Config.m_CamMoveSpeed == 1.0f);  // Scaled from the data value, not compounded.

    g_Config.m_CamMoveSpeed = 3.0f;  // A new session reads the data value again.
    StandaloneSettings::initialize_runtime(slower);
    MG_CHECK(g_Config.m_CamMoveSpeed == 1.5f);
    SoundBridge::shutdown();
    SoundBridge::master_volume(1.0f);
}

void restart_detection() {
    StandaloneSettings::initialize_runtime(Values{});
    auto values = Values{};
    for (auto change : {+[](Values &v) { v.sound_volume = 10; }, +[](Values &v) { v.camera_speed = 50; },
                        +[](Values &v) { v.stencil_shadows = false; }, +[](Values &v) { v.projected_shadows = false; },
                        +[](Values &v) { v.display.mode = Display::Mode::Borderless; },
                        +[](Values &v) { v.display.fullscreen = Display::Mode::Exclusive; }}) {
        change(values);
        MG_CHECK(!StandaloneSettings::restart_pending(values));
    }
    for (auto change : {+[](Values &v) { v.display.width = 800; }, +[](Values &v) { v.display.height = 600; },
                        +[](Values &v) { v.skybox = Skybox::Off; }, +[](Values &v) { v.robot_shadows = false; },
                        +[](Values &v) { v.random = random::Mode::LegacyCRT; }}) {
        auto restart = Values{};
        change(restart);
        MG_CHECK(StandaloneSettings::restart_pending(restart));
    }
    SoundBridge::master_volume(1.0f);
}

void window_availability() {
    const std::wstring disabled = L"x|_COLOR:FF6794A3|_POS:6:105|_TEXT:[setup]|y";
    auto menu = disabled;
    SettingsWindow::customize_menu(menu);
    MG_CHECK(!SettingsWindow::available() && menu == disabled);  // A DLL host owns its settings.

    Display::initialize({});  // The standalone EXE manages its window.
    MG_CHECK(SettingsWindow::available());
    SettingsWindow::customize_menu(menu);
    MG_CHECK(menu == L"x|_COLOR:FFADA793|_POS:6:105|_TEXT:[setup]|y");
    Display::shutdown();
    MG_CHECK(!SettingsWindow::available());
}

constexpr tests::Case cases[] = {
    {"game.settings.startup_settings", startup_settings},
    {"game.settings.immediate_settings", immediate_settings},
    {"game.settings.restart_detection", restart_detection},
    {"game.settings.window_availability", window_availability},
};

}  // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
