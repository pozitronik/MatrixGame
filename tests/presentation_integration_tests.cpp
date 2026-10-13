// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"
#include "audio_fixture.hpp"
#include "SoundBridge.hpp"
#include "MatrixSoundManager.hpp"
#include "Interface/CIFaceButton.h"
#include "Interface/CInterface.h"
#include "battle_scenario.hpp"
#include "MatrixRobot.hpp"
#include <cmath>

namespace {
struct Buttons {
    tests::CommandWorld world;
    std::shared_ptr<audio_fixture::DeviceState> device = std::make_shared<audio_fixture::DeviceState>();
    std::vector<std::wstring> events;
    Buttons(bool audio = true) {
        g_Config.SetDefaults();
        CSound::Init();
        Audio::Mapping mapping;
        for (const auto *name : {L"bclick", L"preset_click", L"build_click", L"cancel_click"}) {
            world.data.BlockGetAdd(L"Sounds")->BlockGetAdd(name)->ParSetAdd(L"path", name);
            mapping.emplace(name, name);
        }
        if (audio) {
            SoundBridge::install(std::make_unique<Audio::Service>(std::make_unique<audio_fixture::FakeDevice>(device),
                std::move(mapping), [this](const std::wstring &path) {
                    events.push_back(path);
                    return audio_fixture::wave();
                }, [](const std::wstring &, const std::string &error) { throw std::runtime_error(error); }));
        }
    }
    ~Buttons() { CSound::Clear(); SoundBridge::shutdown(); }
    void prepare(CIFaceButton &button, const wchar_t *name, IFaceElementType type) {
        button.m_strName = name;
        button.m_Type = type;
        button.SetState(IFACE_FOCUSED);
        button.m_Actions[ON_PRESS].m_function = [this, &button](void *sender) {
            MG_CHECK(sender == &button && button.GetState() == IFACE_PRESSED);
            events.push_back(L"press");
        };
        button.m_Actions[ON_UN_PRESS].m_function = [this](void *) { events.push_back(L"release"); };
    }
};

void push_sound_routes() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        const std::pair<const wchar_t *, const wchar_t *> routes[]{
            {L"ordinary", L"bclick"}, {IF_BASE_CONST_BUILD, L"build_click"}, {IF_BASE_CONST_CANCEL, L"cancel_click"}};
        for (const auto &[name, sound] : routes) {
            Buttons f;
            CIFaceButton button;
            f.prepare(button, name, IFACE_PUSH_BUTTON);
            random::seed(87, mode);
            MG_CHECK(button.OnMouseLBDown());
            MG_CHECK((f.events == std::vector<std::wstring>{L"press", sound}));
            MG_CHECK(f.device->voices.size() == 1 && f.device->voices.front()->playing);
            MG_CHECK(random::draw_count() == 2);
            button.OnMouseLBUp();
            MG_CHECK(button.GetState() == IFACE_FOCUSED && f.events.back() == L"release");
            MG_CHECK(random::draw_count() == 2);
        }
    }
}

void preset_name_matching() {
    const std::pair<const wchar_t *, const wchar_t *> routes[]{
        {L"conf0", L"bclick"}, {L"xconf0", L"preset_click"}, {L"ordinary", L"preset_click"}};
    for (const auto &[name, sound] : routes) {
        Buttons f;
        CIFaceButton button;
        f.prepare(button, name, IFACE_CHECK_PUSH_BUTTON);
        MG_CHECK(button.OnMouseLBDown());
        MG_CHECK((f.events == std::vector<std::wstring>{L"press", sound}));
        button.OnMouseLBUp();
        MG_CHECK(button.GetState() == IFACE_PRESSED_UNFOCUSED);
        const auto count = f.events.size();
        MG_CHECK(!button.OnMouseLBDown());
        MG_CHECK(f.events.size() == count && f.device->voices.size() == 1);
    }
}

void disabled_and_unfocused() {
    Buttons f;
    CIFaceButton button;
    f.prepare(button, L"ordinary", IFACE_PUSH_BUTTON);
    random::seed(87);
    button.SetState(IFACE_DISABLED);
    MG_CHECK(button.OnMouseLBDown());
    button.OnMouseLBUp();
    MG_CHECK(button.GetState() == IFACE_DISABLED);
    button.SetState(IFACE_NORMAL);
    MG_CHECK(!button.OnMouseLBDown());
    button.OnMouseLBUp();
    MG_CHECK(button.GetState() == IFACE_NORMAL);
    MG_CHECK(f.events.empty() && f.device->voices.empty() && random::draw_count() == 0);
}

