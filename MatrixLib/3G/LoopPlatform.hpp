// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "BaseDef.hpp"
#include <chrono>

namespace Loop {
using Clock = std::chrono::high_resolution_clock;

// Narrow platform boundary for deterministic loop tests. Callbacks must remain
// valid for the synchronous run; normal callers use the parameterless L3GRun.
struct Platform {
    void *context;
    Clock::time_point (*now)(void *);
    bool (*poll)(void *, MSG &);
    void (*dispatch)(void *, const MSG &);
    void (*wait)(void *);
};
}

int L3GRun(const Loop::Platform &platform);
