// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "Interface/CInterface.h"
#include "Interface/CIFaceButton.h"
#include "Interface/MatrixHint.hpp"
#include "MapWindow.hpp"
#include "MapSelection.hpp"
#include "StandaloneRuntime.hpp"
#include "WindowDisplay.hpp"
#include "Text/Font.hpp"
#include "Text/Render.hpp"
#include "CStorage.hpp"
#include "CException.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>

// Text metrics and pixels are synthetic. Selector state, layout, hint composition,
// input dispatch, catalog reads, persistence and battle callbacks remain production.
namespace Text {
Font::Font(LPD3DXFONT font) : m_font(font) { MG_CHECK(!font); }
Font::~Font() = default;
LPD3DXFONT Font::operator->() const { throw std::logic_error("Native font draw outside selector fixture"); }
Font::operator bool() const { return true; }
size_t Font::GetHeight() const { return 16; }
size_t Font::GetSpaceWidth() const { return 8; }
size_t Font::CalcTextWidth(std::wstring_view text) const { return text.size() * 8; }
Font &GetFont(IDirect3DDevice9 *, std::wstring_view) { static Font font(nullptr); return font; }
void OnLostDevice() {}
void OnResetDevice() {}
void Render(std::wstring_view text, std::wstring_view, uint32_t color, int width, int height,
    int, int, int, int, int, const Base::CRect &, CBitmap &bitmap) {
    width = width > 0 ? width : std::max(1, static_cast<int>(text.size()) * 8);
    height = height > 0 ? height : 16;
    MG_CHECK(width <= 2048 && height <= 2048);
    bitmap.CreateRGBA(width, height);
    std::fill_n(reinterpret_cast<uint32_t *>(bitmap.Data()), width * height, color);
}
}
namespace {
using namespace tests::battle;
namespace fs = std::filesystem;
void press(CFormMatrixGame &form, uint8_t key) { form.Keyboard(true, key); form.Keyboard(false, key); }
struct Directory {
    fs::path previous = fs::current_path();
    fs::path root = previous / ("map-window-" + std::to_string(GetCurrentProcessId()));
    Directory() { MG_CHECK(fs::create_directory(root)); fs::current_path(root); }
    ~Directory() { fs::current_path(previous); std::error_code error; fs::remove_all(root, error); }
};
void write_catalog_map(const wchar_t *path) {
    Base::CStorage map;
    Base::CStorageRecord properties(DATA_PROPERTIES);
    properties.AddItem(Base::CStorageRecordItem(DATA_PROPERTIES_NAME, Base::ST_WCHAR));
    properties.AddItem(Base::CStorageRecordItem(DATA_PROPERTIES_VALUE, Base::ST_WCHAR));
    map.AddRecord(properties);
    map.Save(path);
}
struct Window {
    Directory directory;
    Session session;
    CFormMatrixGame form;
    CBitmap pixel;
    StandaloneSettings::Values initial;
    int previous_exit = g_ExitState;
    Window(random::Mode mode, int step) : session({mode, 85, step, 1000, false}) {
        MG_CHECK(!MapSelection::relaunch_requested());
        fs::create_directories("Matrix/Map");
        write_catalog_map(L"Matrix/Map/alpha.cmap");
        write_catalog_map(L"Matrix/Map/beta.cmap");
        initial.map = L"Matrix\\Map\\alpha.cmap";
        initial.random = mode;
        initial.camera_speed = 125;
        initial.sound_volume = 70;
        initial.display = {1280, 720};
        std::string error;
        MG_CHECK(StandaloneSettings::save(initial, error));
        StandaloneSettings::initialize_runtime(initial);
        Display::initialize(initial.display);
        g_MatrixMap->m_Ids[g_MatrixMap->m_IdsCnt - 1] = initial.map;
        g_ExitState = 0;
        auto *hints = HNew(g_MatrixHeap) CInterface;
        g_IFaceList->m_First = g_IFaceList->m_Last = g_IFaceList->m_Hints = hints;
        pixel.CreateRGBA(2, 2);
        std::memset(pixel.Data(), 255, pixel.Pitch() * pixel.SizeY());
        CMatrixHint::SetRuntimeBitmap(L"selector-pixel", &pixel);
        CMatrixHint::SetRuntimeBitmap(L"bhole2", &pixel);
        g_MatrixData->BlockGetAdd(PAR_SOURCE_HINTS);
        g_MatrixData->BlockGetAdd(PAR_REPLACE)->ParSetAdd(L"setup", L"Settings");
        g_MatrixData->BlockGetAdd(L"Side")->ParSetAdd(L"2", L"Enemy");
        for (const auto *name : {IF_HINTS_OK, IF_HINTS_CANCEL, IF_HINTS_CANCEL_MENU, IF_HINTS_CONTINUE,
             IF_HINTS_SURRENDER, IF_HINTS_RESET, IF_HINTS_EXIT, IF_HINTS_HELP}) {
            auto *button = HNew(g_MatrixHeap) CIFaceButton;
            button->m_Type = IFACE_PUSH_BUTTON;
            button->m_strName = name;
            MG_CHECK(hints->AddElement(button));
        }
        for (const auto *name : {TEMPLATE_DIALOG_MENU, TEMPLATE_DIALOG_WIN, TEMPLATE_DIALOG_LOOSE,
             TEMPLATE_DIALOG_STATISTICS, TEMPLATE_DIALOG_EXIT, TEMPLATE_DIALOG_SURRENDER}) {
            std::wstring markup = L"-1|_BITMAP:selector-pixel:";
            for (int i = 0; i < (wcscmp(name, TEMPLATE_DIALOG_MENU) == 0 ? 6 : 2); ++i)
                markup += L"|_MOD:COPY|_BITMAP:selector-pixel:";
            g_MatrixData->BlockGetAdd(PAR_TEMPLATES)->ParSetAdd(name, markup);
        }
    }
    ~Window() {
        MapWindow::shutdown();
        g_MatrixMap->LeaveDialogMode();
        CMatrixHint::SetRuntimeBitmap(L"selector-pixel", nullptr);
        CMatrixHint::SetRuntimeBitmap(L"bhole2", nullptr);
        Display::shutdown();
        g_ExitState = previous_exit;
    }
    void move() {
        session.ground_command(500, 300);
        for (int i = 0; i < 25; ++i) session.advance();
        MG_CHECK(session.robot(1).x > 100 && session.robot(1).order == mpo_MoveTo);
    }
};
void menu_cancel() try {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Window window(mode, step);
        window.move();
        const auto before = window.session.robot(1);
        const auto time = window.session.time();
        const auto draws = random::draw_count();
        press(window.form, VK_ESCAPE);
        press(window.form, 'N');
        MG_CHECK(MapWindow::active() && g_MatrixMap->IsPaused());
        for (int i = 0; i < 25; ++i) window.session.advance();
        MG_CHECK(window.session.time() == time && random::draw_count() == draws);
        MG_CHECK(window.session.robot(1).x == before.x && window.session.robot(1).y == before.y);
        MG_CHECK(window.session.robot(1).order == before.order);
        press(window.form, VK_DOWN);
        press(window.form, '2');
        press(window.form, VK_ESCAPE);
        MG_CHECK(!MapWindow::active() && g_MatrixMap->IsPaused());
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_MENU);
        MG_CHECK(StandaloneSettings::load().values == window.initial && !MapSelection::relaunch_requested());
        MG_CHECK(!FLAG(g_Flags, GFLAG_EXITLOOP) && g_ExitState == 0);
        press(window.form, VK_ESCAPE);
        MG_CHECK(!g_MatrixMap->IsPaused());
        for (int i = 0; i < 25; ++i) window.session.advance();
        MG_CHECK(window.session.robot(1).order == before.order && window.session.robot(1).x > before.x);
        std::cout << window.session.metadata("map-selector-menu-cancel-v1") << '\n';
    }
} catch (const Base::CException &error) { std::wcerr << error.Info() << '\n'; throw; }
enum class Entry { Menu, Win, Loss, Surrender, Exit };
enum class Choice { Cancel, Same, Different };
void continuation(Entry entry, Choice choice, random::Mode mode, int step) try {
    Window window(mode, step);
    std::cout << window.session.metadata("map-selector-continuation-v1") << " origin=" << int(entry)
              << " choice=" << int(choice) << '\n';
    window.move();
    const int expected_exit = entry == Entry::Win ? 3 : entry == Entry::Loss ? 2 :
        entry == Entry::Surrender ? 4 : entry == Entry::Exit ? 1 : 0;
    if (entry == Entry::Win || entry == Entry::Loss) {
        const bool loss = entry == Entry::Loss;
        MG_CHECK(window.session.remove(loss ? 1 : 3) && window.session.remove(loss ? 2 : 4));
        for (int i = 0; !g_MatrixMap->IsPaused() && i < 500; ++i) window.session.advance();
        MG_CHECK(g_MatrixMap->IsPaused());
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) == (loss ? TEMPLATE_DIALOG_LOOSE : TEMPLATE_DIALOG_WIN));
        press(window.form, VK_RETURN);
    } else {
        press(window.form, VK_ESCAPE);
        press(window.form, entry == Entry::Menu ? 'N' : entry == Entry::Surrender ? 'S' : 'E');
        if (entry != Entry::Menu) press(window.form, VK_RETURN);
    }
    if (entry != Entry::Menu) {
        MG_CHECK(g_ExitState == expected_exit && FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG));
        window.session.advance();
        MG_CHECK(std::wstring(g_MatrixMap->m_DialogModeName) == TEMPLATE_DIALOG_STATISTICS);
        MG_CHECK(!MapWindow::active() && !FLAG(g_Flags, GFLAG_EXITLOOP));
        press(window.form, VK_RETURN);
    }
    if (entry == Entry::Exit) {
        MG_CHECK(!MapWindow::active() && FLAG(g_Flags, GFLAG_EXITLOOP));
        MG_CHECK(!MapSelection::relaunch_requested() && StandaloneSettings::load().values == window.initial);
        MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
        return;
    }
    MG_CHECK(MapWindow::active() && g_MatrixMap->IsPaused() && !FLAG(g_Flags, GFLAG_EXITLOOP));
    const auto time = window.session.time();
    const auto draws = random::draw_count();
    for (int i = 0; i < 5; ++i) window.session.advance();
    MG_CHECK(window.session.time() == time && random::draw_count() == draws);
    // Default selection is the current map. Change selection/bonus only through input.
    if (choice == Choice::Different) press(window.form, VK_DOWN);
    press(window.form, '2');
    press(window.form, choice == Choice::Cancel ? VK_ESCAPE : VK_RETURN);
    MG_CHECK(!MapWindow::active() && g_ExitState == expected_exit);
    MG_CHECK(window.session.time() == time && random::draw_count() == draws);
    MG_CHECK(FLAG(g_Flags, GFLAG_EXITLOOP));
    MG_CHECK(!g_MatrixMap->IsPaused() && !g_MatrixMap->m_DialogModeName);
    if (choice == Choice::Cancel) {
        MG_CHECK(!MapSelection::relaunch_requested() && StandaloneSettings::load().values == window.initial);
    } else {
        auto expected = window.initial;
        expected.map = choice == Choice::Different ? L"Matrix\\Map\\beta.cmap" : L"Matrix\\Map\\alpha.cmap";
        expected.battle_bonus = BattleChoice::Bonus::AirSupport;
        MG_CHECK(MapSelection::relaunch_requested() && StandaloneSettings::load().values == expected);
        MG_CHECK(StandaloneSettings::current() == window.initial); // New settings belong to the next process.
    }
    std::cout << "exit_state=" << g_ExitState << " relaunch=" << MapSelection::relaunch_requested() << '\n';
} catch (const Base::CException &error) { std::wcerr << error.Info() << '\n'; throw; }
template<Entry entry, Choice choice, random::Mode mode, int step>
void run_continuation() { continuation(entry, choice, mode, step); }
// Acceptance requests a process-lifetime relaunch flag. Each CTest invocation
// therefore has its own process; the fixture does not add a fake reset API.
#define SELECTOR_CASES(name, entry, choice) \
    {"game.selector." name ".park10", run_continuation<Entry::entry, Choice::choice, random::Mode::ParkMiller, 10>}, \
    {"game.selector." name ".park16", run_continuation<Entry::entry, Choice::choice, random::Mode::ParkMiller, 16>}, \
    {"game.selector." name ".legacy10", run_continuation<Entry::entry, Choice::choice, random::Mode::LegacyCRT, 10>}, \
    {"game.selector." name ".legacy16", run_continuation<Entry::entry, Choice::choice, random::Mode::LegacyCRT, 16>}
constexpr tests::Case cases[] = {
    {"game.selector.menu_cancel", menu_cancel},
    SELECTOR_CASES("menu_same", Menu, Same),
    SELECTOR_CASES("menu_different", Menu, Different),
    SELECTOR_CASES("win_cancel", Win, Cancel),
    SELECTOR_CASES("win_same", Win, Same),
    SELECTOR_CASES("win_different", Win, Different),
    SELECTOR_CASES("loss_cancel", Loss, Cancel),
    SELECTOR_CASES("loss_same", Loss, Same),
    SELECTOR_CASES("loss_different", Loss, Different),
    SELECTOR_CASES("surrender_cancel", Surrender, Cancel),
    SELECTOR_CASES("surrender_same", Surrender, Same),
    SELECTOR_CASES("surrender_different", Surrender, Different),
    SELECTOR_CASES("exit_statistics", Exit, Cancel),
};
#undef SELECTOR_CASES
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) return EXIT_FAILURE;
#endif
    return result;
}
