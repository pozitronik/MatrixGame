// MatrixGame - licensed under GPLv2 or any later version.
#include "native_window_containment.hpp"
#include "native_cursor_state.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <shlobj.h>

namespace tests {
namespace {
HWND hidden_parent;
NativeWindowContainment::WindowMode parent_mode = NativeWindowContainment::WindowMode::Hidden;
NativeWindowContainment::Diagnostic report;
struct Api {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    decltype(&CreateWindowExA) create_a = std::bit_cast<decltype(create_a)>(GetProcAddress(user32, "CreateWindowExA"));
    decltype(&CreateWindowExW) create_w = std::bit_cast<decltype(create_w)>(GetProcAddress(user32, "CreateWindowExW"));
    decltype(&SetWindowLongA) style_a = std::bit_cast<decltype(style_a)>(GetProcAddress(user32, "SetWindowLongA"));
    decltype(&SetWindowLongW) style_w = std::bit_cast<decltype(style_w)>(GetProcAddress(user32, "SetWindowLongW"));
    decltype(&ShowWindow) show = std::bit_cast<decltype(show)>(GetProcAddress(user32, "ShowWindow"));
    decltype(&TrackMouseEvent) track_mouse = std::bit_cast<decltype(track_mouse)>(GetProcAddress(user32, "TrackMouseEvent"));
    decltype(&SHGetSpecialFolderLocation) special_folder = std::bit_cast<decltype(special_folder)>(
        GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHGetSpecialFolderLocation"));
};
Api &api() { static Api value; return value; }

bool game_class(const wchar_t *name) {
    return HIWORD(name) && (wcscmp(name, L"MatrixGame_wc") == 0 || wcscmp(name, L"MatrixGame_Loading") == 0);
}
bool game_class(const char *name) {
    return HIWORD(name) && (std::strcmp(name, "MatrixGame_wc") == 0 || std::strcmp(name, "MatrixGame_Loading") == 0);
}
bool game_window(HWND window) {
    std::array<wchar_t, 80> name{};
    GetClassNameW(window, name.data(), name.size());
    return game_class(name.data());
}
DWORD contained_style(DWORD style) { return (style & ~WS_POPUP) | WS_CHILD; }
DWORD contained_extended(DWORD style) { return (style & ~WS_EX_APPWINDOW) | WS_EX_NOACTIVATE; }
HWND WINAPI contained_create_a(DWORD extended, LPCSTR name, LPCSTR title, DWORD style, int x, int y,
    int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
    if (game_class(name)) {
        parent = hidden_parent; style = contained_style(style); extended = contained_extended(extended);
    }
    return api().create_a(extended, name, title, style, x, y, width, height, parent, menu, instance, parameter);
}
HWND WINAPI contained_create_w(DWORD extended, LPCWSTR name, LPCWSTR title, DWORD style, int x, int y,
    int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
    if (game_class(name)) {
        parent = hidden_parent; style = contained_style(style); extended = contained_extended(extended);
    }
    return api().create_w(extended, name, title, style, x, y, width, height, parent, menu, instance, parameter);
}
LONG WINAPI contained_style_a(HWND window, int index, LONG value) {
    if (game_window(window)) {
        if (index == GWL_STYLE) value = contained_style(value);
        if (index == GWL_EXSTYLE) value = contained_extended(value);
    }
    return api().style_a(window, index, value);
}
LONG WINAPI contained_style_w(HWND window, int index, LONG value) {
    if (game_window(window)) {
        if (index == GWL_STYLE) value = contained_style(value);
        if (index == GWL_EXSTYLE) value = contained_extended(value);
    }
    return api().style_w(window, index, value);
}
BOOL WINAPI contained_show(HWND window, int command) {
    if (window == hidden_parent) return FALSE;
    if (game_window(window)) {
        // Fail before any show if another change bypassed the containment contract.
        if (GetParent(window) != hidden_parent || !(GetWindowLongW(window, GWL_STYLE) & WS_CHILD))
            TerminateProcess(GetCurrentProcess(), 98);
        if (parent_mode == NativeWindowContainment::WindowMode::Visible &&
            !SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
            TerminateProcess(GetCurrentProcess(), 98);
        command = command == SW_HIDE ? SW_HIDE : SW_SHOWNOACTIVATE;
    }
    return api().show(window, command);
}
BOOL WINAPI contained_clip(const RECT *) { return TRUE; }
BOOL WINAPI contained_track_mouse(LPTRACKMOUSEEVENT tracking) {
    // Posted client input has its own explicit leave events. The desktop
    // pointer must not change the posted drag's leave-event schedule.
    if (tracking && tracking->cbSize == sizeof(*tracking) && tracking->dwFlags == TME_LEAVE &&
        game_window(tracking->hwndTrack) && GetParent(tracking->hwndTrack) == hidden_parent)
        return TRUE;
    return api().track_mouse(tracking);
}
HCURSOR WINAPI contained_cursor(HCURSOR cursor) {
    return reinterpret_cast<HCURSOR>(native_window::cursor.set(reinterpret_cast<std::uintptr_t>(cursor)));
}
int WINAPI contained_show_cursor(BOOL visible) { return native_window::cursor.show(visible != FALSE); }
HRESULT WINAPI contained_special_folder(HWND owner, int folder, PIDLIST_ABSOLUTE *result) {
    if (folder == CSIDL_PERSONAL) {
        if (result) *result = nullptr;
        return E_FAIL; // Exercise PathToOutputFiles' existing current-directory fallback.
    }
    return api().special_folder(owner, folder, result);
}
std::wstring wide(const char *text) {
    if (!text) return {};
    const int count = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    std::wstring result(count, L'\0');
    if (count) MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), count);
    return result;
}
int WINAPI contained_message_w(HWND, LPCWSTR text, LPCWSTR title, UINT type) {
    if ((type & MB_TYPEMASK) != MB_OK || !report) TerminateProcess(GetCurrentProcess(), 98);
    report(title ? title : L"", text ? text : L"");
    return IDOK;
}
int WINAPI contained_message_a(HWND window, LPCSTR text, LPCSTR title, UINT type) {
    const auto message = wide(text), heading = wide(title);
    return contained_message_w(window, message.c_str(), heading.c_str(), type);
}
struct Import {
    void **slot;
    void *original;
};
void set_import(void **slot, void *value) {
    DWORD previous;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous))
        throw std::runtime_error("Cannot protect test executable import slot");
    InterlockedExchangePointer(slot, value);
    DWORD ignored;
    if (!VirtualProtect(slot, sizeof(*slot), previous, &ignored))
        throw std::runtime_error("Cannot restore test executable import protection");
}
void *replacement(const char *name) {
    if (!std::strcmp(name, "CreateWindowExA")) return reinterpret_cast<void *>(contained_create_a);
    if (!std::strcmp(name, "CreateWindowExW")) return reinterpret_cast<void *>(contained_create_w);
    if (!std::strcmp(name, "SetWindowLongA")) return reinterpret_cast<void *>(contained_style_a);
    if (!std::strcmp(name, "SetWindowLongW")) return reinterpret_cast<void *>(contained_style_w);
    if (!std::strcmp(name, "ShowWindow")) return reinterpret_cast<void *>(contained_show);
    if (!std::strcmp(name, "TrackMouseEvent")) return reinterpret_cast<void *>(contained_track_mouse);
    if (!std::strcmp(name, "ClipCursor")) return reinterpret_cast<void *>(contained_clip);
    if (!std::strcmp(name, "SetCursor")) return reinterpret_cast<void *>(contained_cursor);
    if (!std::strcmp(name, "ShowCursor")) return reinterpret_cast<void *>(contained_show_cursor);
    if (!std::strcmp(name, "MessageBoxA")) return reinterpret_cast<void *>(contained_message_a);
    if (!std::strcmp(name, "MessageBoxW")) return reinterpret_cast<void *>(contained_message_w);
    if (!std::strcmp(name, "SHGetSpecialFolderLocation")) return reinterpret_cast<void *>(contained_special_folder);
    return nullptr;
}
}

