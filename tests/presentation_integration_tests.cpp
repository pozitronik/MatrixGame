// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "command_world.hpp"
#include "audio_fixture.hpp"
#include "SoundBridge.hpp"
#include "MatrixSoundManager.hpp"
#include "Interface/CIFaceButton.h"
#include "Interface/CInterface.h"

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

constexpr tests::Case cases[]{
    {"game.presentation.push_sounds", push_sound_routes},
    {"game.presentation.preset_matching", preset_name_matching},
    {"game.presentation.disabled", disabled_and_unfocused},
    {"game.presentation.optional_audio", optional_audio_command},
    {"game.presentation.interface_layer", interface_layer_replacement},
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
