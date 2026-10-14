// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "audio_fixture.hpp"
#include "StandaloneRuntime.hpp"
#include "SoundBridge.hpp"
#include "MatrixConfig.hpp"
#include "MatrixGameDll.hpp"
#include "SettingsWindow.hpp"
#include "ShadowPasses.hpp"
#include "WindowDisplay.hpp"
#include "DelayedDamage.hpp"
#include "Effects/MatrixEffectWeapon.hpp"
#include "GroupCounts.hpp"

#include <utility>

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
    values.delayed_damage = StandaloneSettings::DamageAttribution::Original;
    values.group_counts = StandaloneSettings::GroupCountMode::Original;
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
    MG_CHECK(!DelayedDamage::original());
    MG_CHECK(g_Config.m_ShowStencilShadows && g_Config.m_ShowProjShadows);
    MG_CHECK(!GroupCounts::original());

    g_Config.m_SkyBox = 2;
    g_Config.m_RobotShadow = SHADOW_STENCIL;
    StandaloneSettings::apply_runtime(custom());
    MG_CHECK(g_Config.m_CamMoveSpeed == 4.0f && device->voices[0]->volume == 0.3f);
    MG_CHECK(!g_Config.m_ShowStencilShadows && !g_Config.m_ShowProjShadows);
    MG_CHECK(DelayedDamage::original());  // Delayed-damage attribution applies immediately.
    MG_CHECK(GroupCounts::original());  // The group-count mode applies immediately.
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
    StandaloneSettings::initialize_runtime(Values{});
    MG_CHECK(!DelayedDamage::original());
    MG_CHECK(!GroupCounts::original());
    SoundBridge::shutdown();
    SoundBridge::master_volume(1.0f);
}

void restart_detection() {
    StandaloneSettings::initialize_runtime(Values{});
    auto values = Values{};
    for (auto change : {+[](Values &v) { v.sound_volume = 10; }, +[](Values &v) { v.camera_speed = 50; },
                        +[](Values &v) { v.stencil_shadows = false; }, +[](Values &v) { v.projected_shadows = false; },
                        +[](Values &v) { v.display.mode = Display::Mode::Borderless; },
                        +[](Values &v) { v.display.fullscreen = Display::Mode::Exclusive; },
                        +[](Values &v) { v.delayed_damage = StandaloneSettings::DamageAttribution::Original; },
                        +[](Values &v) { v.group_counts = StandaloneSettings::GroupCountMode::Original; }}) {
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

void independent_shadows() {
    // Each shadow kind draws only while enabled, and either one needs the stencil pass that
    // darkens it. The sequence also changes options after both kinds have been drawn.
    StandaloneSettings::initialize_runtime(Values{});
    const auto initial = shadow_passes(g_Config);
    MG_CHECK(initial.volumes && initial.projected && initial.stencil_pass());
    const std::pair<bool, bool> combinations[] = {
            {false, true}, {true, false}, {false, false}, {true, true}, {false, true}};
    for (const auto &[stencil, projected] : combinations) {
        auto values = Values{};
        values.stencil_shadows = stencil;
        values.projected_shadows = projected;
        StandaloneSettings::apply_runtime(values);
        const auto passes = shadow_passes(g_Config);
        MG_CHECK(passes.volumes == stencil && passes.projected == projected);
        MG_CHECK(passes.stencil_pass() == (stencil || projected));
    }
    SoundBridge::master_volume(1.0f);
}

// Pulses keep the stored side unless the original attribution is selected; other hits clear it.
void delayed_damage_attribution() {
    for (bool original : {false, true}) {
        DelayedDamage::set_original(original);
        MG_CHECK(DelayedDamage::original() == original);
        for (EWeapon pulse : {WEAPON_ABLAZE, WEAPON_SHORTED})
            MG_CHECK(DelayedDamage::kept_side(pulse, 2) == (original ? 0 : 2));
        for (EWeapon other : {WEAPON_GUN, WEAPON_LASER, WEAPON_BOMB, WEAPON_REPAIR})
            MG_CHECK(DelayedDamage::kept_side(other, 2) == 0);
    }
    DelayedDamage::set_original(DelayedDamage::original_by_default());
}

// A process starts corrected in the standalone EXE and original in the DLL, whose host owns its settings.
void delayed_damage_build_default() {
    MG_CHECK(DelayedDamage::original_by_default() == bool(MATRIXGAME_TESTS_BUILD_DLL));
    MG_CHECK(DelayedDamage::original() == DelayedDamage::original_by_default());
}

// A process starts corrected in the standalone EXE and original in the DLL, whose host owns its settings.
void group_counts_build_default() {
    MG_CHECK(GroupCounts::original_by_default() == bool(MATRIXGAME_TESTS_BUILD_DLL));
    MG_CHECK(GroupCounts::original() == GroupCounts::original_by_default());
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
    {"game.settings.independent_shadows", independent_shadows},
    {"game.settings.window_availability", window_availability},
    {"game.settings.delayed_damage_build_default", delayed_damage_build_default},
    {"game.settings.delayed_damage_attribution", delayed_damage_attribution},
    {"game.settings.group_counts_build_default", group_counts_build_default},
};

}  // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
