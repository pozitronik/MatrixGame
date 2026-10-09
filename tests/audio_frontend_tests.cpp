// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "audio_fixture.hpp"
#include "SoundBridge.hpp"
#include "MatrixSoundManager.hpp"
#include "MatrixGameDll.hpp"
#include "MatrixMap.hpp"

namespace {
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
    {"game.audio.layer_lifetime", layer_lifetime}, {"game.audio.clear_reentrancy", clear_reentrancy},
    {"game.audio.stale_layer_slot", stale_layer_slot}, {"game.audio.host_layer_lifetime", host_layer_lifetime},
    {"game.audio.frontend_missing", missing_mapping}, {"game.audio.host_routing", host_routing},
};
} // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit(); SetUnhandledExceptionFilter(nullptr);
    const int result = tests::run(argc, argv, cases);
    Base::CMain::BaseDeInit();
    return result;
}
