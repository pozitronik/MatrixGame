// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "StandaloneSettings.hpp"
#include "CBlockPar.hpp"
#include "stupid_logger.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

logger_type lgr{std::cerr};

namespace {

using StandaloneSettings::Skybox;
using StandaloneSettings::Values;

StandaloneSettings::Selection parse(const std::wstring &text) {
    Base::CBlockPar options;
    options.LoadFromText(text);
    return StandaloneSettings::resolve(options);
}

Values custom() {
    Values values;
    values.display = {1280, 720, Display::Mode::Borderless, Display::Mode::Borderless};
    values.random = random::Mode::LegacyCRT;
    values.sound_volume = 40;
    values.camera_speed = 150;
    values.stencil_shadows = false;
    values.projected_shadows = false;
    values.skybox = Skybox::Low;
    values.robot_shadows = false;
    return values;
}

struct TemporaryDirectory {
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("settings-" + std::to_string(GetCurrentProcessId()));
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

void write_bytes(const std::filesystem::path &path, const std::string &bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    MG_CHECK(stream.good());
}

std::string read_bytes(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

void defaults() {
    const auto selection = parse(L"");
    MG_CHECK(selection.invalid.empty());
    MG_CHECK(selection.values == Values{});
    MG_CHECK(selection.values.sound_volume == 100 && selection.values.camera_speed == 100);
    MG_CHECK(selection.values.skybox == Skybox::High && selection.values.robot_shadows);
}

void explicit_values() {
    const auto selection = parse(
            L"RandomGenerator=LegacyCRT\r\nResolutionWidth=1280\r\nResolutionHeight=720\r\n"
            L"WindowMode=Borderless\r\nSoundVolume= 40 // quieter\r\nCameraSpeed=150\r\n"
            L"StencilShadows=off\r\nProjectedShadows=FALSE\r\nSkybox=low\r\nRobotShadows=0\r\n");
    MG_CHECK(selection.invalid.empty());
    MG_CHECK(selection.values == custom());
    MG_CHECK(parse(L"StencilShadows=TRUE\nProjectedShadows=1\nRobotShadows=On\nSkybox=OFF\n").values.skybox ==
             Skybox::Off);
}

void invalid_values() {
    const auto selection = parse(
            L"SoundVolume=101\nCameraSpeed=24\nStencilShadows=maybe\nProjectedShadows=\nSkybox=Ultra\n"
            L"RobotShadows=Off\nRobotShadows=On\nResolutionWidth=12\n");
    MG_CHECK(selection.values.sound_volume == 100 && selection.values.camera_speed == 100);
    MG_CHECK(selection.values.stencil_shadows && selection.values.projected_shadows);
    MG_CHECK(selection.values.skybox == Skybox::High && selection.values.robot_shadows);
    MG_CHECK(selection.invalid == std::vector<std::string>({"SoundVolume", "CameraSpeed", "StencilShadows",
                                                            "ProjectedShadows", "Skybox", "RobotShadows"}));
    // Display keys keep their established all-or-default contract and diagnostics.
    MG_CHECK(selection.values.display == Display::Settings{});
    const auto valid = parse(L"SoundVolume=0\nCameraSpeed=300\n");
    MG_CHECK(valid.invalid.empty() && valid.values.sound_volume == 0 && valid.values.camera_speed == 300);
}

void preserves_text() {
    const std::wstring original =
            L"// Header comment\r\n"
            L"RandomGenerator=ParkMiller\r\n"
            L"\r\n"
            L"  ResolutionWidth =  1024   // trailing comment\r\n"
            L"UnknownKey=kept\r\n"
            L"Nested {\r\n"
            L"    SoundVolume=7\r\n"
            L"}\r\n"
            L"WindowMode=Windowed\r\n"
            L"WindowMode=Exclusive\r\n"
            L"// SoundVolume=1 stays a comment\r\n"
            L"FullscreenMode=Borderless";
    const auto updated = StandaloneSettings::update_text(original, custom());
    const std::wstring expected =
            L"// Header comment\r\n"
            L"RandomGenerator=LegacyCRT\r\n"
            L"\r\n"
            L"  ResolutionWidth =  1280   // trailing comment\r\n"
            L"UnknownKey=kept\r\n"
            L"Nested {\r\n"
            L"    SoundVolume=7\r\n"
            L"}\r\n"
            L"WindowMode=Borderless\r\n"
            L"// SoundVolume=1 stays a comment\r\n"
            L"FullscreenMode=Borderless\r\n"
            L"ResolutionHeight=720\r\n"
            L"SoundVolume=40\r\n"
            L"StencilShadows=Off\r\n"
            L"ProjectedShadows=Off\r\n"
            L"Skybox=Low\r\n"
            L"RobotShadows=Off\r\n"
            L"CameraSpeed=150\r\n";
    MG_CHECK(updated == expected);
    MG_CHECK(StandaloneSettings::update_text(updated, custom()) == expected);
    const auto unix = StandaloneSettings::update_text(L"UnknownKey=1\n", Values{});
    MG_CHECK(unix.starts_with(L"UnknownKey=1\nRandomGenerator=ParkMiller\n") && unix.find(L'\r') == std::wstring::npos);
}

void round_trip() {
    for (const auto &values : {Values{}, custom()}) {
        const auto selection = parse(StandaloneSettings::update_text(L"", values));
        MG_CHECK(selection.invalid.empty() && selection.values == values);
    }
    auto exclusive = custom();
    exclusive.display.mode = exclusive.display.fullscreen = Display::Mode::Exclusive;
    MG_CHECK(parse(StandaloneSettings::update_text(L"", exclusive)).values == exclusive);
}

void save_utf8() {
    TemporaryDirectory directory;
    const std::filesystem::path path = L"CFG/standalone.txt";
    write_bytes(path, "// Comment \xd0\x97\r\nUnknownKey=\xd0\xb2\r\nSoundVolume=100\r\n");
    std::string error;
    MG_CHECK(StandaloneSettings::save(custom(), error, path.c_str()));
    const auto bytes = read_bytes(path);
    MG_CHECK(bytes.starts_with("// Comment \xd0\x97\r\nUnknownKey=\xd0\xb2\r\nSoundVolume=40\r\nRandomGenerator=LegacyCRT\r\n"));
    MG_CHECK(!std::filesystem::exists(L"CFG/standalone.txt.tmp"));
    const auto loaded = StandaloneSettings::load(path.c_str());
    MG_CHECK(loaded.invalid.empty() && loaded.values == custom());
}

void save_wide() {
    TemporaryDirectory directory;
    const std::filesystem::path path = L"CFG/standalone.txt";
    std::string bytes("\xff\xfe", 2);
    for (wchar_t ch : std::wstring(L"// \x0417\x0432\x0443\x043a\r\nSkybox=High\r\n")) {
        bytes += static_cast<char>(ch & 0xff);
        bytes += static_cast<char>(ch >> 8);
    }
    write_bytes(path, bytes);
    std::string error;
    MG_CHECK(StandaloneSettings::save(custom(), error, path.c_str()));
    const auto saved = read_bytes(path);
    MG_CHECK(saved.size() % 2 == 0 && saved.starts_with(std::string("\xff\xfe/\0/\0 \0\x17\x04", 10)));
    const auto loaded = StandaloneSettings::load(path.c_str());
    MG_CHECK(loaded.invalid.empty() && loaded.values == custom());
}

void save_creates_file() {
    TemporaryDirectory directory;
    std::string error;
    MG_CHECK(StandaloneSettings::save(custom(), error));
    MG_CHECK(std::filesystem::exists(L"CFG\\standalone.txt"));
    MG_CHECK(StandaloneSettings::load().values == custom());
    const auto missing = StandaloneSettings::load(L"CFG\\absent.txt");
    MG_CHECK(missing.invalid.empty() && missing.values == Values{});
}

void save_failure_keeps_file() {
    TemporaryDirectory directory;
    const std::filesystem::path path = L"CFG/standalone.txt";
    const std::string original = "SoundVolume=100\r\n";
    write_bytes(path, original);
    MG_CHECK(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY));
    std::string error;
    MG_CHECK(!StandaloneSettings::save(custom(), error, path.c_str()));
    MG_CHECK(!error.empty());
    MG_CHECK(read_bytes(path) == original);
    MG_CHECK(!std::filesystem::exists(L"CFG/standalone.txt.tmp"));
}

void unreadable_file() {
    TemporaryDirectory directory;
    std::filesystem::create_directories(L"CFG/standalone.txt");
    const auto selection = StandaloneSettings::load();
    MG_CHECK(selection.values == Values{} && selection.invalid.size() == 1);
}

constexpr tests::Case cases[] = {
    {"game.settings.defaults", defaults},
    {"game.settings.explicit_values", explicit_values},
    {"game.settings.invalid_values", invalid_values},
    {"game.settings.preserves_text", preserves_text},
    {"game.settings.round_trip", round_trip},
    {"game.settings.save_utf8", save_utf8},
    {"game.settings.save_wide", save_wide},
    {"game.settings.save_creates_file", save_creates_file},
    {"game.settings.save_failure", save_failure_keeps_file},
    {"game.settings.unreadable_file", unreadable_file},
};

}  // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
