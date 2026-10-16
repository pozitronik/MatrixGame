// MatrixGame - licensed under GPLv2 or any later version.
#include "settings_display_host.hpp"
#include "test_support.hpp"
#include "3g.hpp"
#include "WindowDisplay.hpp"
#include <cstring>
#include <cstdlib>

namespace tests {
namespace {
SettingsDisplayHost *owner{};
BOOL WINAPI clip_cursor(const RECT *rectangle) {
    // These inactive-window profiles release only; never change the desktop cursor.
    MG_CHECK(owner && !rectangle);
    ++owner->cursor_releases;
    return TRUE;
}
BOOL WINAPI cursor_position(POINT *) {
    MG_CHECK(owner);
    ++owner->cursor_queries;
    return FALSE; // No native pointer position is fed into the synthetic scene.
}
void replace(void **slot, void *value) {
    DWORD previous;
    MG_CHECK(VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous));
    InterlockedExchangePointer(slot, value);
    DWORD ignored;
    if (!VirtualProtect(slot, sizeof(*slot), previous, &ignored)) std::abort();
}
}
SettingsDisplayHost::SettingsDisplayHost(CForm &form)
    : previous_window_(g_Wnd), previous_form_(g_FormCur), previous_parameters_(g_D3Dpp) {
    MG_CHECK(!owner);
    owner = this;
    try {
        auto *base = reinterpret_cast<BYTE *>(GetModuleHandleW(nullptr));
        const auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        const auto *pe = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
        const auto imports = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        MG_CHECK(imports.VirtualAddress);
        auto *library = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + imports.VirtualAddress);
        for (; library->Name; ++library) {
            if (_stricmp(reinterpret_cast<const char *>(base + library->Name), "user32.dll")) continue;
            MG_CHECK(library->OriginalFirstThunk);
            auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + library->OriginalFirstThunk);
            auto *values = reinterpret_cast<IMAGE_THUNK_DATA *>(base + library->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++values) {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                auto *item = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
                const auto *name = reinterpret_cast<const char *>(item->Name);
                void *replacement = nullptr;
                if (!std::strcmp(name, "ClipCursor")) replacement = reinterpret_cast<void *>(clip_cursor);
                if (!std::strcmp(name, "GetCursorPos")) replacement = reinterpret_cast<void *>(cursor_position);
                if (!replacement) continue;
                auto **slot = reinterpret_cast<void **>(&values->u1.Function);
                imports_.push_back({slot, *slot});
                replace(slot, replacement);
            }
        }
        MG_CHECK(imports_.size() == 2);
        window_ = CreateWindowExW(0, L"STATIC", L"Settings display fixture", WS_OVERLAPPED,
            0, 0, 1280, 720, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        MG_CHECK(window_ && !IsWindowVisible(window_));
        g_Wnd = window_;
        g_FormCur = &form;
        g_D3Dpp = {};
        Display::configure_parameters(g_D3Dpp);
        g_D3Dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    } catch (...) { cleanup(); throw; }
}
void SettingsDisplayHost::cleanup() noexcept {
    if (window_) {
        if (!DestroyWindow(window_)) std::abort();
        window_ = nullptr;
    }
    g_Wnd = previous_window_;
    g_FormCur = previous_form_;
    g_D3Dpp = previous_parameters_;
    for (auto i = imports_.rbegin(); i != imports_.rend(); ++i) {
        try { replace(i->slot, i->previous); }
        catch (...) { std::abort(); }
    }
    imports_.clear();
    owner = nullptr;
}
SettingsDisplayHost::~SettingsDisplayHost() { cleanup(); }
}
