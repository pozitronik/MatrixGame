// MatrixGame - licensed under GPLv2 or any later version.
#include "SettingsWindow.hpp"
#include "SettingsModel.hpp"
#include "StandaloneRuntime.hpp"

#include "MatrixMap.hpp"
#include "MatrixSoundManager.hpp"
#include "Interface/CInterface.h"
#include "Interface/MatrixHint.hpp"
#include "WindowDisplay.hpp"
#include "input.hpp"

#include <stupid_logger.hpp>

#include <optional>

namespace SettingsWindow {
namespace {

constexpr const wchar_t *dialog_name = L"Settings";

// The original battle menu draws its settings entry in a disabled color; the standalone game
// enables it and adds a click area over the label and the baked-in button.
constexpr const wchar_t *menu_label_disabled = L"_COLOR:FF6794A3|_POS:6:105|_TEXT:[setup]";
constexpr const wchar_t *menu_label_enabled = L"_COLOR:FFADA793|_POS:6:105|_TEXT:[setup]";
constexpr const wchar_t *menu_label_highlight = L"-1|_FONT:Font.2Ranger|_WIDTH:200|_COLOR:FF6FE4FF|_TEXT:[setup]";
constexpr int menu_label_x = 6;
constexpr int menu_label_y = 105;

struct Area {
    int left, top, right, bottom;
    bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
};
constexpr Area menu_areas[] = {{5, 100, 160, 122}, {70, 121, 122, 168}};

std::optional<SettingsModel::Window> model;
CMatrixHint *menu_highlight = nullptr;

bool menu_ready() {
    return g_MatrixMap && g_MatrixMap->m_DialogModeName &&
           wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_MENU) == 0 &&
           g_MatrixMap->m_DialogModeHints.size() == 1;
}

bool inside_menu_entry(int x, int y) {
    const CMatrixHint *menu = g_MatrixMap->m_DialogModeHints[0];
    for (const auto &area : menu_areas) {
        if (area.contains(x - menu->m_PosX, y - menu->m_PosY)) return true;
    }
    return false;
}

void apply_handler() { close(true); }
void cancel_handler() { close(false); }

void rebuild() {
    auto &hints = g_MatrixMap->m_DialogModeHints;
    CMatrixHint *hint = CMatrixHint::Build(SettingsModel::hint_template(*model), static_cast<CBlockPar *>(nullptr));
    const int x = (g_ScreenX - hint->m_Width) / 2;
    const int y = (g_ScreenY - hint->m_Height) / 2;
    hint->Show(x, y);
    if (hints.empty()) {
        hints.push_back(hint);
    }
    else {
        hints[0]->Release();
        hints[0] = hint;
    }
    if (hint->GetCopyPosCnt() > 1) {
        g_IFaceList->CreateHintButton(x + hint->GetCopyPos(0).x, y + hint->GetCopyPos(0).y, HINT_OK, apply_handler);
        g_IFaceList->CreateHintButton(x + hint->GetCopyPos(1).x, y + hint->GetCopyPos(1).y, HINT_CANCEL,
                                      cancel_handler);
    }
}

SettingsModel::Hit hit(int x, int y) {
    const CMatrixHint *hint = g_MatrixMap->m_DialogModeHints[0];
    return SettingsModel::hit(x - hint->m_PosX, y - hint->m_PosY);
}

std::vector<SettingsModel::Resolution> adapter_modes() {
    std::vector<SettingsModel::Resolution> modes;
    const UINT count = g_D3D->GetAdapterModeCount(D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8);
    for (UINT i = 0; i < count; ++i) {
        D3DDISPLAYMODE mode{};
        if (SUCCEEDED(g_D3D->EnumAdapterModes(D3DADAPTER_DEFAULT, D3DFMT_X8R8G8B8, i, &mode)))
            modes.push_back({static_cast<int>(mode.Width), static_cast<int>(mode.Height)});
    }
    return modes;
}

SettingsModel::Language language() {
    return SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup"));
}

void release_menu_highlight() {
    if (menu_highlight) menu_highlight->Release();
    menu_highlight = nullptr;
}

}  // namespace

