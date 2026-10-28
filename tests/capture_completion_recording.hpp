// MatrixGame - licensed under GPLv2 or any later version.
#pragma once
#include "battle_scenario.hpp"
#include <array>
#include <fstream>
#include <string>

class CMatrixBuilding;
class CMatrixMapStatic;
namespace tests::capture_completion {
enum class Caller { NeutralFactory, EnemyFactory, Base, Keyboard, Button };
void invalidate(Caller caller);
void complete_after_cleanup();
struct Input {
    std::uint64_t draws = 0;
    std::array<std::uint32_t, 3> position{};
    unsigned preorder = 0;
};
class Recording {
public:
    Recording(battle::Session &session, CMatrixBuilding &building, int owner, int route,
              random::Mode mode, int step);
    Input input() const;
    void command(const char *name, const Input &before, const wchar_t *element = nullptr);
    void state(const char *phase, std::array<int, 3> progress = {});
    void finish();
private:
    battle::Session &session_;
    CMatrixBuilding &building_;
    std::array<CMatrixMapStatic *, 2> roles_;
    std::string path_;
    std::array<char, 65536> buffer_{};
    std::ofstream stream_;
    bool finished_ = false;
};
} // namespace tests::capture_completion
