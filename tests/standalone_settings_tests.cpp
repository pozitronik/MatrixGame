// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "StandaloneSettings.hpp"
#include "CBlockPar.hpp"
#include "stupid_logger.hpp"

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

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
    values.delayed_damage = StandaloneSettings::DamageAttribution::Original;
    values.map = L"Matrix\\Map\\Desert (2).cmap";
    values.group_counts = StandaloneSettings::GroupCountMode::Original;
    values.place_containment = StandaloneSettings::PlaceContainmentMode::Original;
    values.zero_x_trace = StandaloneSettings::ZeroXTraceMode::Original;
    values.music_volume = 60;
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
    MG_CHECK(selection.values.music_volume == 100);
    MG_CHECK(selection.values.skybox == Skybox::High && selection.values.robot_shadows);
}

void battle_bonus() {
    MG_CHECK(parse(L"BattleBonus=None").values.battle_bonus == BattleChoice::Bonus::None);
    MG_CHECK(parse(L"BattleBonus=AirSupport").values.battle_bonus == BattleChoice::Bonus::AirSupport);
    MG_CHECK(parse(L"BattleBonus=reinforcedarmor").values.battle_bonus == BattleChoice::Bonus::ReinforcedArmor);
    for (const auto text : {L"BattleBonus=Hard", L"BattleBonus=", L"BattleBonus=None\nBattleBonus=AirSupport"}) {
        const auto value = parse(text);
        MG_CHECK(value.invalid == std::vector<std::string>{"BattleBonus"});
        MG_CHECK(value.values.battle_bonus == BattleChoice::Bonus::None);
    }
    TemporaryDirectory directory;
    const std::filesystem::path path = L"CFG/standalone.txt";
    std::string error;
    for (const auto bonus : {BattleChoice::Bonus::None, BattleChoice::Bonus::AirSupport, BattleChoice::Bonus::ReinforcedArmor}) {
        auto chosen = custom();
        chosen.battle_bonus = bonus;
        if (!StandaloneSettings::save(chosen, error, path.c_str())) throw std::runtime_error(error);
        MG_CHECK(StandaloneSettings::load(path.c_str()).values == chosen);
    }
}

void explicit_values() {
    const auto selection = parse(
            L"RandomGenerator=LegacyCRT\r\nResolutionWidth=1280\r\nResolutionHeight=720\r\n"
            L"WindowMode=Borderless\r\nSoundVolume= 40 // quieter\r\nCameraSpeed=150\r\n"
            L"StencilShadows=off\r\nProjectedShadows=FALSE\r\nSkybox=low\r\nRobotShadows=0\r\n"
            L"DelayedDamage=ORIGINAL\r\n"
            L"Map= Matrix\\Map\\Desert (2).cmap // chosen in the Map window\r\n"
            L"GroupCounts=ORIGINAL\r\nPlaceContainment=original\r\nZeroXTrace=original\r\n"
            L"MusicVolume=60 // under the effects\r\n");
    MG_CHECK(selection.invalid.empty());
    MG_CHECK(selection.values == custom());
    MG_CHECK(parse(L"StencilShadows=TRUE\nProjectedShadows=1\nRobotShadows=On\nSkybox=OFF\n").values.skybox ==
             Skybox::Off);
    // The map path keeps its spelling; an empty value selects the configured map.
    MG_CHECK(parse(L"Map=Matrix/Map/A.CMAP\n").values.map == L"Matrix/Map/A.CMAP");
    const auto empty = parse(L"Map=\n");
    MG_CHECK(empty.invalid.empty() && empty.values.map.empty());
}

