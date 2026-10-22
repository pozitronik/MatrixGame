// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include "MatrixDebugInfo.hpp"

namespace tests {
struct DiagnosticEvent {
    SDIItem item;
    bool append;
};

// Observe arguments reaching the no-font diagnostic surrogate. One capture may
// be active at a time; destroy it before the fixture whose teardown can log.
class DiagnosticCapture {
public:
    std::vector<DiagnosticEvent> events;
    DiagnosticCapture();
    ~DiagnosticCapture();
    DiagnosticCapture(const DiagnosticCapture &) = delete;
    DiagnosticCapture &operator=(const DiagnosticCapture &) = delete;
};
}
