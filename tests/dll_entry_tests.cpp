// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "wire_bytes.hpp"
#include "MatrixGame.h"
#include "MatrixFormGame.hpp"
#include "3g.hpp"
#include "stupid_logger.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <type_traits>
#include <vector>

// Link the production DLL entry point, with deterministic session operations.
IDirect3D9 *g_D3D = nullptr;
DWORD g_Flags = 0;
logger_type lgr{std::cerr};

namespace {

enum class Failure { none, engine_init, standard_init, unknown_init, form, loop, save };
Failure failure;
std::vector<int> events;
bool end_timer;
int exit_state;
bool exit_flag;

// Arguments received by the session operations, recorded to check forwarding from the entry point.
struct InitArguments {
    HINSTANCE instance;
    HWND window;
    const wchar *map;
    uint32_t seed;
    const SRobotsSettings *settings;
    const wchar *lang, *start, *win, *loss, *planet;
} init_arguments;
SRobotGameState *saved_result;

class FixtureDirectory {
    std::filesystem::path previous_ = std::filesystem::current_path();
    std::filesystem::path path_;

public:
    FixtureDirectory() {
        std::array<wchar_t, 32768> executable{};
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
        MG_CHECK(length > 0 && length < executable.size());
        path_ = std::filesystem::path(executable.data()).parent_path() /
                ("matrixgame-dll-entry-" + std::to_string(GetCurrentProcessId()));
        MG_CHECK(std::filesystem::create_directory(path_));
        std::filesystem::current_path(path_);
    }
    ~FixtureDirectory() {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
        std::filesystem::remove_all(path_, error);
        if (error) std::abort();
    }
};

void check_failure(Failure selected, std::string_view diagnostic, bool timer_expected) {
    FixtureDirectory directory;
    // The regular logger can hold test.log open without write sharing on MSVC.
    const HANDLE regular_log = CreateFileW(L"test.log", GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    MG_CHECK(regular_log != INVALID_HANDLE_VALUE);
    struct CloseLog {
        HANDLE file;
        ~CloseLog() { CloseHandle(file); }
    } close_log{regular_log};
    CForm::StaticInit();
    events.clear();
    failure = selected;
    end_timer = !timer_expected;
    g_ExitState = 3;  // A previous successful battle must not affect the failure result.
    g_Flags = 0;
    SRobotGameState result{17, 18, 19, 20, 21, 22};
    const auto before = result;
    const int code = GetRobotInterface()->m_Run(nullptr, nullptr, nullptr, nullptr, nullptr,
                                               nullptr, nullptr, nullptr, nullptr, &result);
    MG_CHECK(code == 100 && g_ExitState == 100);
    MG_CHECK(FLAG(g_Flags, GFLAG_EXITLOOP));
    MG_CHECK(end_timer == timer_expected);
    MG_CHECK(events.back() == 4);
    MG_CHECK(std::count(events.begin(), events.end(), 4) == 1);
    MG_CHECK(g_FormCur == nullptr && g_FormFirst == nullptr && g_FormLast == nullptr);
    MG_CHECK(std::memcmp(&before, &result, sizeof(result)) == 0);
    std::ifstream file("matrixgame-dll-errors.log", std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    MG_CHECK(text.find("MatrixGame DLL Run failed:") != std::string::npos);
    MG_CHECK(text.find(diagnostic) != std::string::npos);
}

void engine_init() { check_failure(Failure::engine_init, "Synthetic engine initialization failure", false); }
void standard_init() { check_failure(Failure::standard_init, "Synthetic standard initialization failure", false); }
void unknown_init() { check_failure(Failure::unknown_init, "Unknown C++ exception", false); }
void form_failure() { check_failure(Failure::form, "Synthetic form failure", false); }
void loop_failure() { check_failure(Failure::loop, "Synthetic loop failure", true); }
void save_failure() { check_failure(Failure::save, "Synthetic result failure", true); }

void success_values() {
    for (int value : {0, 1, 2, 3, 4, 100, 101, 102, 103, 104}) {
        events.clear();
        CForm::StaticInit();
        failure = Failure::none;
        exit_state = value;
        exit_flag = true;
        SRobotGameState result{};
        MG_CHECK(GetRobotInterface()->m_Run(nullptr, nullptr, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, &result) == value);
        MG_CHECK(events == std::vector<int>({1, 2, 3, 4}));
        MG_CHECK(result.m_Time == 61 && end_timer);
        MG_CHECK(g_FormCur == nullptr && g_FormFirst == nullptr);
    }
    events.clear();
    exit_state = 3;
    exit_flag = false;
    SRobotGameState result{};
    MG_CHECK(GetRobotInterface()->m_Run(nullptr, nullptr, nullptr, nullptr, nullptr,
                                        nullptr, nullptr, nullptr, nullptr, &result) == 0);
}

void interface_layout() {
    static_assert(sizeof(SMGDRobotInterface) == 16);
    static_assert(offsetof(SMGDRobotInterface, m_Run) == 12);
    SMGDRangersInterface host{};
    auto *api = GetRobotInterface();
    MG_CHECK(api->m_Init && api->m_Deinit && api->m_Support && api->m_Run);
    api->m_Init(&host);
    MG_CHECK(g_RangersInterface == &host);
    api->m_Deinit();
    MG_CHECK(g_RangersInterface == nullptr);
}

// The original host is a 32-bit process: pointers, long and the callback slots are four bytes wide and
// structures use natural alignment. These checks describe that ABI and apply only to x86 builds.
#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(void *) == 4 && sizeof(long) == 4 && sizeof(bool) == 1 && sizeof(wchar) == 2);
static_assert(sizeof(dword) == 4 && sizeof(DWORD) == 4 && sizeof(float) == 4 && sizeof(Base::CRect) == 16);
static_assert(sizeof(SRobotsSettings) == 56 && offsetof(SRobotsSettings, FD3DDevice) == 4 &&
              offsetof(SRobotsSettings, m_ShowStencilShadows) == 8 &&
              offsetof(SRobotsSettings, m_ShowProjShadows) == 9 && offsetof(SRobotsSettings, m_IzvratMS) == 10 &&
              offsetof(SRobotsSettings, m_LandTexturesGloss) == 11 &&
              offsetof(SRobotsSettings, m_ObjTexturesGloss) == 12 &&
              offsetof(SRobotsSettings, m_SoftwareCursor) == 13 && offsetof(SRobotsSettings, m_SkyBox) == 14 &&
              offsetof(SRobotsSettings, m_RobotShadow) == 15 && offsetof(SRobotsSettings, m_BPP) == 16 &&
              offsetof(SRobotsSettings, m_ResolutionX) == 20 && offsetof(SRobotsSettings, m_ResolutionY) == 24 &&
              offsetof(SRobotsSettings, m_RefreshRate) == 28 && offsetof(SRobotsSettings, m_Brightness) == 32 &&
              offsetof(SRobotsSettings, m_Contrast) == 36 && offsetof(SRobotsSettings, m_FSAASamples) == 40 &&
              offsetof(SRobotsSettings, m_AFDegree) == 44 && offsetof(SRobotsSettings, m_MaxDistance) == 48 &&
              offsetof(SRobotsSettings, m_VSync) == 52);
static_assert(sizeof(SRobotGameState) == 24 && offsetof(SRobotGameState, m_BuildRobot) == 4 &&
              offsetof(SRobotGameState, m_KillRobot) == 8 && offsetof(SRobotGameState, m_BuildTurret) == 12 &&
              offsetof(SRobotGameState, m_KillTurret) == 16 && offsetof(SRobotGameState, m_KillBuilding) == 20);
static_assert(sizeof(SMGDRobotInterface) == 16 && offsetof(SMGDRobotInterface, m_Deinit) == 4 &&
              offsetof(SMGDRobotInterface, m_Support) == 8 && offsetof(SMGDRobotInterface, m_Run) == 12);
static_assert(sizeof(SMGDRangersInterface) == 64 && offsetof(SMGDRangersInterface, m_SoundCreate) == 4 &&
              offsetof(SMGDRangersInterface, m_SoundDestroy) == 8 &&
              offsetof(SMGDRangersInterface, m_SoundPlay) == 12 &&
              offsetof(SMGDRangersInterface, m_SoundIsPlay) == 16 &&
              offsetof(SMGDRangersInterface, m_SoundVolume) == 20 &&
              offsetof(SMGDRangersInterface, m_SoundPan) == 24 &&
              offsetof(SMGDRangersInterface, m_SoundGetVolume) == 28 &&
              offsetof(SMGDRangersInterface, m_SoundGetPan) == 32 &&
              offsetof(SMGDRangersInterface, m_RangersText) == 36 &&
              offsetof(SMGDRangersInterface, m_RangersTextClear) == 40 &&
              offsetof(SMGDRangersInterface, m_ProgressBar) == 44 && offsetof(SMGDRangersInterface, m_Takt) == 48 &&
              offsetof(SMGDRangersInterface, m_Begin) == 52 &&
              offsetof(SMGDRangersInterface, m_MusicVolumeGet) == 56 &&
              offsetof(SMGDRangersInterface, m_MusicVolumeSet) == 60);
static_assert(sizeof(SMGDRangersInterfaceText) == 20 && offsetof(SMGDRangersInterfaceText, m_Buf) == 4 &&
              offsetof(SMGDRangersInterfaceText, m_Pitch) == 8 && offsetof(SMGDRangersInterfaceText, m_SizeX) == 12 &&
              offsetof(SMGDRangersInterfaceText, m_SizeY) == 16);
#endif
static_assert(std::is_trivially_copyable_v<SRobotsSettings> && std::is_standard_layout_v<SRobotsSettings>);
// Host-visible result and support codes; tests compare against these literals, not the production names.
static_assert(MATRIXGAME_RUN_ERROR == 100, "Caught entry failures return the host's loop-error code 100");
static_assert(SUPE_OK == 0 && SUPE_DIRECTX == 1 && SUPE_VIDEODRIVER == 2 && SUPE_VIDEOHARDWARE == 3,
              "Support results are host-visible codes 0 to 3");
static_assert(std::is_trivially_copyable_v<SRobotGameState> && std::is_standard_layout_v<SMGDRangersInterface>);

void host_layout() {
    // The host reads result statistics as six little-endian 32-bit integers.
    const auto result = wire::Bytes().u32(61000).u32(2).u32(3).u32(4).u32(5).u32(0xffffffff);
    SRobotGameState state;
    MG_CHECK(result.size() == sizeof(state));
    std::memcpy(&state, result.data.data(), sizeof(state));
    MG_CHECK(state.m_Time == 61000 && state.m_BuildRobot == 2 && state.m_KillRobot == 3);
    MG_CHECK(state.m_BuildTurret == 4 && state.m_KillTurret == 5 && state.m_KillBuilding == -1);
#if defined(_M_IX86) || defined(__i386__)
    // Host text descriptors carry a 32-bit pixel pointer between 32-bit fields.
    std::array<BYTE, 4> pixels{};
    const auto pointer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pixels.data()));
    const auto text = wire::Bytes().u32(9).u32(pointer).u32(16).u32(4).u32(1);
    SMGDRangersInterfaceText descriptor;
    MG_CHECK(text.size() == sizeof(descriptor));
    std::memcpy(&descriptor, text.data.data(), sizeof(descriptor));
    MG_CHECK(descriptor.m_Image == 9 && descriptor.m_Buf == pixels.data() && descriptor.m_Pitch == 16);
    MG_CHECK(descriptor.m_SizeX == 4 && descriptor.m_SizeY == 1);
#endif
}

void entry_table() {
    // The exported table is one static object; each query refills the same four entry points.
    auto *first = GetRobotInterface();
    const SMGDRobotInterface entries = *first;
    auto *second = GetRobotInterface();
    MG_CHECK(first == second && first == &g_RobotInterface);
    MG_CHECK(std::memcmp(&entries, second, sizeof(entries)) == 0);
    MG_CHECK(entries.m_Init && entries.m_Deinit && entries.m_Support && entries.m_Run);
    std::array<uintptr_t, 4> slots;
    static_assert(sizeof(slots) == sizeof(entries));
    std::memcpy(slots.data(), &entries, sizeof(slots));
    MG_CHECK(slots[0] == reinterpret_cast<uintptr_t>(entries.m_Init));
    MG_CHECK(slots[1] == reinterpret_cast<uintptr_t>(entries.m_Deinit));
    MG_CHECK(slots[2] == reinterpret_cast<uintptr_t>(entries.m_Support));
    MG_CHECK(slots[3] == reinterpret_cast<uintptr_t>(entries.m_Run));

    // The callback table is borrowed: Init stores the host pointer, a later Init replaces it, the table
    // query does not reset it and Deinit clears it, including when repeated.
    SMGDRangersInterface host{}, replacement{};
    first->m_Deinit();
    MG_CHECK(g_RangersInterface == nullptr);
    first->m_Init(&host);
    MG_CHECK(g_RangersInterface == &host);
    first->m_Init(&replacement);
    MG_CHECK(GetRobotInterface() == first && g_RangersInterface == &replacement);
    first->m_Deinit();
    first->m_Deinit();
    MG_CHECK(g_RangersInterface == nullptr);
}

void borrowed_arguments() {
    // Run forwards host pointers to the session without copying them and seeds from the current time.
    events.clear();
    CForm::StaticInit();
    failure = Failure::none;
    exit_state = 3;
    exit_flag = true;
    wchar map[] = L"host.cmap", lang[] = L"xx", start[] = L"1EzStart", win[] = L"Win", loss[] = L"Loss";
    wchar planet[] = L"Planet";
    SRobotsSettings settings{};
    SRobotGameState result{};
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const HWND window = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1234));
    const auto before = static_cast<uint32_t>(time(nullptr));
    const int code =
            GetRobotInterface()->m_Run(instance, window, map, &settings, lang, start, win, loss, planet, &result);
    const auto after = static_cast<uint32_t>(time(nullptr));
    MG_CHECK(code == 3 && events == std::vector<int>({1, 2, 3, 4}));
    MG_CHECK(init_arguments.instance == instance && init_arguments.window == window);
    MG_CHECK(init_arguments.map == map && init_arguments.settings == &settings && init_arguments.lang == lang);
    MG_CHECK(init_arguments.start == start && init_arguments.win == win && init_arguments.loss == loss);
    MG_CHECK(init_arguments.planet == planet && saved_result == &result);
    MG_CHECK(init_arguments.seed >= before && init_arguments.seed <= after);
}

