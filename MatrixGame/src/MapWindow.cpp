// MatrixGame - licensed under GPLv2 or any later version.
#include "MapWindow.hpp"
#include "MapCatalog.hpp"
#include "MapModel.hpp"
#include "MapSelection.hpp"
#include "MapMetadata.hpp"
#include "BattleLoading.hpp"
#include "Text/Font.hpp"
#include "StandaloneRuntime.hpp"

#include "MatrixMap.hpp"
#include "MatrixSoundManager.hpp"
#include "Interface/CInterface.h"
#include "Interface/MatrixHint.hpp"
#include "WindowDisplay.hpp"

#include "CBitmap.hpp"
#include "CBuf.hpp"
#include "CException.hpp"
#include "utils.hpp"

#include <d3dx9.h>

#include <stupid_logger.hpp>

#include <cstring>
#include <memory>
#include <optional>

namespace MapWindow {
namespace {

constexpr const wchar_t *dialog_name = L"Map";

struct State {
    Origin origin;
    StandaloneSettings::Values values;
    MapModel::Window model;
};

std::optional<State> state;
CMatrixHint *startup_hint = nullptr;
std::optional<StandaloneSettings::Values> startup_choice;
CMatrixHint *current_hint() {
    if (state && state->origin == Origin::Startup) return startup_hint;
    return g_MatrixMap && !g_MatrixMap->m_DialogModeHints.empty() ? g_MatrixMap->m_DialogModeHints[0] : nullptr;
}

// Preview of the selected map, registered as a runtime hint bitmap while it is shown. preview_path is
// the last preview attempted, so a preview that fails to decode is not retried on every rebuild.
std::unique_ptr<CBitmap> preview;
SettingsModel::Resolution preview_dimensions{0, 0};
std::wstring preview_path;

// Also frees the pixels, which come from the tracked heap and must be released before its teardown.
void release_preview() {
    CMatrixHint::SetRuntimeBitmap(MapModel::preview_bitmap, nullptr);
    preview.reset();
    preview_dimensions = {0, 0};
    preview_path.clear();
}

// Decodes a JPEG or PNG preview with D3DX, scaled into the preview box, into an opaque ARGB bitmap.
bool decode_preview(const std::wstring &path) {
    CBuf data;
    data.LoadFromFile(path);
    D3DXIMAGE_INFO info{};
    if (FAILED(D3DXGetImageInfoFromFileInMemory(data.Get(), static_cast<UINT>(data.Len()), &info))) return false;
    const auto size = MapModel::preview_size(static_cast<int>(info.Width), static_cast<int>(info.Height));
    if (size.width <= 0) return false;
    IDirect3DSurface9 *surface = nullptr;
    if (FAILED(g_D3DD->CreateOffscreenPlainSurface(size.width, size.height, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH,
                                                   &surface, nullptr)))
        return false;
    bool decoded = false;
    if (SUCCEEDED(D3DXLoadSurfaceFromFileInMemory(surface, nullptr, nullptr, data.Get(), static_cast<UINT>(data.Len()),
                                                  nullptr, D3DX_FILTER_TRIANGLE, 0, nullptr))) {
        D3DLOCKED_RECT locked{};
        if (SUCCEEDED(surface->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            preview = std::make_unique<CBitmap>();
            preview->CreateRGBA(size.width, size.height);
            for (int y = 0; y < size.height; ++y) {
                auto *target = preview->Data() + y * preview->Pitch();
                std::memcpy(target, static_cast<const uint8_t *>(locked.pBits) + y * locked.Pitch, size.width * 4);
                for (int x = 0; x < size.width; ++x) target[x * 4 + 3] = 0xff;
            }
            surface->UnlockRect();
            preview_dimensions = size;
            decoded = true;
        }
    }
    surface->Release();
    return decoded;
}

void load_preview() {
    const auto *map = state->model.selected_map();
    const std::wstring path = map ? map->preview : std::wstring{};
    if (path == preview_path) return;
    release_preview();
    preview_path = path;
    if (path.empty()) return;
    try {
        if (decode_preview(path)) {
            CMatrixHint::SetRuntimeBitmap(MapModel::preview_bitmap, preview.get());
            return;
        }
    }
    catch (const CException &) {
    }
    catch (const std::exception &) {
    }
    lgr.warning("Cannot show the map preview {}")(utils::from_wstring(path));
}


void rebuild() {
    load_preview();
    CMatrixHint *hint = CMatrixHint::Build(MapModel::hint_template(state->model, preview_dimensions),
                                           static_cast<CBlockPar *>(nullptr));
    const int x = (g_ScreenX - hint->m_Width) / 2;
    const int y = (g_ScreenY - hint->m_Height) / 2;
    hint->Show(x, y);
    if (state->origin == Origin::Startup) {
        if (startup_hint) startup_hint->Release();
        startup_hint = hint;
        return;
    }
    auto &hints = g_MatrixMap->m_DialogModeHints;
    if (hints.empty()) {
        hints.push_back(hint);
    }
    else {
        hints[0]->Release();
        hints[0] = hint;
    }

}

MapModel::Language language() {
    return SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup"));
}

void update(bool changed) {
    if (!changed) return;
    if (g_MatrixMap) CSound::Play(S_BCLICK);
    rebuild();
}

}  // namespace

bool available() { return Display::enabled(); }

bool active() {
    return state && current_hint() && (state->origin == Origin::Startup ||
           (g_MatrixMap->m_DialogModeName && wcscmp(g_MatrixMap->m_DialogModeName, dialog_name) == 0));
}

void open(Origin origin, const StandaloneSettings::Values &values) {
    if (!available() || (origin != Origin::Startup && !g_MatrixMap)) return;
    std::string diagnostic;
    const auto metadata = MapMetadata::load(diagnostic);
    if (!diagnostic.empty()) lgr.warning("Map metadata: {}")(diagnostic);
    const auto current = g_MatrixMap ? g_MatrixMap->MapName() : values.map;
    state.emplace(State{origin, values, MapModel::Window(MapCatalog::scan(), current, language(), metadata, values.battle_bonus)});
    state->model.set_text_measure([](std::wstring_view line) { return Text::GetFont(g_D3DD, L"Font.2Small").CalcTextWidth(line); });
    lgr.notice("Map window lists {} maps")(state->model.maps().size());
    if (g_MatrixMap) {
        g_MatrixMap->LeaveDialogMode();
        g_MatrixMap->Pause(true);
        SETFLAG(g_MatrixMap->m_Flags, MMFLAG_DIALOG_MODE);
        g_MatrixMap->m_DialogModeName = dialog_name;
        CSound::Play(L"Formopen");
    }
    rebuild();
}

bool open_after_battle() {
    if (!available() || !g_MatrixMap) return false;
    open(Origin::BattleEnd, StandaloneSettings::current());
    return true;
}

void close(bool play) {
    if (!active()) return;
    if (play) {
        const auto *map = state->model.selected_map();
        if (!map) return;
        if (!MapCatalog::readable(map->path)) {
            lgr.error("The map {} cannot be loaded")(utils::from_wstring(map->path));
            state->model.set_error(MapModel::Error::Unreadable);
            rebuild();
            return;
        }
        std::string error;
        auto values = state->values;
        values.map = map->path;
        values.battle_bonus = state->model.bonus();
        values.display.mode = Display::current_mode();
        values.display.fullscreen = Display::fullscreen_mode();
        const auto size = Display::current_size();
        values.display.width = size.width;
        values.display.height = size.height;
        const bool startup = state->origin == Origin::Startup;
        if (!(startup ? StandaloneSettings::save(values, error) : MapSelection::choose(values, map->path, error))) {
            lgr.error("Cannot save CFG/standalone.txt: {}")(error);
            state->model.set_error(MapModel::Error::Save);
            rebuild();
            return;
        }
        BattleLoading::show(g_Wnd, language() == MapModel::Language::Russian);
        if (startup) {
            startup_choice = values;
            shutdown();
            SETFLAG(g_Flags, GFLAG_EXITLOOP);
            return;
        }
        shutdown();
        g_MatrixMap->LeaveDialogMode();
        SETFLAG(g_Flags, GFLAG_EXITLOOP);
        return;
    }
    const Origin origin = state->origin;
    shutdown();
    if (origin == Origin::Startup) return;
    if (origin == Origin::Menu) {
        g_MatrixMap->EnterDialogMode(TEMPLATE_DIALOG_MENU);
        return;
    }
    g_MatrixMap->LeaveDialogMode();
    SETFLAG(g_Flags, GFLAG_EXITLOOP);
}

bool keyboard(uint8_t vk, bool down) {
    if (!active()) return false;
    if (!down) return true;
    auto &model = state->model;
    const int count = static_cast<int>(model.maps().size());
    switch (vk) {
        case VK_UP: update(model.move(-1)); break;
        case VK_DOWN: update(model.move(1)); break;
        case VK_PRIOR: update(model.scroll_description(-MapModel::Layout::description_rows)); break;
        case VK_NEXT: update(model.scroll_description(MapModel::Layout::description_rows)); break;
        case VK_LEFT: update(model.page(-1)); break;
        case VK_RIGHT: update(model.page(1)); break;
        case '1': case '2': case '3': update(model.select_bonus(vk - '1')); break;
        case VK_HOME: update(model.move(-count)); break;
        case VK_END: update(model.move(count)); break;
        case VK_RETURN:
            lgr.notice("Starting selected map from Enter in the {} selector")(state->origin == Origin::Startup ? "startup" : "battle");
            close(true);
            break;
        case VK_ESCAPE: close(false); break;
        default: break;  // The battle stays paused; other keys are ignored while the window is open.
    }
    return true;
}

bool mouse_down(int x, int y, bool double_click) {
    if (!active()) return false;
    const CMatrixHint *hint = current_hint();
    const auto target = MapModel::hit(state->model, x - hint->m_PosX, y - hint->m_PosY);
    if (target.action) { close(target.action == 1); return true; }
    if (target.bonus >= 0) { update(state->model.select_bonus(target.bonus)); return true; }
    if (target.scroll) { update(state->model.scroll_description(target.scroll)); return true; }
    if (target.index >= 0) {
        update(state->model.select(target.index));
        if (double_click) close(true);
        return true;
    }
    if (target.page != 0) {
        update(state->model.page(target.page));
        return true;
    }
    return false;  // The hint buttons and the rest of the screen keep their handling.
}

void mouse_wheel(int steps, int x, int y) {
    if (!active()) return;
    const auto *hint = current_hint();
    if (x >= hint->m_PosX + MapModel::Layout::preview_x && y >= hint->m_PosY + MapModel::Layout::description_y)
        update(state->model.scroll_description(-steps * 3));
    else update(state->model.move(-steps));
}

void relayout() {
    if (active()) rebuild();
}

void shutdown() {
    if (startup_hint) startup_hint->Release();
    startup_hint = nullptr;
    release_preview();
    state.reset();
}

std::optional<StandaloneSettings::Values> take_startup_choice() {
    auto choice = std::move(startup_choice);
    startup_choice.reset();
    return choice;
}

void customize_menu(std::wstring &menu_template) {
    if (!available()) return;
    utils::replace(menu_template, L"[help]", language() == MapModel::Language::Russian ? L"\u041d\u043e\u0432\u0430\u044f \u0431\u0438\u0442\u0432\u0430 (N)" : L"New battle (N)");
}

bool menu_keyboard(uint8_t vk, bool down) {
    if (!available() || !g_MatrixMap || !g_MatrixMap->m_DialogModeName ||
        wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_MENU) != 0 || g_MatrixMap->m_DialogModeHints.size() != 1)
        return false;
    if (down && vk == 'N') { open(Origin::Menu, StandaloneSettings::current()); return true; }
    return false;
}

bool menu_mouse_down(int x, int y) {
    if (!available() || !g_MatrixMap || !g_MatrixMap->m_DialogModeName ||
        wcscmp(g_MatrixMap->m_DialogModeName, TEMPLATE_DIALOG_MENU) != 0 || g_MatrixMap->m_DialogModeHints.size() != 1)
        return false;
    const auto *hint = g_MatrixMap->m_DialogModeHints[0];
    x -= hint->m_PosX; y -= hint->m_PosY;
    if (x >= 0 && x < 200 && y >= 245 && y < 275) {
        open(Origin::Menu, StandaloneSettings::current());
        return true;
    }
    return false;
}

}  // namespace MapWindow
