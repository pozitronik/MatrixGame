// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "audio_fixture.hpp"
#include "SoundBridge.hpp"
#include "MatrixSoundManager.hpp"
#include "MatrixGameDll.hpp"
#include "MatrixMap.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace {
std::shared_ptr<audio_fixture::DeviceState> startup_device;
int device_creations{};
bool fail_device{};
}

// Replace only the platform device factory in this executable. Startup, mapping,
// resource reads, WAV decoding and playback routing use production implementations.
namespace Audio {
std::unique_ptr<Device> create_xaudio_device() {
    ++device_creations;
    if (fail_device) throw std::runtime_error("Synthetic device initialization failure");
    MG_CHECK(startup_device != nullptr);
    return std::make_unique<audio_fixture::FakeDevice>(startup_device);
}
// Synthetic tracks hold their first sample value as two little-endian bytes.
std::unique_ptr<Decoder> open_vorbis(std::vector<uint8_t> bytes) {
    if (bytes.size() != 2) throw std::runtime_error("Synthetic unsupported track");
    return std::make_unique<audio_fixture::FakeDecoder>(
        audio_fixture::TrackSpec{1, 8000, 2000, int16_t(bytes[0] | (bytes[1] << 8))});
}
}

namespace {
struct StartupFixture {
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("audio-startup-" + std::to_string(GetCurrentProcessId()));
    StartupFixture() {
        MG_CHECK(std::filesystem::create_directory(root));
        std::filesystem::current_path(root);
        startup_device = std::make_shared<audio_fixture::DeviceState>();
        device_creations = 0;
        fail_device = false;
        g_RangersInterface = nullptr;
    }
    ~StartupFixture() {
        SoundBridge::shutdown();
        startup_device.reset();
        std::filesystem::current_path(previous);
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void wave(const std::filesystem::path &path) {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        const auto bytes = audio_fixture::wave();
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        MG_CHECK(stream.good());
    }
    void track(const std::filesystem::path &path, int16_t base) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.put(char(base & 0xff)).put(char(base >> 8));
        MG_CHECK(stream.good());
    }
    void override_text(const std::string &text) {
        std::filesystem::create_directories("CFG");
        std::ofstream stream("CFG/sounds.txt", std::ios::binary);
        stream << text;
        MG_CHECK(stream.good());
    }
    void play(const wchar_t *name) {
        const auto id = SoundBridge::create(const_cast<wchar_t *>(name), 0, 0);
        MG_CHECK(id != 0);
        SoundBridge::play(id);
        MG_CHECK(SoundBridge::playing(id));
    }
};

void default_catalog_startup() {
    StartupFixture fixture;
    fixture.wave("sound/ButtonClick.wav");
    fixture.wave("sound/robots/chassis/track.wav");
    fixture.wave("sound/robots/exp/robot.wav");
    fixture.wave("sound/robots/voices/robots/robotdone.wav");
    fixture.wave("sound/robots/map/water.wav");
    fixture.wave("sound/robots/map/bottle.wav");
    MG_CHECK(!std::filesystem::exists("CFG/sounds.txt"));
    SoundBridge::initialize_standalone(true);
    MG_CHECK(SoundBridge::available() && device_creations == 1);
    for (const auto *name : {L"Sound.ButtonClick", L"Sound.ChasTrack", L"Sound.ExpRobot", L"Sound.RReady",
                            L"Sound.MapWater", L"Sound.MapAmb1"})
        fixture.play(name);
    MG_CHECK(startup_device->voices.size() == 6);
    wchar_t unresolved[] = L"Sound.BReady";
    MG_CHECK(SoundBridge::create(unresolved, 0, 0) == 0);
}

void map_spawner_catalog_startup() {
    StartupFixture fixture;
    // Map effect spawners supply names independently of the tracked CFG files.
    // Keep their resource contract explicit, including names shared with the host UI.
    const std::pair<const wchar_t *, const char *> clips[] = {
        {L"Sound.FormPlanetNone1", "sound/Planet/None/1.wav"},
        {L"Sound.FormPlanetNone3", "sound/Planet/None/3.wav"},
        {L"Sound.HangarOpen", "sound/HangarOpen.wav"},
        {L"Sound.MapWater", "sound/robots/map/water.wav"},
        {L"Sound.MapAmb1", "sound/robots/map/bottle.wav"},
        {L"Sound.MapAmb2", "sound/robots/map/fantasy1.wav"},
        {L"Sound.MapAmb3", "sound/robots/map/fantasy2.wav"},
        {L"Sound.MapAmb4", "sound/robots/map/amb_low1.wav"},
        {L"Sound.MapAmb5", "sound/robots/map/amb_low2.wav"},
        {L"Sound.MapAmb6", "sound/robots/map/amb_low3.wav"},
        {L"Sound.MapAmb7", "sound/robots/map/amb_low4.wav"},
        {L"Sound.Splash", "sound/robots/world/splash.wav"},
        {L"Sound.Zamknulo", "sound/robots/weapon/shorted.wav"},
    };
    for (const auto &[name, path] : clips) fixture.wave(path);
    MG_CHECK(!std::filesystem::exists("CFG/sounds.txt"));
    SoundBridge::initialize_standalone(true);
    MG_CHECK(SoundBridge::available() && device_creations == 1);
    for (const auto &[name, path] : clips) fixture.play(name);
    MG_CHECK(startup_device->voices.size() == std::size(clips));
}

void master_level_retained() {
    StartupFixture fixture;
    fixture.wave("sound/ButtonClick.wav");
    SoundBridge::master_volume(0.25f);  // Set before the service exists, as at startup.
    SoundBridge::initialize_standalone(true);
    fixture.play(L"Sound.ButtonClick");
    const auto &voice = startup_device->voices[0];
    MG_CHECK(voice->volume == 0.25f);
    SoundBridge::volume(1, 0.5f);
    MG_CHECK(voice->volume == 0.125f && SoundBridge::volume(1) == 0.5f);
    SoundBridge::master_volume(1.0f);
    MG_CHECK(voice->volume == 0.5f);
    SoundBridge::master_volume(0.0f);
    SoundBridge::initialize_standalone(true);  // A new battle session keeps the level.
    fixture.play(L"Sound.ButtonClick");
    MG_CHECK(startup_device->voices.size() == 2 && startup_device->voices[1]->volume == 0.0f);
    SoundBridge::master_volume(1.0f);
}

void partial_mapping_override() {
    StartupFixture fixture;
    fixture.wave("synthetic.wav");
    fixture.wave("sound/robots/chassis/track.wav");
    fixture.override_text("Sound {\nButtonClick=synthetic.wav\nBReady=synthetic.wav\n}\n");
    SoundBridge::initialize_standalone(true);
    fixture.play(L"Sound.ButtonClick"); // No default ButtonClick file exists in this fixture.
    fixture.play(L"Sound.ChasTrack"); // An unspecified name retains its default.
    fixture.play(L"Sound.BReady"); // Additional names are supported.
    MG_CHECK(startup_device->voices.size() == 3);
}

void invalid_mapping_fallback() {
    StartupFixture fixture;
    fixture.wave("sound/ButtonClick.wav");
    for (const std::string text : {
            "Other {\nButtonClick=missing.wav\n}\n",
            "Sound {\nButtonClick=missing.wav\nButtonClick=also-missing.wav\n}\n",
            "Sound {\nButtonClick=missing.wav\n}\nSound {\n}\n",
            "Sound {\nButtonClick=missing.wav\nNested {\n}\n}\n",
            "Sound {\nButtonClick=missing.wav\nInvalid.Name=missing.wav\n}\n",
            "Sound {\nButtonClick=\n}\n", "\xc3\x28"}) {
        fixture.override_text(text);
        SoundBridge::initialize_standalone(true);
        MG_CHECK(SoundBridge::available());
        fixture.play(L"Sound.ButtonClick");
    }
    MG_CHECK(device_creations == 7);
}

void startup_without_packages_or_device() {
    StartupFixture fixture;
    fixture.override_text("not a mapping");
    SoundBridge::initialize_standalone(false);
    MG_CHECK(!SoundBridge::available() && device_creations == 0);
    fail_device = true;
    SoundBridge::initialize_standalone(true);
    MG_CHECK(!SoundBridge::available() && device_creations == 1);
}

struct Frontend {
    Base::CBlockPar data;
    std::shared_ptr<audio_fixture::DeviceState> device = std::make_shared<audio_fixture::DeviceState>();
    int reports{};
    Frontend() {
        g_RangersInterface = nullptr;
        g_MatrixData = &data;
        auto *event = data.BlockGetAdd(L"Sounds")->BlockGetAdd(L"bclick");
        event->ParSetAdd(L"path", L"Sound.A");
        event->ParSetAdd(L"looped", L"1");
        event->ParSetAdd(L"vol", L"1,1");
        CSound::Init();
        SoundBridge::install(std::make_unique<Audio::Service>(std::make_unique<audio_fixture::FakeDevice>(device),
            Audio::Mapping{{L"Sound.A", L"synthetic.wav"}},
            [](const std::wstring &) { return audio_fixture::wave(); },
            [this](const std::wstring &, const std::string &) { ++reports; }));
    }
    ~Frontend() {
        g_RangersInterface = nullptr;
        CSound::Clear();
        SoundBridge::shutdown();
        g_MatrixData = nullptr;
    }
};

void layer_lifetime() {
    Frontend f;
    const auto a = CSound::Play(S_BCLICK, 0.5f, -0.25f, SL_INTERFACE, SEF_INTERRUPT);
    MG_CHECK(a != SOUND_ID_EMPTY && CSound::IsSoundPlay(a));
    MG_CHECK(f.device->voices.size() == 1 && f.device->voices[0]->loop);
    const auto skipped = CSound::Play(S_BCLICK, 1, 0, SL_INTERFACE, SEF_SKIP);
    MG_CHECK(skipped == SOUND_ID_EMPTY && f.device->voices.size() == 1 && CSound::IsSoundPlay(a));
    const auto b = CSound::Play(S_BCLICK, 1, 0, SL_INTERFACE, SEF_INTERRUPT);
    MG_CHECK(b != SOUND_ID_EMPTY && b != a);
    MG_CHECK(f.device->voices[0]->destroyed == 1 && !CSound::IsSoundPlay(a));
    CSound::LayerOff(SL_INTERFACE);
    MG_CHECK(!CSound::IsSoundPlay(b) && f.device->voices[1]->destroyed == 1);
    MG_CHECK(g_D3DD == nullptr);
}

void clear_reentrancy() {
    Frontend f;
    const auto id = CSound::Play(S_BCLICK, 1, 0, SL_ALL);
    MG_CHECK(CSound::IsSoundPlay(id));
    CSound::Clear(); CSound::Clear();
    MG_CHECK(!CSound::IsSoundPlay(id) && f.device->voices[0]->destroyed == 1);
    const auto next = CSound::Play(S_BCLICK, 1, 0, SL_ALL);
    MG_CHECK(next != SOUND_ID_EMPTY && next != id && CSound::IsSoundPlay(next));
}

void stale_layer_slot() {
    Frontend f;
    const auto layered = CSound::Play(S_BCLICK, 1, 0, SL_INTERFACE, SEF_INTERRUPT);
    f.device->voices[0]->playing = false;
    const auto other = CSound::Play(S_BCLICK, 1, 0, SL_ALL);
    MG_CHECK(layered != other && CSound::IsSoundPlay(other));
    (void)CSound::Play(S_BCLICK, 1, 0, SL_INTERFACE, SEF_INTERRUPT);
    MG_CHECK(f.device->voices[1]->playing && f.device->voices[1]->destroyed == 0);
    MG_CHECK(CSound::IsSoundPlay(other));
    CSound::StopPlay(other);
    MG_CHECK(!CSound::IsSoundPlay(other) && f.device->voices[1]->destroyed == 1);
}

void missing_mapping() {
    Frontend f;
    f.data.BlockGet(L"Sounds")->BlockGet(L"bclick")->ParSetAdd(L"path", L"missing");
    for (int i = 0; i < 4; ++i)
        MG_CHECK(CSound::Play(S_BCLICK, 1, 0, SL_INTERFACE) == SOUND_ID_EMPTY);
    MG_CHECK(f.device->voices.empty() && f.reports == 1);
}

void no_optional_packages() {
    Frontend f;
    const auto id = CSound::Play(S_BCLICK, 1, 0, SL_ALL);
    MG_CHECK(id != SOUND_ID_EMPTY && SoundBridge::available());
    SoundBridge::initialize_standalone(false);
    MG_CHECK(!SoundBridge::available());
    MG_CHECK(f.device->voices[0]->destroyed == 1);
    MG_CHECK(CSound::Play(S_BCLICK, 1, 0, SL_ALL) == SOUND_ID_EMPTY);
    SoundBridge::initialize_standalone(false);
    MG_CHECK(!SoundBridge::available() && f.reports == 0);
}

int host_created{}, host_played{}, host_destroyed{}, host_group{}, host_loop{};
dword __stdcall host_create(wchar *, int group, int loop) { ++host_created; host_group = group; host_loop = loop; return 71; }
void __stdcall host_play(dword id) { if (id == 71) ++host_played; }
void __stdcall host_destroy(dword id) { if (id == 71) ++host_destroyed; }
int __stdcall host_playing(dword id) { return id == 71; }
void __stdcall host_parameter(dword, float) {}

void host_routing() {
    Frontend f;
    SMGDRangersInterface host{};
    host.m_SoundCreate = host_create; host.m_SoundPlay = host_play;
    host.m_SoundDestroy = host_destroy; host.m_SoundIsPlay = host_playing;
    g_RangersInterface = &host;
    wchar_t name[] = L"Sound.A";
    const auto id = SoundBridge::create(name, 12, 1);
    SoundBridge::play(id); MG_CHECK(SoundBridge::playing(id)); SoundBridge::destroy(id);
    g_RangersInterface = nullptr;
    MG_CHECK(id == 71 && host_created == 1 && host_played == 1 && host_destroyed == 1);
    MG_CHECK(host_group == 12 && host_loop == 1 && f.device->voices.empty());
}

void host_layer_lifetime() {
    Frontend f;
    SMGDRangersInterface host{};
    host.m_SoundCreate = host_create; host.m_SoundPlay = host_play;
    host.m_SoundDestroy = host_destroy; host.m_SoundIsPlay = host_playing;
    host.m_SoundVolume = host_parameter; host.m_SoundPan = host_parameter;
    g_RangersInterface = &host;
    const auto first = CSound::Play(S_BCLICK, 1, 0, SL_INTERFACE, SEF_INTERRUPT);
    MG_CHECK(first != SOUND_ID_EMPTY && host_created == 1 && host_destroyed == 0);
    MG_CHECK(CSound::Play(S_BCLICK, 1, 0, SL_INTERFACE, SEF_SKIP) == SOUND_ID_EMPTY);
    MG_CHECK(host_created == 1 && host_destroyed == 0);
    const auto second = CSound::Play(S_BCLICK, 1, 0, SL_INTERFACE, SEF_INTERRUPT);
    MG_CHECK(second != SOUND_ID_EMPTY && second != first);
    MG_CHECK(host_created == 2 && host_played == 2 && host_destroyed == 1);
    CSound::Clear();
    CSound::Clear();
    MG_CHECK(host_destroyed == 2 && f.device->voices.empty());
    g_RangersInterface = nullptr;
}


#ifdef MATRIXGAME_MUSIC
void music_sessions() {
    StartupFixture fixture;
    fixture.track("music/BATTLE/b.dat", 2000);
    fixture.track("music/BATTLE/A.dat", 1000);
    fixture.track("music/BATTLE/c.dat", 3000);
    fixture.track("music/BATTLE/nested/d.dat", 4000);  // Only files directly in the folder are tracks.
    MG_CHECK(SoundBridge::battle_tracks() ==
             std::vector<std::wstring>({L"music\\BATTLE\\A.dat", L"music\\BATTLE\\b.dat", L"music\\BATTLE\\c.dat"}));
    SoundBridge::music_volume(0.5f);  // Set before the service exists, as at startup.
    SoundBridge::initialize_standalone(true);
    MG_CHECK(SoundBridge::music_available() && startup_device->streams.empty());
    SoundBridge::update_music();
    auto stream = startup_device->streams.at(0);
    MG_CHECK(stream->volume == 0.5f && stream->plays == 1 && stream->buffers.size() == 4);
    MG_CHECK(stream->buffers[0].front() == 1000 && stream->buffers[1].front() == 2000);
    MG_CHECK(stream->buffers[2].front() == 3000 && stream->buffers[3].front() == 1000);
    SoundBridge::music_level(0.5f);  // The engine fade multiplies the listener level.
    MG_CHECK(SoundBridge::music_level() == 0.5f && stream->volume == 0.25f);
    SoundBridge::music_volume(1.0f);
    MG_CHECK(stream->volume == 0.5f);
    SoundBridge::master_volume(0.1f);  // The effects level does not reach music.
    MG_CHECK(stream->volume == 0.5f);
    SoundBridge::master_volume(1.0f);
    // A new session starts after the track that was playing and resets the fade.
    SoundBridge::initialize_standalone(true);
    MG_CHECK(stream->destroyed && SoundBridge::music_level() == 1.0f);
    SoundBridge::update_music();
    stream = startup_device->streams.at(1);
    MG_CHECK(stream->volume == 1.0f && stream->buffers[0].front() == 2000);
    SoundBridge::shutdown();
    MG_CHECK(stream->destroyed && !SoundBridge::music_available());
    SoundBridge::update_music();
    SoundBridge::music_level(0.5f);
    MG_CHECK(startup_device->streams.size() == 2 && SoundBridge::music_level() == 0.0f);
}

void music_unavailable() {
    StartupFixture fixture;
    SoundBridge::initialize_standalone(true);  // Sound packages without battle tracks.
    SoundBridge::update_music();
    MG_CHECK(SoundBridge::available() && !SoundBridge::music_available() && startup_device->streams.empty());
    fixture.track("music/BATTLE/a.dat", 1000);
    SoundBridge::initialize_standalone(false);  // No audio package: no device and no music.
    SoundBridge::update_music();
    MG_CHECK(!SoundBridge::music_available() && device_creations == 1);
    // An undecodable track disables music without affecting effects.
    std::filesystem::remove("music/BATTLE/a.dat");
    fixture.wave("music/BATTLE/a.dat");
    fixture.wave("sound/ButtonClick.wav");
    SoundBridge::initialize_standalone(true);
    SoundBridge::update_music();
    SoundBridge::update_music();
    MG_CHECK(SoundBridge::music_available() && startup_device->streams.empty());
    fixture.play(L"Sound.ButtonClick");
    MG_CHECK(startup_device->voices.size() == 1);
}
#else
// A build without the decoder leaves music off and lists no tracks; effects still play.
void music_not_built() {
    StartupFixture fixture;
    fixture.track("music/BATTLE/a.dat", 1000);
    fixture.wave("sound/ButtonClick.wav");
    SoundBridge::initialize_standalone(true);
    SoundBridge::update_music();
    MG_CHECK(SoundBridge::available() && !SoundBridge::music_available() && startup_device->streams.empty());
    fixture.play(L"Sound.ButtonClick");
    MG_CHECK(startup_device->voices.size() == 1);
}
#endif

float host_music = 0.75f;
float __stdcall host_music_get() { return host_music; }
void __stdcall host_music_set(float value) { host_music = value; }

void host_music_routing() {
    Frontend f;
    SMGDRangersInterface host{};
    host.m_MusicVolumeGet = host_music_get; host.m_MusicVolumeSet = host_music_set;
    g_RangersInterface = &host;
    MG_CHECK(SoundBridge::music_available() && SoundBridge::music_level() == 0.75f);
    SoundBridge::music_level(0.25f);
    SoundBridge::music_volume(0.5f);  // The standalone setting does not change host music.
    MG_CHECK(host_music == 0.25f && SoundBridge::music_level() == 0.25f);
    g_RangersInterface = nullptr;
    SoundBridge::music_volume(1.0f);
    MG_CHECK(!SoundBridge::music_available() && f.device->streams.empty());
}

constexpr tests::Case cases[] = {
    {"game.audio.default_catalog", default_catalog_startup},
    {"game.audio.map_spawner_catalog", map_spawner_catalog_startup},
    {"game.audio.master_level_retained", master_level_retained},
    {"game.audio.mapping_override", partial_mapping_override},
    {"game.audio.mapping_fallback", invalid_mapping_fallback},
    {"game.audio.startup_unavailable", startup_without_packages_or_device},
    {"game.audio.layer_lifetime", layer_lifetime}, {"game.audio.clear_reentrancy", clear_reentrancy},
    {"game.audio.stale_layer_slot", stale_layer_slot}, {"game.audio.host_layer_lifetime", host_layer_lifetime},
    {"game.audio.frontend_missing", missing_mapping}, {"game.audio.host_routing", host_routing},
    {"game.audio.no_optional_packages", no_optional_packages},
#ifdef MATRIXGAME_MUSIC
    {"game.audio.music_sessions", music_sessions}, {"game.audio.music_unavailable", music_unavailable},
#else
    {"game.audio.music_not_built", music_not_built},
#endif
    {"game.audio.host_music_routing", host_music_routing},
};
} // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit(); SetUnhandledExceptionFilter(nullptr);
    const int result = tests::run(argc, argv, cases);
    Base::CMain::BaseDeInit();
    return result;
}
