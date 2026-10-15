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
#include "MatrixFormGame.hpp"
#include "input.hpp"
#include <array>
#include <cstring>
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

void textured_button_hit_testing() {
    tests::battle::Session session;
    auto *texture = static_cast<CTextureManaged *>(g_Cache->Get(CacheClass::TextureManaged, L"synthetic-ui-atlas"));
    D3DLOCKED_RECT locked{};
    MG_CHECK(texture->CreateLock(D3DFMT_A8R8G8B8, 4, 4, 1, locked) == D3D_OK);
    std::array<DWORD, 16> pixels{};
    pixels[5] = 0x00123456; // Transparent RGB must not catch the pointer.
    pixels[6] = 0x01123456; // Any nonzero alpha is eligible, including one.
    pixels[9] = pixels[10] = 0xff123456;
    for (int y = 0; y < 4; ++y)
        std::memcpy(static_cast<BYTE *>(locked.pBits) + y * locked.Pitch,
                    pixels.data() + y * 4, 4 * sizeof(DWORD));
    texture->UnlockRect();
    texture->Preload();

    std::vector<std::string> events;
    CInterface panel;
    panel.m_VisibleAlpha = IS_VISIBLEA;
    panel.m_xPos = 100;
    panel.m_yPos = 200;
    auto *button = HNew(g_MatrixHeap) CIFaceButton;
    button->m_strName = L"synthetic-hit-test";
    button->m_xSize = button->m_ySize = 2;
    for (auto state : {IFACE_NORMAL, IFACE_FOCUSED, IFACE_PRESSED, IFACE_DISABLED})
        MG_CHECK(button->SetStateImage(state, texture, 1, 1, 4, 4));
    button->ElementGeomInit(button);
    panel.AddElement(button);
    button->SetVisibility(true);
    button->m_Actions[ON_FOCUS].m_function = [&](void *) { events.push_back("focus"); };
    button->m_Actions[ON_PRESS].m_function = [&](void *) { events.push_back("press"); };
    button->m_Actions[ON_UN_PRESS].m_function = [&](void *) { events.push_back("release"); };
    MG_CHECK(button->ElementCatch(CPoint(100, 200)));
    MG_CHECK(!panel.OnMouseMove(CPoint(100, 200)));
    MG_CHECK(button->GetState() == IFACE_NORMAL && !g_IFaceList->m_FocusedElement);
    g_MatrixMap->m_Cursor.SetPos(100, 200);
    MG_CHECK(!panel.OnMouseLBDown() && events.empty());
    MG_CHECK(panel.OnMouseMove(CPoint(101, 200)));
    MG_CHECK(g_IFaceList->m_FocusedElement == button && button->GetState() == IFACE_FOCUSED);
    // Keep the production focus state, but click a transparent atlas texel.
    g_MatrixMap->m_Cursor.SetPos(100, 200);
    MG_CHECK(!panel.OnMouseLBDown());
    MG_CHECK((events == std::vector<std::string>{"focus"}));
    MG_CHECK(button->GetState() == IFACE_FOCUSED);
    g_MatrixMap->m_Cursor.SetPos(101, 200);
    MG_CHECK(panel.OnMouseLBDown());
    g_MatrixMap->m_Cursor.SetPos(100, 200);
    panel.OnMouseLBUp();
    MG_CHECK((events == std::vector<std::string>{"focus", "press"}));
    MG_CHECK(button->GetState() == IFACE_PRESSED);
    // Observed behavior: a transparent release does not dispatch or unpress.
    g_MatrixMap->m_Cursor.SetPos(101, 200);
    panel.OnMouseLBUp();
    MG_CHECK((events == std::vector<std::string>{"focus", "press", "release"}));
    MG_CHECK(!panel.OnMouseMove(CPoint(102, 200)));
    MG_CHECK(button->GetState() == IFACE_NORMAL && !g_IFaceList->m_FocusedElement);
    MG_CHECK(!panel.OnMouseMove(CPoint(101, 202)));
    MG_CHECK(panel.OnMouseMove(CPoint(100, 201)));
    button->SetState(IFACE_DISABLED);
    const auto event_count = events.size();
    MG_CHECK(panel.OnMouseMove(CPoint(101, 201)));
    g_MatrixMap->m_Cursor.SetPos(101, 201);
    MG_CHECK(panel.OnMouseLBDown());
    panel.OnMouseLBUp();
    MG_CHECK(button->GetState() == IFACE_DISABLED && events.size() == event_count);
    button->SetVisibility(false);
    MG_CHECK(!panel.OnMouseMove(CPoint(101, 201)));
    MG_CHECK(!g_IFaceList->m_FocusedElement);
    MG_CHECK(!panel.OnMouseLBDown() && events.size() == event_count);
}