void optional_audio_command() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Buttons f(false);
        CIFaceButton button;
        f.prepare(button, IF_BASE_CONST_BUILD, IFACE_PUSH_BUTTON);
        random::seed(87, mode);
        MG_CHECK(button.OnMouseLBDown());
        button.OnMouseLBUp();
        MG_CHECK((f.events == std::vector<std::wstring>{L"press", L"release"}));
        MG_CHECK(button.GetState() == IFACE_FOCUSED);
        MG_CHECK(f.device->voices.empty() && random::draw_count() == 0);
    }
}

void interface_layer_replacement() {
    Buttons f;
    CIFaceButton first, second;
    f.prepare(first, IF_BASE_CONST_BUILD, IFACE_PUSH_BUTTON);
    f.prepare(second, IF_BASE_CONST_CANCEL, IFACE_PUSH_BUTTON);
    MG_CHECK(first.OnMouseLBDown());
    MG_CHECK(f.device->voices.size() == 1 && f.device->voices[0]->playing);
    MG_CHECK(second.OnMouseLBDown());
    MG_CHECK(f.device->voices.size() == 2);
    MG_CHECK(!f.device->voices[0]->playing && f.device->voices[0]->destroyed == 1);
    MG_CHECK(f.device->voices[1]->playing);
    CSound::Clear();
    MG_CHECK(!f.device->voices[1]->playing && f.device->voices[1]->destroyed == 1);
}

struct BattleSound {
    tests::battle::Session session;
    std::shared_ptr<audio_fixture::DeviceState> device = std::make_shared<audio_fixture::DeviceState>();
    std::vector<std::wstring> loaded;
    std::vector<std::wstring> reported;
    explicit BattleSound(random::Mode mode, bool enabled = true, bool speech = true)
      : session({mode, 87, 10, 1000, false, true}) {
        CSound::Init();
        Audio::Mapping mapping;
        for (const auto *name : {L"Sound.Synthetic", L"s_ord_attack", L"s_ord_inprogress1",
                                 L"s_ord_inprogress2", L"s_orda_defence", L"wlaser"}) {
            g_MatrixData->BlockGetAdd(L"Sounds")->BlockGetAdd(name)->ParSetAdd(L"path", name);
            if (speech || std::wstring_view(name) == L"wlaser") mapping.emplace(name, name);
        }
        auto *loop = g_MatrixData->BlockGetAdd(L"Sounds")->BlockGetAdd(L"wlaser");
        loop->ParSetAdd(L"looped", L"1");
        loop->ParSetAdd(L"pan", L"-1,1");
        loop->ParSetAdd(L"vol", L"0,1");
        loop->ParSetAdd(L"attn", L"0.5"); // Production scales this by 0.002.
        if (enabled)
            SoundBridge::install(std::make_unique<Audio::Service>(std::make_unique<audio_fixture::FakeDevice>(device),
                std::move(mapping), [this](const std::wstring &path) {
                    loaded.push_back(path);
                    return audio_fixture::wave();
                }, [this](const std::wstring &name, const std::string &) { reported.push_back(name); }));
    }
    ~BattleSound() { CSound::Clear(); SoundBridge::shutdown(); }
};

void battle_acknowledgements() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleSound sound(mode);
        const auto before = random::draw_count();
        sound.session.ground_command(500, 300);
        MG_CHECK(sound.loaded == std::vector<std::wstring>{L"Sound.Synthetic"});
        MG_CHECK(sound.device->voices.size() == 1 && sound.device->voices[0]->playing);
        MG_CHECK(random::draw_count() == before + 3);
        MG_CHECK(sound.session.robot(1).order == mpo_MoveTo);
        sound.session.ground_command(480, 300);
        MG_CHECK(sound.device->voices.size() == 2);
        MG_CHECK(!sound.device->voices[0]->playing && sound.device->voices[0]->destroyed == 1);
        // Context-command dispatch rebuilds the logic group before replacing
        // its order, so it uses the chassis acknowledgement again.
        MG_CHECK(sound.loaded == std::vector<std::wstring>{L"Sound.Synthetic"});
        MG_CHECK(random::draw_count() == before + 6);
        g_MatrixMap->GetPlayerSide()->PGOrderMoveTo(sound.session.robot(1).group, CPoint(46, 28));
        std::wcout << L"acknowledgement " << static_cast<int>(mode) << L' ' << sound.loaded.back() << L'\n';
        MG_CHECK(sound.loaded.back() == (mode == random::Mode::ParkMiller ? L"s_ord_inprogress1" : L"s_ord_inprogress2"));
        MG_CHECK(random::draw_count() == before + 9);
        MG_CHECK(sound.device->voices.size() == 3 && sound.device->voices[1]->destroyed == 1);
        MG_CHECK(sound.session.target_command(3));
        MG_CHECK(sound.loaded.back() == L"s_ord_attack");
        MG_CHECK(sound.device->voices.size() == 4 && sound.device->voices.back()->playing);
        MG_CHECK(sound.device->voices[2]->destroyed == 1);
        MG_CHECK(sound.session.robot(1).order == mpo_Attack && sound.session.robot(1).target == 3);
        for (int i = 0; i < 30; ++i) sound.session.advance();
        MG_CHECK(sound.session.robot(1).x > 100);
        CSound::Clear();
        for (const auto &voice : sound.device->voices) MG_CHECK(voice->destroyed == 1);
        MG_CHECK(sound.reported.empty());
    }
}

