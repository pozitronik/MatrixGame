// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"
#include "wire_bytes.hpp"

#include "MatrixGame.h"
#include "MatrixGameDll.hpp"
#include "MatrixConfig.hpp"
#include "MatrixLoadProgress.hpp"
#include "MatrixSampleStateManager.hpp"
#include "MatrixSoundManager.hpp"
#include "SoundBridge.hpp"
#include "3g.hpp"

#include <array>
#include <cstring>
#include <string>
#include <vector>

// Production consumers of the original host contract: settings, result statistics, progress and sound
// callbacks. Host callbacks are synthetic recorders; no host process, window or device is involved.

namespace {

using wire::Bytes;

// SRobotsSettings as the original x86 host lays it out: two 32-bit device values, eight one-byte fields,
// then 32-bit integers and IEEE-754 binary32 values, and a final one-byte VSync flag with three padding bytes.
Bytes host_settings(uint8_t robot_shadow) {
    Bytes out;
    out.u32(0x11223344).u32(0x55667788);
    out.u8(1).u8(0).u8(1).u8(0).u8(1).u8(1).u8(2).u8(robot_shadow);
    out.u32(32).u32(1024).u32(768).u32(60);
    out.u32(0x3f000000).u32(0x3f400000);  // brightness 0.5, contrast 0.75
    out.u32(4).u32(8).u32(0x3f800000);    // FSAA, anisotropy, maximum distance 1.0
    out.u8(1).u8(0xcc).u8(0xcc).u8(0xcc);
    return out;
}

SRobotsSettings settings_from(const Bytes &image) {
    MG_CHECK(image.size() == 56 && sizeof(SRobotsSettings) == 56);
    SRobotsSettings settings;
    std::memcpy(&settings, image.data.data(), sizeof(settings));
    return settings;
}

void settings_image() {
    auto settings = settings_from(host_settings(1));
    MG_CHECK(settings.FDirect3D == 0x11223344 && settings.FD3DDevice == 0x55667788);
    MG_CHECK(settings.m_SkyBox == 2 && settings.m_RobotShadow == 1 && settings.m_BPP == 32);
    MG_CHECK(settings.m_ResolutionX == 1024 && settings.m_ResolutionY == 768 && settings.m_RefreshRate == 60);
    MG_CHECK(settings.m_FSAASamples == 4 && settings.m_AFDegree == 8 && settings.m_MaxDistance == 1.0f);
    MG_CHECK(settings.m_VSync);

    // The engine copies host settings into its configuration and sampler state.
    g_Config.ApplySettings(&settings);
    MG_CHECK(g_Config.m_ShowStencilShadows && !g_Config.m_ShowProjShadows && g_Config.m_IzvratMS);
    MG_CHECK(!g_Config.m_LandTexturesGloss && g_Config.m_ObjTexturesGloss && g_Config.m_SoftwareCursor);
    MG_CHECK(g_Config.m_SkyBox == 2 && g_Config.m_RobotShadow == SHADOW_STENCIL);
    MG_CHECK(g_Config.m_GammaR.brightness == 0.5f && g_Config.m_GammaR.contrast == 0.75f);
    MG_CHECK(g_Config.m_GammaR.gamma == 1.0f && g_Config.m_GammaB.contrast == 0.75f);
    auto disabled = settings_from(host_settings(0));
    g_Config.ApplySettings(&disabled);
    MG_CHECK(g_Config.m_RobotShadow == SHADOW_OFF);

    // Requested anisotropy is limited by the device capability.
    g_D3DDCaps.MaxAnisotropy = 16;
    g_Sampler.ApplySettings(&settings);
    MG_CHECK(g_Sampler.m_AFDegree == 8);
    g_D3DDCaps.MaxAnisotropy = 2;
    g_Sampler.ApplySettings(&settings);
    MG_CHECK(g_Sampler.m_AFDegree == 2);
    g_D3DDCaps.MaxAnisotropy = 0;
}

void result_statistics() {
    tests::CommandWorld world;
    auto *side = world.map->GetPlayerSide();
    side->SetStatValue(STAT_TIME, 61000);
    side->SetStatValue(STAT_ROBOT_BUILD, 2);
    side->SetStatValue(STAT_ROBOT_KILL, 3);
    side->SetStatValue(STAT_TURRET_BUILD, 4);
    side->SetStatValue(STAT_TURRET_KILL, 5);
    side->SetStatValue(STAT_BUILDING_KILL, 6);
    // The host owns the 24-byte result; the engine writes six 32-bit fields and nothing beyond them.
    alignas(SRobotGameState) std::array<uint8_t, 32> host{};
    host.fill(0xa5);
    CGame::SaveResult(reinterpret_cast<SRobotGameState *>(host.data()));
    const Bytes expected = Bytes().u32(61000).u32(2).u32(3).u32(4).u32(5).u32(6);
    MG_CHECK(std::equal(expected.data.begin(), expected.data.end(), host.begin()));
    for (size_t i = 24; i < host.size(); ++i) MG_CHECK(host[i] == 0xa5);
}

std::vector<float> progress;
void __stdcall record_progress(float value) { progress.push_back(value); }

void progress_callback() {
    // Standalone sessions have no host callback table.
    g_RangersInterface = nullptr;
    {
        CLoadProgress standalone;
        standalone.SetCurLP(0);
        standalone.InitCurLP(10);
        standalone.SetCurLPPos(10);
    }
    SMGDRangersInterface host{};
    host.m_ProgressBar = record_progress;
    g_RangersInterface = &host;
    CLoadProgress load;
    for (int phase = 0; phase < 4; ++phase) {
        load.SetCurLP(phase);
        load.InitCurLP(250);
        for (int position = 0; position <= 250; ++position) load.SetCurLPPos(position);
    }
    g_RangersInterface = nullptr;
    // Reports are strictly increasing multiples of 1/100 and the final phase reaches 1.0.
    MG_CHECK(!progress.empty() && progress.size() <= 100);
    for (size_t i = 0; i < progress.size(); ++i) {
        MG_CHECK(progress[i] > 0.0f && progress[i] <= 1.0f);
        if (i) MG_CHECK(progress[i] > progress[i - 1]);
    }
    MG_CHECK(progress.back() == 1.0f);

    // Session initialization's final phase stops one step short of its 701-step size; rounding to
    // hundredths still reports completion.
    progress.clear();
    g_RangersInterface = &host;
    CLoadProgress session;
    for (int phase = 0; phase < 3; ++phase) {
        session.SetCurLP(phase);
        session.InitCurLP(1);
    }
    session.SetCurLP(3);
    session.InitCurLP(701);
    for (int position : {10, 100, 200, 300, 400, 500, 600, 700}) session.SetCurLPPos(position);
    g_RangersInterface = nullptr;
    MG_CHECK(!progress.empty() && progress.back() == 1.0f);
    for (size_t i = 1; i < progress.size(); ++i) MG_CHECK(progress[i] > progress[i - 1]);
}

struct SoundCall {
    std::string kind;
    std::wstring path;
    dword id;
    int group;
    int loop;
    uint32_t value;
};
std::vector<SoundCall> sound_calls;
dword next_sound_id;

uint32_t bits(float value) {
    uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

dword __stdcall sound_create(wchar *path, int group, int loop) {
    // The path is borrowed for the duration of the call.
    sound_calls.push_back({"create", path, 0, group, loop, 0});
    return next_sound_id;
}
void __stdcall sound_destroy(dword id) { sound_calls.push_back({"destroy", L"", id, 0, 0, 0}); }
void __stdcall sound_play(dword id) { sound_calls.push_back({"play", L"", id, 0, 0, 0}); }
int __stdcall sound_is_play(dword id) {
    sound_calls.push_back({"is_play", L"", id, 0, 0, 0});
    return 2;
}
void __stdcall sound_volume(dword id, float value) { sound_calls.push_back({"volume", L"", id, 0, 0, bits(value)}); }
void __stdcall sound_pan(dword id, float value) { sound_calls.push_back({"pan", L"", id, 0, 0, bits(value)}); }
float __stdcall sound_get_volume(dword id) {
    sound_calls.push_back({"get_volume", L"", id, 0, 0, 0});
    return 0.375f;
}

SMGDRangersInterface sound_host() {
    SMGDRangersInterface host{};
    host.m_SoundCreate = sound_create;
    host.m_SoundDestroy = sound_destroy;
    host.m_SoundPlay = sound_play;
    host.m_SoundIsPlay = sound_is_play;
    host.m_SoundVolume = sound_volume;
    host.m_SoundPan = sound_pan;
    host.m_SoundGetVolume = sound_get_volume;
    return host;
}

struct SoundData {
    CBlockPar data;
    SoundData() {
        g_MatrixData = &data;
        auto *event = data.BlockGetAdd(L"Sounds")->BlockGetAdd(L"bclick");
        event->ParSetAdd(L"path", L"Sound.Click");
        event->ParSetAdd(L"looped", L"1");
        CSound::Init();
    }
    ~SoundData() {
        g_RangersInterface = nullptr;
        CSound::Clear();
        g_MatrixData = nullptr;
    }
};

void sound_arguments() {
    SoundData fixture;
    auto host = sound_host();
    g_RangersInterface = &host;
    sound_calls.clear();
    next_sound_id = 0x80000001;  // all 32 bits of the host identifier are preserved

    wchar_t name[] = L"Sound.Direct";
    const uint32_t id = SoundBridge::create(name, 7, 0);
    SoundBridge::volume(id, 0.123456789f);
    SoundBridge::pan(id, -1.0f);
    MG_CHECK(SoundBridge::volume(id) == 0.375f && SoundBridge::playing(id));
    SoundBridge::destroy(id);
    MG_CHECK(id == 0x80000001 && sound_calls.size() == 6);
    MG_CHECK(sound_calls[0].kind == "create" && sound_calls[0].path == L"Sound.Direct");
    MG_CHECK(sound_calls[0].group == 7 && sound_calls[0].loop == 0);
    MG_CHECK(sound_calls[1].kind == "volume" && sound_calls[1].value == bits(0.123456789f));
    MG_CHECK(sound_calls[2].kind == "pan" && sound_calls[2].value == 0xbf800000);
    MG_CHECK(sound_calls[3].kind == "get_volume" && sound_calls[4].kind == "is_play");
    MG_CHECK(sound_calls[5].kind == "destroy");
    for (const auto &call : sound_calls) {
        if (call.kind != "create") MG_CHECK(call.id == 0x80000001);
    }

    // Engine playback creates the host sound, then sets pan, volume and starts it; the group counter
    // starts at zero after initialization and the looped flag is passed as one.
    sound_calls.clear();
    next_sound_id = 41;
    const auto played = CSound::Play(S_BCLICK, 0.5f, -0.25f, SL_ALL);
    MG_CHECK(played != SOUND_ID_EMPTY && sound_calls.size() == 4);
    MG_CHECK(sound_calls[0].kind == "create" && sound_calls[0].path == L"Sound.Click");
    MG_CHECK(sound_calls[0].group == 0 && sound_calls[0].loop == 1);
    MG_CHECK(sound_calls[1].kind == "pan" && sound_calls[1].id == 41 && sound_calls[1].value == bits(-0.25f));
    MG_CHECK(sound_calls[2].kind == "volume" && sound_calls[2].id == 41 && sound_calls[2].value == bits(0.5f));
    MG_CHECK(sound_calls[3].kind == "play" && sound_calls[3].id == 41);

    // A zero host identifier rejects the sound without further callbacks for it.
    sound_calls.clear();
    next_sound_id = 0;
    MG_CHECK(CSound::Play(S_BCLICK, 0.5f, 0.0f, SL_ALL) == SOUND_ID_EMPTY);
    for (const auto &call : sound_calls) MG_CHECK(call.kind == "create" || call.kind == "is_play");
    MG_CHECK(sound_calls.back().kind == "create" && sound_calls.back().group == 1);

    // Clearing releases the remaining host sound.
    sound_calls.clear();
    CSound::Clear();
    MG_CHECK(sound_calls.size() == 1 && sound_calls[0].kind == "destroy" && sound_calls[0].id == 41);
    g_RangersInterface = nullptr;
}

constexpr tests::Case cases[] = {
    {"game.host.settings_image", settings_image},
    {"game.host.result_statistics", result_statistics},
    {"game.host.progress_callback", progress_callback},
    {"game.host.sound_arguments", sound_arguments},
};

}  // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "Host contract test leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
