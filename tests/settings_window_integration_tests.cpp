// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "battle_scenario.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixRobot.hpp"
#include "Interface/CInterface.h"
#include "Interface/MatrixHint.hpp"
#include "SettingsWindow.hpp"
#include "SettingsModel.hpp"
#include "StandaloneRuntime.hpp"
#include "WindowDisplay.hpp"
#include "Text/Render.hpp"
#include "input.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Presentation boundary only: synthetic opaque text-sized rectangles, no glyphs,
// D3DX font, draw calls or game artwork. Hint composition remains production code.
namespace Text {
std::vector<std::wstring> rendered_text;
void Render(std::wstring_view text, std::wstring_view, uint32_t color, int width, int height,
            int, int, int, int, int, const Base::CRect &, CBitmap &dst) {
    rendered_text.emplace_back(text);
    width = width > 0 ? width : std::max(1, static_cast<int>(text.size()) * 8);
    height = height > 0 ? height : 16;
    MG_CHECK(width <= 2048 && height <= 2048);
    dst.CreateRGBA(width, height);
    std::fill_n(reinterpret_cast<uint32_t *>(dst.Data()), width * height, color);
}
}

namespace {
[[noreturn]] void unsupported(const char *name) {
    std::fprintf(stderr, "Settings adapter: unsupported %s\n", name);
    std::abort();
}
class Adapter final : public IDirect3D9 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { unsupported(__func__); }
    ULONG STDMETHODCALLTYPE AddRef() override { unsupported(__func__); }
    ULONG STDMETHODCALLTYPE Release() override { unsupported(__func__); }
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void *) override { unsupported(__func__); }
    UINT STDMETHODCALLTYPE GetAdapterCount() override { unsupported(__func__); }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9 *) override { unsupported(__func__); }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter, D3DFORMAT format) override {
        MG_CHECK(adapter == 0 && format == D3DFMT_X8R8G8B8);
        return 2;
    }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT index, D3DDISPLAYMODE *mode) override {
        MG_CHECK(adapter == 0 && format == D3DFMT_X8R8G8B8 && index < 2 && mode);
        *mode = {index == 0 ? 640u : 1280u, index == 0 ? 480u : 720u, 60, format};
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT, D3DDISPLAYMODE *) override { unsupported(__func__); }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, BOOL) override { unsupported(__func__); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT) override { unsupported(__func__); }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE, DWORD *) override { unsupported(__func__); }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override { unsupported(__func__); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT) override { unsupported(__func__); }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9 *) override { unsupported(__func__); }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) override { unsupported(__func__); }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **) override { unsupported(__func__); }
};
struct Directory {
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root = previous / ("settings-window-" + std::to_string(GetCurrentProcessId()));
    Directory() {
        MG_CHECK(std::filesystem::create_directory(root));
        std::filesystem::current_path(root);
    }
    ~Directory() {
        std::filesystem::current_path(previous);
        std::error_code error;
        SetFileAttributesW((root / "CFG/standalone.txt").c_str(), FILE_ATTRIBUTE_NORMAL);
        std::filesystem::remove_all(root, error);
    }
};
struct ReadOnlyFile {
    std::filesystem::path path;
    DWORD attributes;
    explicit ReadOnlyFile(const std::filesystem::path &file)
        : path(std::filesystem::absolute(file)), attributes(GetFileAttributesW(path.c_str())) {
        MG_CHECK(attributes != INVALID_FILE_ATTRIBUTES);
        MG_CHECK(SetFileAttributesW(path.c_str(), attributes | FILE_ATTRIBUTE_READONLY));
    }
    ~ReadOnlyFile() { SetFileAttributesW(path.c_str(), attributes); }
    void restore() { MG_CHECK(SetFileAttributesW(path.c_str(), attributes)); }
};
bool rendered(const wchar_t *text) {
    return std::find(Text::rendered_text.begin(), Text::rendered_text.end(), text) != Text::rendered_text.end();
}
void press(CFormMatrixGame &form, uint8_t key) { form.Keyboard(true, key); form.Keyboard(false, key); }
struct Window {
    Directory directory;
    tests::battle::Session session;
    Adapter adapter;
    IDirect3D9 *previous_adapter = g_D3D;
    CBitmap hole;
    CFormMatrixGame form;
    StandaloneSettings::Values initial;
    explicit Window(random::Mode mode) : session({mode, 87, 10, 1000, false, true}) {
        g_D3D = &adapter;
        Text::rendered_text.clear();
        initial.random = mode;
        initial.display.width = 1280; initial.display.height = 720;
        initial.display.mode = Display::Mode::Windowed;
        g_Config.m_CamMoveSpeed = 2;
        g_Config.m_SkyBox = 2;
        StandaloneSettings::initialize_runtime(initial);
        Display::initialize(initial.display);
        g_MatrixData->BlockGetAdd(PAR_SOURCE_HINTS);
        g_MatrixData->BlockGetAdd(PAR_REPLACE)->ParSetAdd(L"setup", L"Settings");
        g_MatrixData->BlockGetAdd(PAR_TEMPLATES)->ParSetAdd(TEMPLATE_DIALOG_MENU,
            L"-1|_WIDTH:200|_HEIGHT:180|_TEXT:Synthetic menu");
        hole.CreateRGBA(8, 8);
        std::fill_n(reinterpret_cast<uint32_t *>(hole.Data()), 64, 0u);
        CMatrixHint::SetRuntimeBitmap(L"bhole2", &hole);
        auto *panel = HNew(g_MatrixHeap) CInterface;
        panel->m_strName = L"synthetic-settings-hints";
        panel->m_AlwaysOnTop = true;
        g_IFaceList->m_First = g_IFaceList->m_Last = g_IFaceList->m_Hints = panel;
    }
    ~Window() {
        g_MatrixMap->LeaveDialogMode();
        SettingsWindow::shutdown();
        CMatrixHint::SetRuntimeBitmap(L"bhole2", nullptr);
        Display::shutdown();
        g_D3D = previous_adapter;
    }
    void open(bool menu_click = false) {
        press(form, VK_ESCAPE);
        MG_CHECK(g_MatrixMap->m_DialogModeHints.size() == 1);
        MG_CHECK(g_MatrixMap->IsPaused());
        if (menu_click) {
            const auto *menu = g_MatrixMap->m_DialogModeHints[0];
            const int x = menu->m_PosX + 10, y = menu->m_PosY + 110;
            form.MouseMove(x, y);
            form.MouseKey(B_DOWN, VK_LBUTTON, x, y);
            form.MouseKey(B_UP, VK_LBUTTON, x, y);
        }
        else press(form, 'C');
        MG_CHECK(SettingsWindow::active());
        MG_CHECK(g_MatrixMap->IsPaused());
    }
    void row(SettingsModel::Row row, bool right) {
        auto *hint = g_MatrixMap->m_DialogModeHints[0];
        const int x = hint->m_PosX + (right ? SettingsModel::Layout::next_x : SettingsModel::Layout::previous_x) + 1;
        const int y = hint->m_PosY + SettingsModel::row_top(static_cast<int>(row)) + 1;
        form.MouseMove(x, y);
        form.MouseKey(B_DOWN, VK_LBUTTON, x, y);
        form.MouseKey(B_UP, VK_LBUTTON, x, y);
    }
    void resume() {
        MG_CHECK(!SettingsWindow::active());
        MG_CHECK(g_MatrixMap->m_DialogModeName && wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_MENU) == 0);
        MG_CHECK(g_MatrixMap->IsPaused());
        press(form, VK_ESCAPE);
        MG_CHECK(!g_MatrixMap->IsPaused() && g_MatrixMap->m_DialogModeName == nullptr);
    }
};

