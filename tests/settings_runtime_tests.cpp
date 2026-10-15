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
#include "PlaceContainment.hpp"
#include "ZeroXTrace.hpp"
#include "MapSelection.hpp"
#include "MapWindow.hpp"
#include "CStorage.hpp"

#include <windows.h>

#include <filesystem>
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
    values.place_containment = StandaloneSettings::PlaceContainmentMode::Original;
    values.zero_x_trace = StandaloneSettings::ZeroXTraceMode::Original;
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
    MG_CHECK(!GroupCounts::original() && !PlaceContainment::original() && !ZeroXTrace::original());

    g_Config.m_SkyBox = 2;
    g_Config.m_RobotShadow = SHADOW_STENCIL;
    StandaloneSettings::apply_runtime(custom());
    MG_CHECK(g_Config.m_CamMoveSpeed == 4.0f && device->voices[0]->volume == 0.3f);
    MG_CHECK(!g_Config.m_ShowStencilShadows && !g_Config.m_ShowProjShadows);
    MG_CHECK(DelayedDamage::original());  // Delayed-damage attribution applies immediately.
    MG_CHECK(GroupCounts::original());  // The group-count mode applies immediately.
    MG_CHECK(PlaceContainment::original());
    MG_CHECK(ZeroXTrace::original());
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
    MG_CHECK(!GroupCounts::original() && !PlaceContainment::original() && !ZeroXTrace::original());
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
                        +[](Values &v) { v.display.width = 800; }, +[](Values &v) { v.display.height = 600; },
                        +[](Values &v) { v.delayed_damage = StandaloneSettings::DamageAttribution::Original; },
                        +[](Values &v) { v.group_counts = StandaloneSettings::GroupCountMode::Original; },
                        +[](Values &v) { v.place_containment = StandaloneSettings::PlaceContainmentMode::Original; },
                        +[](Values &v) { v.zero_x_trace = StandaloneSettings::ZeroXTraceMode::Original; }}) {
        change(values);
        MG_CHECK(!StandaloneSettings::restart_pending(values));
    }
    for (auto change : {+[](Values &v) { v.skybox = Skybox::Off; }, +[](Values &v) { v.robot_shadows = false; },
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

// A process starts corrected in the standalone EXE and original in the DLL, whose host owns its settings.
void place_containment_build_default() {
    MG_CHECK(PlaceContainment::original_by_default() == bool(MATRIXGAME_TESTS_BUILD_DLL));
    MG_CHECK(PlaceContainment::original() == PlaceContainment::original_by_default());
}

void zero_x_trace_build_default() {
    MG_CHECK(ZeroXTrace::original_by_default() == bool(MATRIXGAME_TESTS_BUILD_DLL));
    MG_CHECK(ZeroXTrace::original() == ZeroXTrace::original_by_default());
}

void window_availability() {
    const std::wstring disabled = L"x|_COLOR:FF6794A3|_POS:6:105|_TEXT:[setup]|y";
    auto menu = disabled;
    SettingsWindow::customize_menu(menu);
    MG_CHECK(!SettingsWindow::available() && menu == disabled);  // A DLL host owns its settings.
    MG_CHECK(!MapWindow::available() && !MapWindow::open_after_battle());  // and its map selection.

    Display::initialize({});  // The standalone EXE manages its window.
    MG_CHECK(SettingsWindow::available() && MapWindow::available());
    SettingsWindow::customize_menu(menu);
    MG_CHECK(menu == L"x|_COLOR:FFADA793|_POS:6:105|_TEXT:[setup]|y");
    Display::shutdown();
    MG_CHECK(!SettingsWindow::available());
}

struct TemporaryDirectory {
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("map-choice-" + std::to_string(GetCurrentProcessId()));
    TemporaryDirectory() {
        MG_CHECK(std::filesystem::create_directory(root));
        std::filesystem::current_path(root);
    }
    ~TemporaryDirectory() {
        std::filesystem::current_path(previous);
        std::error_code error;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root, error))
            SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        std::filesystem::remove_all(root, error);
    }
};

