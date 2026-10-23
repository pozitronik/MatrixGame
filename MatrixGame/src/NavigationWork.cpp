// MatrixGame - licensed under GPLv2 or any later version.
#include "NavigationWork.hpp"

namespace NavigationWork {
namespace {
Counters *destination = nullptr;
}

Capture::Capture(Counters &next) noexcept : previous_(destination) {
    destination = &next;
}

Capture::~Capture() noexcept {
    destination = previous_;
}

Counters *active() noexcept {
    return destination;
}

}  // namespace NavigationWork
