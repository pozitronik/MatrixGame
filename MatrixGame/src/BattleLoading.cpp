// MatrixGame - licensed under GPLv2 or any later version.
#include "BattleLoading.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <thread>

namespace BattleLoading {
namespace {
constexpr wchar_t class_name[] = L"MatrixGame_Loading";
constexpr std::wstring_view event_prefix = L"Local\\MatrixGame-Battle-";

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() { if (value) CloseHandle(value); }
    void reset() { if (value) CloseHandle(value); value = nullptr; }
};
Handle parent;

class Screen {
    Handle started{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread worker;
    RECT bounds{};
    bool russian;
    bool created = false;
    unsigned pulse = 0;

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto *self = reinterpret_cast<Screen *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Screen *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wp, lp);
        if (message == WM_TIMER) {
            ++self->pulse;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT area{};
            GetClientRect(window, &area);
            HBRUSH background = CreateSolidBrush(RGB(20, 35, 44));
            FillRect(dc, &area, background);
            DeleteObject(background);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(225, 242, 245));
            auto old_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            std::wstring text = self->russian ? L"\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0430 \u0431\u0438\u0442\u0432\u044b" : L"Loading battle";
            text.append(1 + self->pulse % 3, L'.');
            DrawTextW(dc, text.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old_font);
            EndPaint(window, &paint);
            return 0;
        }
        // Loading owns no game input; closing the old game window must not close it.
        if (message == WM_CLOSE) return 0;
        return DefWindowProcW(window, message, wp, lp);
    }

    void run() noexcept {
        WNDCLASSW type{};
        type.lpfnWndProc = procedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.hCursor = LoadCursor(nullptr, IDC_ARROW);
        type.lpszClassName = class_name;
        const ATOM registered = RegisterClassW(&type);
        HWND window = registered ? CreateWindowExW(WS_EX_NOACTIVATE, class_name, L"MatrixGame - Loading",
            WS_POPUP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            nullptr, nullptr, type.hInstance, this) : nullptr;
        created = window != nullptr;
        if (window) {
            ShowWindow(window, SW_SHOWNOACTIVATE);
            UpdateWindow(window);
            SetTimer(window, 1, 350, nullptr);
        }
        SetEvent(started.value);
        if (window) {
            while (MsgWaitForMultipleObjects(1, &stop.value, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
            }
            DestroyWindow(window);
        }
        if (registered) UnregisterClassW(class_name, type.hInstance);
    }
public:
    Screen(HWND source, bool use_russian) : russian(use_russian) {
        if (!started.value || !stop.value) throw std::runtime_error("Cannot create loading-screen events");
        if (!GetWindowRect(source, &bounds)) bounds = {0, 0, 640, 480};
        worker = std::thread([this] { run(); });
        WaitForSingleObject(started.value, INFINITE);
        if (!created) {
            worker.join();
            throw std::runtime_error("Cannot create the loading screen");
        }
    }
    ~Screen() {
        SetEvent(stop.value);
        if (worker.joinable()) worker.join();
    }
};
std::unique_ptr<Screen> screen;
} // namespace

void show(HWND game_window, bool russian) {
    if (!screen && !parent.value && IsWindowVisible(game_window)) screen = std::make_unique<Screen>(game_window, russian);
}
void hide() noexcept { screen.reset(); }

std::wstring event_name() {
    return std::wstring(event_prefix) + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount());
}
bool valid_event_name(std::wstring_view name) {
    if (!name.starts_with(event_prefix) || name.size() <= event_prefix.size() || name.size() > 96) return false;
    return std::all_of(name.begin() + event_prefix.size(), name.end(), [](wchar_t ch) {
        return (ch >= L'0' && ch <= L'9') || ch == L'-';
    });
}
void connect(std::wstring_view name) {
    if (!valid_event_name(name)) throw std::runtime_error("Invalid battle-loading handshake");
    parent.reset();
    parent.value = OpenEventW(EVENT_MODIFY_STATE, FALSE, std::wstring(name).c_str());
    if (!parent.value) throw std::runtime_error("Cannot open the battle-loading handshake");
}
bool has_parent() { return parent.value != nullptr; }
void presented(HRESULT result) {
    if (result != S_OK) return;
    if (parent.value) SetEvent(parent.value);
    parent.reset();
    hide();
}
void failed() noexcept {
    if (parent.value) SetEvent(parent.value);
    parent.reset();
    hide();
}
bool wait(HANDLE ready, HANDLE process) {
    const HANDLE handles[] = {ready, process};
    return WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0;
}
} // namespace BattleLoading