void invalid_values() {
    const auto selection = parse(
            L"SoundVolume=101\nCameraSpeed=24\nStencilShadows=maybe\nProjectedShadows=\nSkybox=Ultra\n"
            L"RobotShadows=Off\nRobotShadows=On\nResolutionWidth=12\nDelayedDamage=Broken\nMap=..\\escape.cmap\n"
            L"GroupCounts=Fixed\nPlaceContainment=Diagonal\nZeroXTrace=Vertical\nMusicVolume=-5\n");
    MG_CHECK(selection.values.sound_volume == 100 && selection.values.camera_speed == 100);
    MG_CHECK(selection.values.stencil_shadows && selection.values.projected_shadows);
    MG_CHECK(selection.values.skybox == Skybox::High && selection.values.robot_shadows);
    MG_CHECK(selection.values.delayed_damage == StandaloneSettings::DamageAttribution::Corrected);
    MG_CHECK(selection.values.group_counts == StandaloneSettings::GroupCountMode::Corrected);
    MG_CHECK(selection.values.place_containment == StandaloneSettings::PlaceContainmentMode::Corrected);
    MG_CHECK(selection.values.zero_x_trace == StandaloneSettings::ZeroXTraceMode::Corrected);
    MG_CHECK(selection.invalid == std::vector<std::string>({"SoundVolume", "CameraSpeed", "StencilShadows",
                                                            "ProjectedShadows", "Skybox", "RobotShadows",
                                                            "DelayedDamage", "Map", "GroupCounts",
                                                            "PlaceContainment", "ZeroXTrace", "MusicVolume"}));
    MG_CHECK(selection.values.music_volume == 100);
    MG_CHECK(selection.values.map.empty());
    for (const wchar_t *text : {L"Map=x.txt\n", L"Map=C:\\x.cmap\n", L"Map=a.cmap\nMap=b.cmap\n", L"Map=a|b.cmap\n"}) {
        const auto rejected = parse(text);
        MG_CHECK(rejected.values.map.empty() && rejected.invalid == std::vector<std::string>({"Map"}));
    }
    // A repeated key is invalid and keeps the default.
    const auto repeated = parse(L"GroupCounts=Original\nGroupCounts=Original\n");
    MG_CHECK(repeated.values.group_counts == StandaloneSettings::GroupCountMode::Corrected);
    MG_CHECK(repeated.invalid == std::vector<std::string>({"GroupCounts"}));
    const auto repeated_place = parse(L"PlaceContainment=Original\nPlaceContainment=Original\n");
    MG_CHECK(repeated_place.values.place_containment == StandaloneSettings::PlaceContainmentMode::Corrected);
    MG_CHECK(repeated_place.invalid == std::vector<std::string>({"PlaceContainment"}));
    const auto repeated_trace = parse(L"ZeroXTrace=Original\nZeroXTrace=Original\n");
    MG_CHECK(repeated_trace.values.zero_x_trace == StandaloneSettings::ZeroXTraceMode::Corrected);
    MG_CHECK(repeated_trace.invalid == std::vector<std::string>({"ZeroXTrace"}));
    const auto music = parse(L"MusicVolume=20\nMusicVolume=30\n");
    MG_CHECK(music.values.music_volume == 100 && music.invalid == std::vector<std::string>({"MusicVolume"}));
    // Display keys keep their established all-or-default contract and diagnostics.
    MG_CHECK(selection.values.display == Display::Settings{});
    const auto valid = parse(L"SoundVolume=0\nCameraSpeed=300\nMusicVolume=0\n");
    MG_CHECK(valid.invalid.empty() && valid.values.sound_volume == 0 && valid.values.camera_speed == 300);
    MG_CHECK(valid.values.music_volume == 0);
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
            L"CameraSpeed=150\r\n"
            L"DelayedDamage=Original\r\n"
            L"Map=Matrix\\Map\\Desert (2).cmap\r\n"
            L"GroupCounts=Original\r\n"
            L"BattleBonus=None\r\n"
            L"PlaceContainment=Original\r\n"
            L"ZeroXTrace=Original\r\n"
            L"MusicVolume=60\r\n";
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

void round_trip_inline_blocks() {
    // CBlockPar reads a statement after a block's closing brace at the enclosing level.
    struct Case {
        const wchar_t *original;
        int sound_volume;
        const wchar_t *start;
        const wchar_t *end;
    };
    const Case cases[] = {
            {L"Extension {} SoundVolume=20\n", 20, L"Extension {} SoundVolume=40\nRandomGenerator=", L""},
            {L"Block {\n    SoundVolume=7\n} SoundVolume=20 // kept\nOther { SoundVolume=8\n}\n", 20,
             L"Block {\n    SoundVolume=7\n} SoundVolume=40 // kept\nOther { SoundVolume=8\n}\nRandomGenerator=", L""},
            // A repeated key is invalid; saving keeps the first and the block's comment.
            {L"SoundVolume=20\nExtension {} SoundVolume=30 // note\n", 100,
             L"SoundVolume=40\nExtension {} // note\nRandomGenerator=", L""},
            // A top-level closing brace ends the reader's input, so new keys must precede it.
            {L"UnknownKey=1\n}\nSoundVolume=20\n", 100, L"UnknownKey=1\nRandomGenerator=", L"}\nSoundVolume=20\n"},
            {L"Extension {} }\n", 100, L"Extension {} \nRandomGenerator=", L"MusicVolume=60\n}\n"},
    };
    for (const auto &item : cases) {
        MG_CHECK(parse(item.original).values.sound_volume == item.sound_volume);
        const auto updated = StandaloneSettings::update_text(item.original, custom());
        MG_CHECK(updated.starts_with(item.start) && updated.ends_with(item.end));
        const auto selection = parse(updated);
        MG_CHECK(selection.invalid.empty() && selection.values == custom());
        MG_CHECK(StandaloneSettings::update_text(updated, custom()) == updated);
    }
}

void save_utf8() {
    TemporaryDirectory directory;
    const std::filesystem::path path = L"CFG/standalone.txt";
    write_bytes(path, "// Comment \xd0\x97\r\nUnknownKey=\xd0\xb2\r\nSoundVolume=100\r\n");
    std::string error;
    if (!StandaloneSettings::save(custom(), error, path.c_str())) throw std::runtime_error(error);
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
    if (!StandaloneSettings::save(custom(), error, path.c_str())) throw std::runtime_error(error);
    const auto saved = read_bytes(path);
    MG_CHECK(saved.size() % 2 == 0 && saved.starts_with(std::string("\xff\xfe/\0/\0 \0\x17\x04", 10)));
    const auto loaded = StandaloneSettings::load(path.c_str());
    MG_CHECK(loaded.invalid.empty() && loaded.values == custom());
}

void save_creates_file() {
    TemporaryDirectory directory;
    std::string error;
    if (!StandaloneSettings::save(custom(), error)) throw std::runtime_error(error);
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
    const auto before = GetTickCount();
    MG_CHECK(!StandaloneSettings::save(custom(), error, path.c_str()));
    const auto elapsed = GetTickCount() - before;
    std::cout << "readonly elapsed_ms=" << elapsed << " error=" << error << '\n';
    MG_CHECK(elapsed < 250);
    MG_CHECK(!error.empty());
    MG_CHECK(read_bytes(path) == original);
    MG_CHECK(!std::filesystem::exists(L"CFG/standalone.txt.tmp"));
}

struct FileHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~FileHandle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
};

