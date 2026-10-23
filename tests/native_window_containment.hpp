// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include <windows.h>
#include <memory>

namespace tests {
// Only for a test executable. Adapts its imported USER32 calls and Documents lookup;
// it never installs a system hook or changes another module/process.
class NativeWindowContainment {
    struct State;
    std::unique_ptr<State> state;
public:
    using Diagnostic = void (*)(const wchar_t *title, const wchar_t *text);
    enum class WindowMode { Hidden, Visible };
    explicit NativeWindowContainment(Diagnostic diagnostic, bool require_documents = true,
                                     WindowMode mode = WindowMode::Hidden);
    ~NativeWindowContainment();
    NativeWindowContainment(const NativeWindowContainment &) = delete;
    NativeWindowContainment &operator=(const NativeWindowContainment &) = delete;
    HWND parent() const;
};
}