struct NativeWindowContainment::State {
    std::vector<Import> imports;
    ~State() {
        for (auto it = imports.rbegin(); it != imports.rend(); ++it) {
            try { set_import(it->slot, it->original); }
            catch (...) { TerminateProcess(GetCurrentProcess(), 98); }
        }
        if (hidden_parent) DestroyWindow(hidden_parent);
        hidden_parent = nullptr; report = nullptr;
        parent_mode = WindowMode::Hidden;
    }
};
NativeWindowContainment::NativeWindowContainment(Diagnostic diagnostic, bool require_documents, WindowMode mode) {
    static_assert(sizeof(void *) == 4, "Native EXE baseline currently targets x86");
    if (hidden_parent) throw std::runtime_error("Window containment already active");
    state = std::make_unique<State>();
    native_window::cursor = {};
    parent_mode = mode;
    const bool visible = mode == WindowMode::Visible;
    hidden_parent = api().create_w(WS_EX_NOACTIVATE | (visible ? WS_EX_TOOLWINDOW : 0), L"STATIC",
        visible ? L"MatrixGame native test" : L"Invisible native-test parent", WS_POPUP,
        visible ? 50 : 0, visible ? 50 : 0, visible ? 640 : 2048, visible ? 480 : 2048,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hidden_parent || IsWindowVisible(hidden_parent)) throw std::runtime_error("Cannot create invisible test parent");
    report = diagnostic;
    auto *base = reinterpret_cast<BYTE *>(GetModuleHandleW(nullptr));
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *headers = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const auto directory = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) throw std::runtime_error("Test executable has no import directory");
    auto *descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress);
    bool documents_adapted = false, cursor_adapted = false, cursor_count_adapted = false, clip_adapted = false;
    for (; descriptor->Name; ++descriptor) {
        const auto *library = reinterpret_cast<char *>(base + descriptor->Name);
        if (_stricmp(library, "user32.dll") != 0 && _stricmp(library, "shell32.dll") != 0) continue;
        if (!descriptor->OriginalFirstThunk) throw std::runtime_error("Test window/output import names unavailable");
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + descriptor->OriginalFirstThunk);
        auto *addresses = reinterpret_cast<IMAGE_THUNK_DATA *>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto *item = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (auto *function = replacement(reinterpret_cast<char *>(item->Name))) {
                auto **slot = reinterpret_cast<void **>(&addresses->u1.Function);
                state->imports.push_back({slot, *slot});
                set_import(slot, function);
                if (!std::strcmp(reinterpret_cast<char *>(item->Name), "SHGetSpecialFolderLocation")) documents_adapted = true;
                if (!std::strcmp(reinterpret_cast<char *>(item->Name), "SetCursor")) cursor_adapted = true;
                if (!std::strcmp(reinterpret_cast<char *>(item->Name), "ShowCursor")) cursor_count_adapted = true;
                if (!std::strcmp(reinterpret_cast<char *>(item->Name), "ClipCursor")) clip_adapted = true;
            }
        }
    }
    if (state->imports.empty()) throw std::runtime_error("No test window imports adapted");
    if (require_documents && !documents_adapted) throw std::runtime_error("Test Documents lookup was not contained");
    if (!cursor_adapted || !cursor_count_adapted || !clip_adapted) throw std::runtime_error("Test cursor presentation was not contained");
    if (visible && !SetWindowPos(hidden_parent, HWND_TOPMOST, 50, 50, 640, 480,
                                 SWP_NOACTIVATE | SWP_SHOWWINDOW))
        throw std::runtime_error("Cannot show bounded native test parent");
}
NativeWindowContainment::~NativeWindowContainment() = default;
HWND NativeWindowContainment::parent() const { return hidden_parent; }
}