void save_target_lock(bool transient) {
    TemporaryDirectory directory;
    const std::filesystem::path path = L"CFG/standalone.txt";
    const std::string original = "// Keep this comment\r\nSoundVolume=100\r\n";
    write_bytes(path, original);
    // Permit save's initial read, but deny deletion/replacement of the target.
    FileHandle target{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    MG_CHECK(target.value != INVALID_HANDLE_VALUE);
    std::atomic<bool> saw_temporary = false;
    std::jthread release;
    if (transient) {
        const HANDLE held = target.value;
        // Keep ownership here until the thread exists; its join precedes scope cleanup.
        release = std::jthread([held, &saw_temporary](std::stop_token stop) {
            const auto waiting = GetTickCount();
            // Observe save's real preparation boundary before delaying the unlock.
            // A bounded, cancellable wait also handles an unexpectedly early save failure.
            while (!stop.stop_requested() && GetTickCount() - waiting < 5000) {
                if (GetFileAttributesW(L"CFG/standalone.txt.tmp") != INVALID_FILE_ATTRIBUTES) {
                    saw_temporary = true;
                    Sleep(30);
                    break;
                }
                Sleep(1);
            }
            CloseHandle(held);
        });
        target.value = INVALID_HANDLE_VALUE;
    }
    std::string error;
    const auto before = GetTickCount();
    const bool saved = StandaloneSettings::save(custom(), error, path.c_str());
    const auto elapsed = GetTickCount() - before;
    std::cout << "target_lock=" << (transient ? "transient" : "persistent")
              << " saved=" << saved << " elapsed_ms=" << elapsed << " error=" << error << '\n';
    if (release.joinable()) {
        release.request_stop();
        release.join();
    }
    MG_CHECK(elapsed < 5000);
    MG_CHECK(saved == transient);
    MG_CHECK(!std::filesystem::exists(L"CFG/standalone.txt.tmp"));
    if (transient) {
        MG_CHECK(saw_temporary);
        MG_CHECK(error.empty());
        MG_CHECK(read_bytes(path).starts_with("// Keep this comment\r\nSoundVolume=40\r\n"));
        MG_CHECK(StandaloneSettings::load(path.c_str()).values == custom());
    } else {
        MG_CHECK(elapsed >= 500);
        MG_CHECK(error.find("Cannot replace the settings file (Windows error ") != std::string::npos);
        MG_CHECK(error.find("error 5)") != std::string::npos || error.find("error 32)") != std::string::npos);
        MG_CHECK(read_bytes(path) == original);
    }
}

void unreadable_file() {
    TemporaryDirectory directory;
    std::filesystem::create_directories(L"CFG/standalone.txt");
    const auto selection = StandaloneSettings::load();
    MG_CHECK(selection.values == Values{} && selection.invalid.size() == 1);
}

constexpr tests::Case cases[] = {
    {"game.settings.battle_bonus", battle_bonus},
    {"game.settings.defaults", defaults},
    {"game.settings.explicit_values", explicit_values},
    {"game.settings.invalid_values", invalid_values},
    {"game.settings.preserves_text", preserves_text},
    {"game.settings.round_trip", round_trip},
    {"game.settings.round_trip_inline_blocks", round_trip_inline_blocks},
    {"game.settings.save_utf8", save_utf8},
    {"game.settings.save_wide", save_wide},
    {"game.settings.save_creates_file", save_creates_file},
    {"game.settings.save_failure", save_failure_keeps_file},
    {"game.settings.unreadable_file", unreadable_file},
    {"game.settings.save_transient_lock", [] { save_target_lock(true); }},
    {"game.settings.save_persistent_lock", [] { save_target_lock(false); }},
};

}  // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
