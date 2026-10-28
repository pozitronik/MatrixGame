// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "battle_scenario.hpp"
#include <array>
#include <fstream>
#include <string>

class CMatrixMapStatic;
struct SObjectCore;

namespace tests::ai::competing_repair {

// Each caller invalidates its entire finite family before constructing a fixture.
void invalidate(bool replacement);
void complete_after_cleanup();

class Recording {
public:
    Recording(battle::Session &session, random::Mode mode, int step, bool replacement, bool in_range,
              CMatrixRobotAI *repairer, CMatrixRobotAI *&first, CMatrixRobotAI *second, SObjectCore *first_core);
    void state(const char *phase, int first_pulses, int second_pulses, float first_health, float second_health,
               bool changed, int tick_target);
    void command(const char *name, std::uint64_t before);
    int identity(CMatrixMapStatic *object) const;
    void finish();

private:
    std::array<CMatrixRobotAI *, 6> actors() const;
    battle::Session &session_;
    CMatrixRobotAI *repairer_;
    CMatrixRobotAI *&first_;
    CMatrixRobotAI *second_;
    SObjectCore *first_core_;
    std::string path_;
    std::array<char, 65536> buffer_{};
    std::ofstream stream_;
    bool finished_ = false;
};

} // namespace tests::ai::competing_repair