bool available() {
#ifdef BUILD_EXE
    return Display::enabled();
#else
    return false;
#endif
}

bool active() {
    return model && g_MatrixMap && g_MatrixMap->m_DialogModeName &&
           wcscmp(g_MatrixMap->m_DialogModeName, dialog_name) == 0 && !g_MatrixMap->m_DialogModeHints.empty();
}

void open() {
    if (!available() || !g_MatrixMap) return;
    auto values = StandaloneSettings::current();
    values.display.mode = Display::current_mode();  // Alt+Enter changes are shown and saved by Apply.
    values.display.fullscreen = Display::fullscreen_mode();
    model.emplace(values, adapter_modes(), language());

    g_MatrixMap->LeaveDialogMode();
    g_MatrixMap->Pause(true);
    SETFLAG(g_MatrixMap->m_Flags, MMFLAG_DIALOG_MODE);
    g_MatrixMap->m_DialogModeName = dialog_name;
    CSound::Play(L"Formopen");
    rebuild();
}

void close(bool apply) {
    if (!active()) return;
    if (apply) {
        const auto values = model->values();
        std::string error;
        if (!StandaloneSettings::save(values, error)) {
            lgr.error("Cannot save CFG/standalone.txt: {}")(error);
            model->set_error(true);
            rebuild();
            return;
        }
        StandaloneSettings::apply_runtime(values);
        lgr.notice("Settings saved to CFG/standalone.txt{}")(
                StandaloneSettings::restart_pending(values) ? "; some changes apply after restart" : "");
    }
    model.reset();
    g_MatrixMap->EnterDialogMode(TEMPLATE_DIALOG_MENU);
}

bool keyboard(uint8_t vk, bool down) {
    if (!active()) return false;
    if (!down) return true;
    bool update = false;
    switch (vk) {
        case VK_UP:
            model->move(-1);
            update = true;
            break;
        case VK_DOWN:
            model->move(1);
            update = true;
            break;
        case VK_LEFT: update = model->change(model->selected(), -1); break;
        case VK_RIGHT: update = model->change(model->selected(), 1); break;
        case VK_RETURN: close(true); return true;
        case VK_ESCAPE: close(false); return true;
        default: return true;  // The battle stays paused; other keys are ignored while the dialog is open.
    }
    if (update) {
        CSound::Play(S_BCLICK);
        rebuild();
    }
    return true;
}

void mouse_move(int x, int y) {
    if (!active()) return;
    const auto target = hit(x, y);
    if (target.row >= 0 && model->select(target.row)) rebuild();
}

bool mouse_down(int x, int y) {
    if (!active()) return false;
    const auto target = hit(x, y);
    if (target.row < 0) return false;  // The hint buttons and the rest of the screen keep their handling.
    model->select(target.row);
    model->change(target.row, target.direction);
    CSound::Play(S_BCLICK);
    rebuild();
    return true;
}

void customize_menu(std::wstring &menu_template) {
    if (!available()) return;
    utils::replace(menu_template, menu_label_disabled, menu_label_enabled);
}

bool menu_mouse_down(int x, int y) {
    if (!available() || !menu_ready() || !inside_menu_entry(x, y)) return false;
    release_menu_highlight();
    CSound::Play(S_BCLICK);
    open();
    return true;
}

void menu_mouse_move(int x, int y) {
    if (!available() || !menu_ready() || !inside_menu_entry(x, y)) {
        release_menu_highlight();
        return;
    }
    if (menu_highlight) return;
    const CMatrixHint *menu = g_MatrixMap->m_DialogModeHints[0];
    menu_highlight = CMatrixHint::Build(menu_label_highlight, g_MatrixData->BlockGet(PAR_REPLACE));
    menu_highlight->Show(menu->m_PosX + menu_label_x, menu->m_PosY + menu_label_y);
    CSound::Play(S_BENTER);
}

void menu_closed() { release_menu_highlight(); }

void shutdown() {
    release_menu_highlight();
    model.reset();
}

}  // namespace SettingsWindow