void map_choice() {
    TemporaryDirectory directory;
    std::filesystem::create_directories("Matrix/Map");
    {
        Base::CStorageRecord properties(std::wstring(L"properties"));
        properties.AddItem(Base::CStorageRecordItem(L"Name", Base::ST_WCHAR));
        properties.AddItem(Base::CStorageRecordItem(L"Value", Base::ST_WCHAR));
        Base::CStorage map;
        map.AddRecord(properties);
        map.Save(L"Matrix\\Map\\Chosen.cmap");
    }
    Values none;
    MG_CHECK(MapSelection::startup_map(none).empty() && !MapSelection::remembered_map_started());
    MG_CHECK(!MapSelection::forget_remembered_map());

    // Choosing saves the other settings with the map and requests a new start.
    std::string error;
    MG_CHECK(!MapSelection::relaunch_requested());
    MG_CHECK(MapSelection::choose(custom(), L"Matrix\\Map\\Chosen.cmap", error) && error.empty());
    MG_CHECK(MapSelection::relaunch_requested());
    auto stored = StandaloneSettings::load();
    auto expected = custom();
    expected.map = L"Matrix\\Map\\Chosen.cmap";
    MG_CHECK(stored.invalid.empty() && stored.values == expected);

    // The next start uses the readable remembered map. A failure before the map loads keeps the choice.
    MG_CHECK(MapSelection::startup_map(stored.values) == expected.map && stored.values == expected);
    MG_CHECK(MapSelection::remembered_map_started() && !MapSelection::forget_remembered_map());
    MG_CHECK(StandaloneSettings::load().values == expected);
    // The map is cleared in the file while it loads, so a crash leaves the configured map for the next
    // start, and written back once initialization succeeds.
    MapSelection::begin_map_load();
    MG_CHECK(StandaloneSettings::load().values.map.empty());
    MapSelection::finish_map_load();
    MG_CHECK(StandaloneSettings::load().values == expected);
    // An unwritable file still names the map, so a failure is not reported as forgetting it.
    MG_CHECK(SetFileAttributesW(L"CFG\\standalone.txt", FILE_ATTRIBUTE_READONLY));
    MapSelection::begin_map_load();
    MG_CHECK(StandaloneSettings::load().values == expected);
    MG_CHECK(!MapSelection::forget_remembered_map());
    MG_CHECK(StandaloneSettings::load().values == expected);
    MG_CHECK(SetFileAttributesW(L"CFG\\standalone.txt", FILE_ATTRIBUTE_NORMAL));
    // A failure while the map loads keeps it forgotten.
    MapSelection::begin_map_load();
    MG_CHECK(MapSelection::forget_remembered_map() && !MapSelection::forget_remembered_map());
    stored = StandaloneSettings::load();
    MG_CHECK(stored.values.map.empty() && stored.values.sound_volume == custom().sound_volume);

    // A demo map plays once: it is not written back after it starts.
    MG_CHECK(MapSelection::is_demo(L"Matrix\\Map\\Demo2.cmap") && MapSelection::is_demo(L"x\\DEMO.cmap"));
    MG_CHECK(!MapSelection::is_demo(L"Matrix\\Map\\Chosen.cmap"));
    std::filesystem::copy_file(L"Matrix/Map/Chosen.cmap", L"Matrix/Map/Demo2.cmap");
    MG_CHECK(MapSelection::choose(custom(), L"Matrix\\Map\\Demo2.cmap", error));
    stored = StandaloneSettings::load();
    MG_CHECK(MapSelection::startup_map(stored.values) == L"Matrix\\Map\\Demo2.cmap");
    MapSelection::begin_map_load();
    MapSelection::finish_map_load();
    MG_CHECK(StandaloneSettings::load().values.map.empty());

    // A failed save is reported and leaves the file unchanged.
    MG_CHECK(SetFileAttributesW(L"CFG\\standalone.txt", FILE_ATTRIBUTE_READONLY));
    MG_CHECK(!MapSelection::choose(Values{}, L"Matrix\\Map\\Chosen.cmap", error) && !error.empty());
    MG_CHECK(StandaloneSettings::load().values.map.empty());
}

constexpr tests::Case cases[] = {
    {"game.settings.startup_settings", startup_settings},
    {"game.settings.immediate_settings", immediate_settings},
    {"game.settings.restart_detection", restart_detection},
    {"game.settings.independent_shadows", independent_shadows},
    {"game.settings.window_availability", window_availability},
    {"game.settings.delayed_damage_build_default", delayed_damage_build_default},
    {"game.settings.delayed_damage_attribution", delayed_damage_attribution},
    {"game.settings.map_choice", map_choice},
    {"game.settings.group_counts_build_default", group_counts_build_default},
    {"game.settings.place_containment_build_default", place_containment_build_default},
    {"game.settings.zero_x_trace_build_default", zero_x_trace_build_default},
};

}  // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