constexpr tests::Case cases[] = {
    {"game.dll.engine_init_failure", engine_init},
    {"game.dll.standard_init_failure", standard_init},
    {"game.dll.unknown_init_failure", unknown_init},
    {"game.dll.form_failure", form_failure},
    {"game.dll.loop_failure", loop_failure},
    {"game.dll.result_failure", save_failure},
    {"game.dll.success_values", success_values},
    {"game.dll.interface_layout", interface_layout},
    {"game.dll.host_layout", host_layout},
    {"game.dll.entry_table", entry_table},
    {"game.dll.borrowed_arguments", borrowed_arguments},
};

}  // namespace

void CGame::Init(HINSTANCE instance, HWND window, const wchar *map, uint32_t seed, const SRobotsSettings *settings,
                 const wchar *lang, const wchar *start, const wchar *win, const wchar *loss, const wchar *planet) {
    init_arguments = {instance, window, map, seed, settings, lang, start, win, loss, planet};
    events.push_back(1);
    if (failure == Failure::engine_init) ERROR_S(L"Synthetic engine initialization failure");
    if (failure == Failure::standard_init) throw std::runtime_error("Synthetic standard initialization failure");
    if (failure == Failure::unknown_init) throw 17;
}

void CGame::RunGameLoop(CFormMatrixGame *form) {
    events.push_back(2);
    FormChange(form);
    if (failure == Failure::loop) throw std::runtime_error("Synthetic loop failure");
    g_ExitState = exit_state;
    if (exit_flag) SETFLAG(g_Flags, GFLAG_EXITLOOP);
    else RESETFLAG(g_Flags, GFLAG_EXITLOOP);
}

void CGame::SaveResult(SRobotGameState *result) {
    saved_result = result;
    events.push_back(3);
    if (failure == Failure::save) throw std::runtime_error("Synthetic result failure");
    result->m_Time = 61;
}

void CGame::SafeFree(bool timer) noexcept {
    events.push_back(4);
    end_timer = timer;
    // The real form must still be registered when cleanup calls Leave.
    if (g_FormCur && g_FormCur != g_FormFirst) std::abort();
    FormChange(nullptr);
}

CFormMatrixGame::CFormMatrixGame() {
    if (failure == Failure::form) throw std::runtime_error("Synthetic form failure");
}
CFormMatrixGame::~CFormMatrixGame() = default;
void CFormMatrixGame::Enter() {}
void CFormMatrixGame::Leave() {}
void CFormMatrixGame::Draw() {}
void CFormMatrixGame::Takt(int) {}
void CFormMatrixGame::MouseMove(int, int) {}
void CFormMatrixGame::MouseKey(ButtonStatus, int, int, int) {}
void CFormMatrixGame::Keyboard(bool, uint8_t) {}
void CFormMatrixGame::SystemEvent(ESysEvent) {}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    const int result = tests::run(argc, argv, cases);
    Base::CMain::BaseDeInit();
    return result;
}
