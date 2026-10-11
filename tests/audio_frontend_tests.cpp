// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "audio_fixture.hpp"
#include "SoundBridge.hpp"
#include "MatrixSoundManager.hpp"
#include "MatrixGameDll.hpp"
#include "MatrixMap.hpp"
#include <filesystem>
#include <fstream>

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

constexpr tests::Case cases[] = {
    {"game.audio.default_catalog", default_catalog_startup},
    {"game.audio.mapping_override", partial_mapping_override},
    {"game.audio.mapping_fallback", invalid_mapping_fallback},
    {"game.audio.startup_unavailable", startup_without_packages_or_device},
    {"game.audio.layer_lifetime", layer_lifetime}, {"game.audio.clear_reentrancy", clear_reentrancy},
    {"game.audio.stale_layer_slot", stale_layer_slot}, {"game.audio.host_layer_lifetime", host_layer_lifetime},
    {"game.audio.frontend_missing", missing_mapping}, {"game.audio.host_routing", host_routing},
    {"game.audio.no_optional_packages", no_optional_packages},
};
} // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit(); SetUnhandledExceptionFilter(nullptr);
    const int result = tests::run(argc, argv, cases);
    Base::CMain::BaseDeInit();
    return result;
}
