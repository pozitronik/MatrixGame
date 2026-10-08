// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "MatrixConfig.hpp"

// The global engine configuration keeps the representative startup layout.
CMatrixConfig g_Config;

namespace {
struct UnwindGuard {
    bool &destroyed;
    ~UnwindGuard() { destroyed = true; }
};

void throw_failure(bool &destroyed) {
    UnwindGuard guard{destroyed};
    throw std::runtime_error("synthetic startup failure");
}

void exception_unwind() {
    bool destroyed = false;
    bool caught = false;
    void (*operation)(bool &) = throw_failure;
    try {
        operation(destroyed);
    }
    catch (const std::exception &error) {
        MG_CHECK(std::string_view(error.what()) == "synthetic startup failure");
        caught = true;
    }
    MG_CHECK(caught);
    MG_CHECK(destroyed);
}

constexpr tests::Case cases[] = {{"base.compiler.exception_unwind", exception_unwind}};
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    return tests::run(argc, argv, cases);
}
