// MatrixGame - licensed under GPLv2 or any later version.
#include "StartupMenu.hpp"
#include "MapWindow.hpp"
#include "SettingsModel.hpp"
#include "MatrixMap.hpp"
#include "MatrixInstantDraw.hpp"
#include "MatrixSampleStateManager.hpp"
#include "Interface/MatrixHint.hpp"
#include "Text/Font.hpp"
#include "DeviceRecovery.hpp"
#include "WindowDisplay.hpp"
#include "Form.hpp"
#include "input.hpp"

#include <stupid_logger.hpp>

namespace StartupMenu {
namespace {
bool skip = false;
bool resources_ready = true;

void release_resources() {
    resources_ready = false;
    CInstDraw::MarkAllBuffersNoNeed();
    Text::OnLostDevice();
}
void restore_resources() {
    Text::OnResetDevice();
    S3D_Default();
    resources_ready = true;
}

class Form final : public CForm {
    StandaloneSettings::Values &values;
    CMatrixHint *menu = nullptr;
    bool ready_to_draw = false;
    bool exit_selected = false;
    bool russian = false;

    void release_menu() {
        if (menu) menu->Release();
        menu = nullptr;
    }
    void show_menu() {
        if (!menu) {
            using SettingsModel::text_element;
            auto text = std::wstring(L"2|_BORDER:0:0:0:0|_FONT:Font.2Normal|_WIDTH:360|_HEIGHT:210|_TEXTP:L|_TEXT: |_TEXTP:B");
            text += text_element(L"MatrixGame", L"Font.2Ranger", 0xff000000, 0, 35, 360, 1);
            text += text_element(russian ? L"( \u041d\u043e\u0432\u0430\u044f \u0431\u0438\u0442\u0432\u0430 )" : L"( New battle )", L"Font.2Normal",
                                 exit_selected ? 0xff000000 : 0xff0b5fa5, 50, 95, 260, 1);
            text += text_element(russian ? L"( \u0412\u044b\u0445\u043e\u0434 )" : L"( Exit )", L"Font.2Normal",
                                 exit_selected ? 0xff0b5fa5 : 0xff000000, 50, 140, 260, 1);
            menu = CMatrixHint::Build(text, static_cast<CBlockPar *>(nullptr));
        }
        menu->Show((g_ScreenX - menu->m_Width) / 2, (g_ScreenY - menu->m_Height) / 2);
    }
    void select(bool exit) {
        if (exit_selected == exit) return;
        exit_selected = exit;
        release_menu();
        show_menu();
    }
    void activate() {
        if (exit_selected) SETFLAG(g_Flags, GFLAG_EXITLOOP);
        else {
            release_menu();
            MapWindow::open(MapWindow::Origin::Startup, values);
        }
    }
public:
    explicit Form(StandaloneSettings::Values &settings) : values(settings) {
        m_Name = L"Startup menu";
        russian = SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup")) ==
                  SettingsModel::Language::Russian;
    }
    ~Form() {
        if (g_FormCur == this) FormChange(nullptr);
        MapWindow::shutdown();
        release_menu();
    }
    void Enter() override {
        S3D_Default();
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        show_menu();
    }
    void Leave() override { ClipCursor(nullptr); }
    void Takt(int) override {
        ready_to_draw = false;
        if (Display::apply_pending(resources_ready, release_resources, restore_resources)) return;
        if (Rendering::RecoverDevice(g_D3DD->TestCooperativeLevel(), resources_ready, release_resources,
                [] { return Display::reset_device(g_D3Dpp); }, restore_resources,
                [](auto duration) { MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(duration.count()), QS_ALLINPUT); }))
            return;
        if (!MapWindow::active()) show_menu();
        ready_to_draw = true;
    }
    void Draw() override {
        if (!ready_to_draw) return;
        CInstDraw::DrawFrameBegin();
        ASSERT_DX(g_D3DD->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff14232c, 1.0f, 0));
        ASSERT_DX(g_D3DD->BeginScene());
        CMatrixHint::DrawAll();
        ASSERT_DX(g_D3DD->EndScene());
        Rendering::CheckPresentResult(g_D3DD->Present(nullptr, nullptr, nullptr, nullptr));
        // The static menu does not need an uncapped render loop.
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
    }
    void MouseMove(int x, int y) override {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        if (!resources_ready || g_D3DD->TestCooperativeLevel() != D3D_OK) return;
        if (!menu || MapWindow::active()) return;
        x -= menu->m_PosX; y -= menu->m_PosY;
        if (x >= 50 && x < 310) {
            if (y >= 95 && y < 115) select(false);
            if (y >= 140 && y < 160) select(true);
        }
    }
    void MouseKey(ButtonStatus status, int key, int x, int y) override {
        if (!resources_ready || g_D3DD->TestCooperativeLevel() != D3D_OK) return;
        if (MapWindow::active()) {
            if (status == B_DOWN && key == VK_LBUTTON) MapWindow::mouse_down(x, y);
            if (status == B_WHEEL) MapWindow::mouse_wheel(key, x, y);
            return;
        }
        if (status != B_DOWN || key != VK_LBUTTON || !menu) return;
        x -= menu->m_PosX; y -= menu->m_PosY;
        if (x >= 50 && x < 310) {
            if (y >= 95 && y < 115) { select(false); activate(); }
            else if (y >= 140 && y < 160) { select(true); activate(); }
        }
    }
    void Keyboard(bool down, uint8_t key) override {
        if (!resources_ready || g_D3DD->TestCooperativeLevel() != D3D_OK) return;
        if (MapWindow::keyboard(key, down) || !down) return;
        if (key == VK_UP || key == VK_DOWN || key == VK_TAB) select(!exit_selected);
        else if (key == VK_RETURN) activate();
        else if (key == 'N') { select(false); activate(); }
        else if (key == VK_ESCAPE) SETFLAG(g_Flags, GFLAG_EXITLOOP);
    }
    void SystemEvent(ESysEvent event) override {
        if (event == SYSEV_DEACTIVATING && resources_ready) release_resources();
        if (event == SYSEV_DEACTIVATING || event == SYSEV_INPUT_RESET) Input::onFocusLost();
    }
};
}  // namespace

void set_direct_start(bool enabled) { skip = enabled; }
bool direct_start() { return skip; }

bool run(StandaloneSettings::Values &values) {
    ASSERT(g_MatrixMap == nullptr);
    lgr.notice("Opening the startup menu before battlefield initialization");
    resources_ready = true;
    Form form(values);
    FormChange(&form);
    L3GRun();
    auto choice = MapWindow::take_startup_choice();
    const bool start = choice && !FLAG(g_Flags, GFLAG_APPCLOSE);
    if (start) values = std::move(*choice);
    RESETFLAG(g_Flags, GFLAG_EXITLOOP);
    return start;
}
}  // namespace StartupMenu