void input_pause_cancel() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (bool menu_click : {false, true}) {
        Window w(mode);
        std::cout << w.session.metadata("settings-window-input-v1") << "\nmenu_click=" << menu_click << std::endl;
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto *robot = side.GetCurGroup()->GetObjectByN(0)->AsRobot();
        w.form.Keyboard(true, VK_CONTROL); press(w.form, '1'); w.form.Keyboard(false, VK_CONTROL);
        MG_CHECK(robot->GetCtrlGroup() == '1');
        w.session.ground_command(120, 500);
        for (int i = 0; i < 20; ++i) w.session.advance();
        MG_CHECK(w.session.robot(1).y > 200);
        w.open(menu_click);
        MG_CHECK(side.GetCurGroup() == nullptr); // Opening the menu intentionally drops selection.
        press(w.form, '1');
        MG_CHECK(side.GetCurGroup() == nullptr); // Assigned-group recall must not escape the modal window.
        // Stress precondition: preserve a viable selection through the modal route;
        // normal menu opening has already been checked to deselect it.
        side.CreateGroupFromCurrent(robot);
        side.Select(ROBOT, nullptr);
        MG_CHECK(side.GetCurGroup() && side.GetCurGroup()->GetObjectByN(0) == robot);
        const auto before = w.session.robot(1);
        const auto time = w.session.time();
        const auto flags = g_Flags;
        const auto selected = side.GetCurGroup();
        press(w.form, 'M');
        MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
        w.form.Keyboard(true, VK_CONTROL); press(w.form, '2'); w.form.Keyboard(false, VK_CONTROL);
        MG_CHECK(robot->GetCtrlGroup() == '1');
        for (auto key : std::string("NEED4SPEED")) press(w.form, key);
        press(w.form, VK_PAUSE);
        for (int i = 0; i < 20; ++i) w.session.advance();
        MG_CHECK(g_MatrixMap->IsPaused() && w.session.time() == time);
        MG_CHECK(w.session.robot(1).x == before.x && w.session.robot(1).y == before.y);
        MG_CHECK(w.session.robot(1).order == before.order);
        MG_CHECK(!FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
        MG_CHECK(side.GetCurGroup() == selected && robot->GetCtrlGroup() == '1');
        MG_CHECK(g_Flags == flags);
        Text::rendered_text.clear();
        w.row(SettingsModel::Row::CameraSpeed, true);
        MG_CHECK(rendered(L"125%"));
        MG_CHECK(StandaloneSettings::current() == w.initial && g_Config.m_CamMoveSpeed == 2);
        press(w.form, VK_ESCAPE); w.resume();
        MG_CHECK(StandaloneSettings::current() == w.initial && !std::filesystem::exists("CFG/standalone.txt"));
        for (int i = 0; i < 20; ++i) w.session.advance();
        MG_CHECK(w.session.time() > time && w.session.robot(1).y > before.y);
        MG_CHECK(side.GetCurGroup() == nullptr);
        press(w.form, '1');
        MG_CHECK(side.GetCurGroup() && side.GetCurGroup()->GetRobotsCnt() == 2);
        press(w.form, 'M'); MG_CHECK(FLAG(g_IFaceList->m_IfListFlags, ORDERING_MODE));
        w.form.Keyboard(true, VK_CONTROL); press(w.form, '2'); w.form.Keyboard(false, VK_CONTROL);
        MG_CHECK(robot->GetCtrlGroup() == '2');
        for (auto key : std::string("NEED4SPEED")) press(w.form, key);
#if MATRIXGAME_SCENARIO_CHEATS
        MG_CHECK(g_Flags == (flags ^ GFLAG_4SPEED));
#else
        MG_CHECK(g_Flags == flags);
#endif
    }
}
void apply_and_save_failure_profile(random::Mode mode) {
    Window w(mode);
    std::cout << w.session.metadata("settings-window-save-v1") << std::endl;
    w.open();
    w.row(SettingsModel::Row::CameraSpeed, true);
    MG_CHECK(rendered(L"125%"));
    Text::rendered_text.clear();
    press(w.form, VK_RIGHT);
    MG_CHECK(rendered(L"150%"));
    Text::rendered_text.clear();
    press(w.form, VK_LEFT);
    MG_CHECK(rendered(L"125%"));
    w.row(SettingsModel::Row::SoundVolume, false);
    w.row(SettingsModel::Row::Skybox, false);
    MG_CHECK(StandaloneSettings::current() == w.initial && g_Config.m_CamMoveSpeed == 2);
    std::string error;
    MG_CHECK(StandaloneSettings::save(w.initial, error));
    auto contents = [] {
        std::ifstream file("CFG/standalone.txt", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    const auto saved_bytes = contents();
    MG_CHECK(!saved_bytes.empty());
    // Windows rejects replacement of a read-only existing target.
    ReadOnlyFile protected_file("CFG/standalone.txt");
    Text::rendered_text.clear();
    press(w.form, VK_RETURN);
    MG_CHECK(rendered(L"Settings could not be saved; see test.log"));
    MG_CHECK(SettingsWindow::active() && g_MatrixMap->IsPaused());
    MG_CHECK(StandaloneSettings::current() == w.initial && g_Config.m_CamMoveSpeed == 2);
    MG_CHECK(contents() == saved_bytes);
    MG_CHECK(!std::filesystem::exists("CFG/standalone.txt.tmp"));
    protected_file.restore();
    press(w.form, VK_RETURN);
    MG_CHECK(!SettingsWindow::active());
    const auto values = StandaloneSettings::current();
    MG_CHECK(values.camera_speed == 125 && values.sound_volume == 90);
    MG_CHECK(values.skybox == StandaloneSettings::Skybox::Low && g_Config.m_SkyBox == 2);
    MG_CHECK(g_Config.m_CamMoveSpeed == 2.5f && StandaloneSettings::restart_pending(values));
    const auto loaded = StandaloneSettings::load();
    MG_CHECK(loaded.values == values);
    w.resume();
}
void apply_and_save_failure() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) apply_and_save_failure_profile(mode);
}
const tests::Case cases[]{
    {"game.presentation.settings_input_pause_cancel", input_pause_cancel},
    {"game.presentation.settings_apply_save_failure", apply_and_save_failure},
};
}
int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
