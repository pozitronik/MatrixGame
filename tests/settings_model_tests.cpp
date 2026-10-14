// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "SettingsModel.hpp"
#include "stupid_logger.hpp"

logger_type lgr{std::cerr};

namespace {

using namespace SettingsModel;
using Display::Mode;
using StandaloneSettings::Values;

int row(Row value) { return static_cast<int>(value); }

Window english(Values values = {}) {
    return Window(values, {{800, 600}, {1024, 768}, {1280, 720}}, Language::English);
}

void resolutions() {
    const auto choices = resolution_choices({{1280, 720}, {320, 240}, {800, 600}, {1280, 720}, {640, 400}},
                                            {1024, 768});
    MG_CHECK(choices == std::vector<Resolution>({{800, 600}, {1024, 768}, {1280, 720}}));
    MG_CHECK(resolution_choices({}, {1600, 900}) == std::vector<Resolution>({{1600, 900}}));
}

void selection() {
    auto window = english();
    MG_CHECK(window.selected() == row(Row::Resolution));
    MG_CHECK(!window.select(row(Row::SoundGroup)) && !window.select(-1) && !window.select(row_count));
    MG_CHECK(window.select(row(Row::SoundVolume)) && !window.select(row(Row::SoundVolume)));
    window.move(1);
    MG_CHECK(window.selected() == row(Row::StencilShadows));  // The graphics group row is skipped.
    window.select(row(Row::Resolution));
    window.move(-1);
    MG_CHECK(window.selected() == row(Row::DelayedDamage));  // Wraps past the display group row.
    window.move(1);
    MG_CHECK(window.selected() == row(Row::Resolution));
}

void stepping() {
    auto window = english();
    MG_CHECK(!window.changed());
    MG_CHECK(window.change(row(Row::Resolution), 1) && window.values().display.width == 1280);
    MG_CHECK(!window.change(row(Row::Resolution), 1));  // The list end does not wrap.
    MG_CHECK(window.change(row(Row::Resolution), -1) && window.change(row(Row::Resolution), -1));
    MG_CHECK(window.values().display.width == 800 && window.values().display.height == 600);
    MG_CHECK(window.change(row(Row::SoundVolume), -1) && window.values().sound_volume == 90);
    for (int i = 0; i < 12; ++i) window.change(row(Row::SoundVolume), -1);
    MG_CHECK(window.values().sound_volume == 0 && !window.change(row(Row::SoundVolume), -1));
    MG_CHECK(window.change(row(Row::CameraSpeed), 1) && window.values().camera_speed == 125);
    for (int i = 0; i < 20; ++i) window.change(row(Row::CameraSpeed), 1);
    MG_CHECK(window.values().camera_speed == StandaloneSettings::max_camera_speed);
    MG_CHECK(window.change(row(Row::StencilShadows), 1) && !window.values().stencil_shadows);
    MG_CHECK(window.change(row(Row::ProjectedShadows), -1) && !window.values().projected_shadows);
    MG_CHECK(window.change(row(Row::RobotShadows), 1) && !window.values().robot_shadows);
    MG_CHECK(window.change(row(Row::Skybox), 1) && window.values().skybox == StandaloneSettings::Skybox::Off);
    MG_CHECK(window.change(row(Row::Skybox), -1) && window.values().skybox == StandaloneSettings::Skybox::High);
    MG_CHECK(window.change(row(Row::RandomGenerator), 1) && window.values().random == random::Mode::LegacyCRT);
    MG_CHECK(window.value(row(Row::DelayedDamage)) == L"Corrected");
    MG_CHECK(window.change(row(Row::DelayedDamage), 1) &&
             window.values().delayed_damage == StandaloneSettings::DamageAttribution::Original);
    MG_CHECK(window.value(row(Row::DelayedDamage)) == L"Original");
    MG_CHECK(!window.change(row(Row::GameplayGroup), 1) && !window.change(row(Row::Skybox), 0));
    MG_CHECK(window.changed());

    Values custom;
    custom.sound_volume = 45;
    custom.camera_speed = 110;
    auto off_grid = english(custom);
    MG_CHECK(off_grid.change(row(Row::SoundVolume), 1) && off_grid.values().sound_volume == 50);
    MG_CHECK(off_grid.change(row(Row::CameraSpeed), -1) && off_grid.values().camera_speed == 100);
}

void display_modes() {
    auto window = english();
    MG_CHECK(window.change(row(Row::FullscreenMode), 1));
    MG_CHECK(window.values().display.fullscreen == Mode::Exclusive && window.values().display.mode == Mode::Windowed);
    MG_CHECK(window.change(row(Row::WindowMode), 1));
    MG_CHECK(window.values().display.mode == Mode::Borderless && window.values().display.fullscreen == Mode::Borderless);
    MG_CHECK(window.change(row(Row::FullscreenMode), 1));
    MG_CHECK(window.values().display.mode == Mode::Exclusive && window.values().display.fullscreen == Mode::Exclusive);
    MG_CHECK(window.change(row(Row::WindowMode), 1) && window.values().display.mode == Mode::Windowed);
    MG_CHECK(window.values().display.fullscreen == Mode::Exclusive);  // Alt+Enter keeps the last fullscreen mode.
    MG_CHECK(window.change(row(Row::WindowMode), -1) && window.values().display.mode == Mode::Exclusive);
}

void geometry() {
    const int y = row_top(row(Row::SoundVolume)) + Layout::row_height / 2;
    MG_CHECK(hit(Layout::label_x, y).row == row(Row::SoundVolume) && hit(Layout::label_x, y).direction == 0);
    MG_CHECK(hit(Layout::previous_x, y).direction == -1);
    MG_CHECK(hit(Layout::value_x + Layout::value_width / 2 - 1, y).direction == -1);
    MG_CHECK(hit(Layout::value_x + Layout::value_width / 2, y).direction == 1);
    MG_CHECK(hit(Layout::next_x, y).direction == 1);
    MG_CHECK(hit(Layout::cell_right + 10, y).row == -1 && hit(Layout::label_x - 11, y).row == -1);
    MG_CHECK(hit(Layout::label_x, row_top(row(Row::SoundGroup)) + 1).row == -1);
    MG_CHECK(hit(Layout::label_x, Layout::first_row_y - 1).row == -1);
    MG_CHECK(hit(Layout::label_x, row_top(row_count)).row == -1);
    MG_CHECK(hit(Layout::label_x, row_top(row(Row::Resolution))).row == row(Row::Resolution));
    MG_CHECK(hit(Layout::label_x, row_top(row(Row::Resolution)) - 1).row == -1);
    MG_CHECK(Layout::content_height + 21 + 28 <= 480);  // Fits the smallest supported resolution.
}

void text() {
    auto window = english();
    MG_CHECK(window.title() == L"Settings" && window.label(row(Row::SoundVolume)) == L"Volume");
    MG_CHECK(window.label(row(Row::Resolution)) == L"Resolution");
    for (const auto restart : {Row::Skybox, Row::RobotShadows, Row::RandomGenerator})
        MG_CHECK(needs_restart(row(restart)) && window.label(row(restart)).ends_with(L" *"));
    for (const auto live : {Row::Resolution, Row::WindowMode, Row::FullscreenMode, Row::SoundVolume,
                            Row::StencilShadows, Row::ProjectedShadows, Row::CameraSpeed, Row::DelayedDamage})
        MG_CHECK(!needs_restart(row(live)));
    MG_CHECK(window.value(row(Row::Resolution)) == L"1024x768" && window.value(row(Row::SoundVolume)) == L"100%");
    MG_CHECK(window.value(row(Row::WindowMode)) == L"Windowed" && window.value(row(Row::FullscreenMode)) == L"Borderless");
    MG_CHECK(window.value(row(Row::RandomGenerator)) == L"ParkMiller" && window.value(row(Row::Skybox)) == L"High");
    MG_CHECK(window.value(row(Row::DisplayGroup)).empty());

    MG_CHECK(detect_language(L"settings (c)") == Language::English);
    MG_CHECK(detect_language(L"\u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 (c)") == Language::Russian);
    Window russian({}, {}, Language::Russian);
    for (int i = 0; i < row_count; ++i) {
        MG_CHECK(!russian.label(i).empty());
        if (i != row(Row::FullscreenMode)) MG_CHECK(detect_language(russian.label(i)) == Language::Russian);
    }
    MG_CHECK(detect_language(russian.title() + russian.legend() + russian.error_text()) == Language::Russian);
    MG_CHECK(russian.value(row(Row::RandomGenerator)) == L"ParkMiller");
    MG_CHECK(detect_language(russian.value(row(Row::DelayedDamage))) == Language::Russian);
}

size_t count(const std::wstring &text, const std::wstring &part) {
    size_t result = 0;
    for (size_t at = text.find(part); at != std::wstring::npos; at = text.find(part, at + part.size())) ++result;
    return result;
}

void hint_text() {
    for (const auto language : {Language::English, Language::Russian}) {
        Window window({}, {}, language);
        const auto hint = hint_template(window);
        MG_CHECK(hint.starts_with(L"2|_BORDER:0:0:0:28|"));
        MG_CHECK(count(hint, L"_MOD:COPY") == 2);  // OK and Cancel hint buttons.
        MG_CHECK(hint.find(window.title()) != std::wstring::npos);
        for (int i = 0; i < row_count; ++i) {
            MG_CHECK(hint.find(L"|_TEXT:" + window.label(i)) != std::wstring::npos);
            MG_CHECK(window.label(i).find(L'|') == std::wstring::npos && window.value(i).find(L'|') == std::wstring::npos);
            if (!is_group(i)) MG_CHECK(hint.find(L"|_TEXT:" + window.value(i)) != std::wstring::npos);
        }
        // Every text element is positioned; the blank block is the only flowing text.
        MG_CHECK(count(hint, L"|_TEXT:") == count(hint, L"|_POS:") + 1);
        MG_CHECK(hint.find(window.error_text()) == std::wstring::npos);
        window.set_error(true);
        MG_CHECK(hint_template(window).find(window.error_text()) != std::wstring::npos);
    }
    Window window({}, {}, Language::English);
    const auto selected = L"_COLOR:FF0B5FA5|_WIDTH:" + std::to_wstring(Layout::previous_x - Layout::label_x - 12) +
                          L"|_HEIGHT:19|_ALIGN:0:0|_POS:82:" + std::to_wstring(row_top(row(Row::Resolution))) +
                          L"|_TEXT:Resolution|";  // The next element follows, so the old " *" label fails.
    MG_CHECK(hint_template(window).find(selected) != std::wstring::npos);
    window.select(row(Row::SoundVolume));
    MG_CHECK(hint_template(window).find(selected) == std::wstring::npos);
}

constexpr tests::Case cases[] = {
    {"game.settings.window_resolutions", resolutions},
    {"game.settings.window_selection", selection},
    {"game.settings.window_stepping", stepping},
    {"game.settings.window_display_modes", display_modes},
    {"game.settings.window_geometry", geometry},
    {"game.settings.window_text", text},
    {"game.settings.window_hint", hint_text},
};

}  // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