void silent_battle_commands() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleSound sound(mode, false);
        const auto before = random::draw_count();
        sound.session.ground_command(500, 300);
        MG_CHECK(random::draw_count() == before + 1); // Chassis acknowledgement choice still occurs.
        MG_CHECK(sound.session.robot(1).order == mpo_MoveTo);
        sound.session.ground_command(480, 300);
        MG_CHECK(random::draw_count() == before + 2); // Replacement still chooses a chassis acknowledgement.
        MG_CHECK(sound.session.target_command(3));
        MG_CHECK(sound.session.robot(1).target == 3);
        MG_CHECK(sound.loaded.empty() && sound.device->voices.empty());
        for (int i = 0; i < 30; ++i) sound.session.advance();
        MG_CHECK(sound.session.robot(1).x > 100);
    }
}

void positional_loop() {
    BattleSound sound(random::Mode::ParkMiller);
    const auto center = g_MatrixMap->m_Camera.GetFrustumCenter();
    const auto right = g_MatrixMap->m_Camera.GetRight();
    const auto before = random::draw_count();
    auto id = CSound::Play(S_WEAPON_LASER, center + right * 100, SL_ALL);
    MG_CHECK(id != SOUND_ID_EMPTY && sound.device->voices.size() == 1);
    const auto voice = sound.device->voices.front();
    MG_CHECK(voice->loop && voice->playing && voice->volume > 0);
    MG_CHECK(voice->pan > 0.99f);
    const auto updated = CSound::Play(id, S_WEAPON_LASER, center - right * 100, SL_ALL);
    MG_CHECK(updated == id && sound.device->voices.size() == 1);
    MG_CHECK(voice->pan < -0.99f);
    CSound::ChangePos(id, S_WEAPON_LASER, center + right * 2000);
    MG_CHECK(voice->volume == 0 && voice->playing);
    MG_CHECK(random::draw_count() == before);
    CSound::StopPlay(id);
    MG_CHECK(!voice->playing && voice->destroyed == 1);
    MG_CHECK(CSound::ChangePos(id, S_WEAPON_LASER, center) == SOUND_ID_EMPTY);
    MG_CHECK(sound.reported.empty());
}

void missing_speech_mapping() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleSound sound(mode, true, false);
        const auto before = random::draw_count();
        sound.session.ground_command(500, 300);
        sound.session.ground_command(480, 300);
        MG_CHECK(sound.session.robot(1).order == mpo_MoveTo);
        MG_CHECK(sound.device->voices.empty() && sound.loaded.empty());
        MG_CHECK(sound.reported == std::vector<std::wstring>{L"Sound.Synthetic"});
        // A live backend still takes the audio-path draws even when speech is missing.
        MG_CHECK(random::draw_count() == before + 6);
        const auto id = CSound::Play(S_WEAPON_LASER, g_MatrixMap->m_Camera.GetFrustumCenter());
        MG_CHECK(id != SOUND_ID_EMPTY && sound.device->voices.size() == 1);
        MG_CHECK(sound.loaded == std::vector<std::wstring>{L"wlaser"});
        for (int i = 0; i < 30; ++i) sound.session.advance();
        MG_CHECK(sound.session.robot(1).x > 100);
    }
}

constexpr tests::Case cases[]{
    {"game.presentation.push_sounds", push_sound_routes},
    {"game.presentation.preset_matching", preset_name_matching},
    {"game.presentation.disabled", disabled_and_unfocused},
    {"game.presentation.optional_audio", optional_audio_command},
    {"game.presentation.interface_layer", interface_layer_replacement},
    {"game.presentation.battle_acknowledgements", battle_acknowledgements},
    {"game.presentation.silent_battle_commands", silent_battle_commands},
    {"game.presentation.positional_loop", positional_loop},
    {"game.presentation.missing_speech", missing_speech_mapping},
};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