void update_camera() {
    g_MatrixMap->m_Camera.Takt(0);
    g_MatrixMap->m_Camera.BeforeDraw();
}
float camera_distance() {
    const auto &camera = g_MatrixMap->m_Camera;
    const auto &inverse = camera.GetViewInversedMatrix();
    const auto delta = D3DXVECTOR3(inverse._41, inverse._42, inverse._43) - camera.GetLinkPoint();
    return D3DXVec3Length(&delta);
}
void camera_wheel_limits() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        tests::battle::Session session({mode, 87, 10, 10, false});
        CFormMatrixGame form;
        std::cout << session.metadata("synthetic-camera-wheel-v1") << '\n';
        const auto draws = random::draw_count();
        MG_CHECK(std::abs(camera_distance() - 250) < 0.001f);
        form.MouseKey(B_WHEEL, 1, 640, 360);
        update_camera();
        MG_CHECK(std::abs(camera_distance() - 209.5f) < 0.001f);
        form.MouseKey(B_WHEEL, -1, 640, 360);
        update_camera();
        MG_CHECK(std::abs(camera_distance() - 250) < 0.001f);
        form.MouseKey(B_WHEEL, 0, 640, 360);
        update_camera();
        MG_CHECK(std::abs(camera_distance() - 250) < 0.001f);
        for (int repeat = 0; repeat < 2; ++repeat) {
            form.MouseKey(B_WHEEL, 100, 640, 360);
            update_camera();
            MG_CHECK(std::abs(camera_distance() - 115) < 0.001f);
        }
        form.MouseKey(B_WHEEL, -100, 640, 360);
        update_camera();
        MG_CHECK(std::abs(camera_distance() - 790) < 0.001f);
        form.MouseKey(B_WHEEL, -1, 640, 360);
        update_camera();
        MG_CHECK(std::abs(camera_distance() - 790) < 0.001f);
        // Reset restores orientation but deliberately preserves wheel distance.
        form.Keyboard(true, g_Config.m_KeyActions[KA_CAM_SETDEFAULT]);
        form.Keyboard(false, g_Config.m_KeyActions[KA_CAM_SETDEFAULT]);
        update_camera();
        MG_CHECK(std::abs(camera_distance() - 790) < 0.001f);
        MG_CHECK(random::draw_count() == draws && session.time() == 0);
    }
}
void camera_drag_release() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        tests::battle::Session session({mode, 87, 10, 10, false});
        CFormMatrixGame form;
        auto &camera = g_MatrixMap->m_Camera;
        const float original_x = camera.GetAngleX(), original_z = camera.GetAngleZ();
        const auto draws = random::draw_count();
        std::cout << session.metadata("synthetic-camera-drag-v1") << '\n';
        form.MouseKey(B_DOWN, VK_MBUTTON, 640, 360);
        MG_CHECK(g_MatrixMap->IsMouseCam() && Input::isVKeyPressed(VK_MBUTTON));
        form.MouseMove(660, 380);
        update_camera();
        MG_CHECK(std::abs(camera.GetAngleZ() - (original_z + 0.2f)) < 0.00001f);
        MG_CHECK(std::abs(camera.GetAngleX() - GRAD2RAD(46.0f)) < 0.00001f);
        MG_CHECK(g_MatrixMap->m_Cursor.GetPos() == CPoint(660, 380));
        form.MouseMove(660, 10000);
        update_camera();
        MG_CHECK(camera.GetAngleX() == g_Config.m_CamParams[CAMERA_STRATEGY].m_CamRotAngleMin);
        form.MouseMove(660, -10000);
        update_camera();
        MG_CHECK(camera.GetAngleX() == g_Config.m_CamParams[CAMERA_STRATEGY].m_CamRotAngleMax);
        form.MouseKey(B_UP, VK_MBUTTON, 660, -10000);
        MG_CHECK(!g_MatrixMap->IsMouseCam() && !Input::isVKeyPressed(VK_MBUTTON));
        const float x = camera.GetAngleX(), z = camera.GetAngleZ();
        form.MouseMove(640, 360);
        update_camera();
        MG_CHECK(camera.GetAngleX() == x && camera.GetAngleZ() == z);
        form.Keyboard(true, g_Config.m_KeyActions[KA_CAM_SETDEFAULT]);
        form.Keyboard(false, g_Config.m_KeyActions[KA_CAM_SETDEFAULT]);
        update_camera();
        MG_CHECK(camera.GetAngleX() == original_x && camera.GetAngleZ() == original_z);
        MG_CHECK(random::draw_count() == draws && session.time() == 0);
    }
}
void camera_input_interruption() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (auto event : {SYSEV_DEACTIVATING, SYSEV_INPUT_RESET, SYSEV_POINTER_LEFT}) {
        tests::battle::Session session({mode, 87, 10, 10, false});
        CFormMatrixGame form;
        auto &camera = g_MatrixMap->m_Camera;
        std::cout << session.metadata("synthetic-camera-interruption-v1") << " event=" << int(event) << '\n';
        form.MouseKey(B_DOWN, VK_MBUTTON, 640, 360);
        form.MouseMove(660, 380);
        update_camera();
        const float x = camera.GetAngleX(), z = camera.GetAngleZ();
        form.SystemEvent(event);
        MG_CHECK(!g_MatrixMap->IsMouseCam());
        // Pointer leave cancels rotation but does not synthesize a key release.
        MG_CHECK(Input::isVKeyPressed(VK_MBUTTON) == (event == SYSEV_POINTER_LEFT));
        if (event == SYSEV_POINTER_LEFT) MG_CHECK(g_MatrixMap->m_Cursor.GetPos() == CPoint(-1, -1));
        form.MouseMove(680, 400);
        form.MouseKey(B_UP, VK_MBUTTON, 680, 400);
        update_camera();
        MG_CHECK(!Input::isVKeyPressed(VK_MBUTTON));
        MG_CHECK(camera.GetAngleX() == x && camera.GetAngleZ() == z);
        form.MouseKey(B_DOWN, VK_MBUTTON, 680, 400);
        form.MouseMove(700, 420);
        update_camera();
        MG_CHECK(camera.GetAngleZ() > z && camera.GetAngleX() > x);
        form.MouseKey(B_UP, VK_MBUTTON, 700, 420);
        MG_CHECK(!g_MatrixMap->IsMouseCam());
    }
}
constexpr tests::Case cases[]{
    {"game.presentation.camera_wheel_limits", camera_wheel_limits},
    {"game.presentation.camera_drag_release", camera_drag_release},
    {"game.presentation.camera_input_interruption", camera_input_interruption},
    {"game.presentation.textured_button_hit_testing", textured_button_hit_testing},
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
