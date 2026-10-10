// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "MatrixGame.h"
#include "MatrixFormGame.hpp"
#include "3g.hpp"
#include "stupid_logger.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    MG_CHECK(code == MATRIXGAME_RUN_ERROR && g_ExitState == MATRIXGAME_RUN_ERROR);
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

constexpr tests::Case cases[] = {
    {"game.dll.engine_init_failure", engine_init},
    {"game.dll.standard_init_failure", standard_init},
    {"game.dll.unknown_init_failure", unknown_init},
    {"game.dll.form_failure", form_failure},
    {"game.dll.loop_failure", loop_failure},
    {"game.dll.result_failure", save_failure},
    {"game.dll.success_values", success_values},
    {"game.dll.interface_layout", interface_layout},
};

}  // namespace

void CGame::Init(HINSTANCE, HWND, const wchar *, uint32_t, const SRobotsSettings *, const wchar *,
                 const wchar *, const wchar *, const wchar *, const wchar *) {
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
